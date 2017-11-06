/*
 * Copyright 2014 - 2017 Stepan Perun
 * This program is free software.
 * License: GNU LESSER GENERAL PUBLIC LICENSE
 * http://www.gnu.org/licenses/lgpl.html
*/

#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gst/video/videooverlay.h>
#include <stdlib.h>

#include "gtv.h"
#include "gtv-scan.h"


static GstElement *dvbplay, *dvb_all_n[15], *dvb_rec_all_n[6];

static GtkWindow *main_window;
static GtkDrawingArea *video_window;
static GdkPixbuf *tv_logo = NULL;
static GSimpleActionGroup *group;

static GtkBox *tool_hbox, *sw_vbox;
static GtkVolumeButton *volbutton;
static GtkLabel *signal_snr, *label_tv_rec;

static GtkToolbar  *toolbar_media, *toolbar_media_sw;
static GtkToolItem * tv_get_item_toolbar_media ( GtkToolbar *toolbar, gint i );

static guintptr video_window_handle = 0;
static gdouble volume_start = 0.5;
static guint j = 0, a = 0, c = 0, d = 0, tv_time_rec = 0;
static gboolean video_enable = TRUE, rec_status = TRUE, msgerr = FALSE;

static void tv_stop ();
static void tv_gst_rec_remove ();

static void tv_goup  ();
static void tv_down  ();
static void tv_remv  ();
static void tv_clear ();
static void tv_quit  ( /*GtkWindow *window*/ );

gchar *channels_conf;




void tv_set_tuning_timeout ( GstElement *element )
{
    guint64 timeout = 0;
    g_object_get ( element, "tuning-timeout", &timeout, NULL );
    g_object_set ( element, "tuning-timeout", (guint64)timeout / 5, NULL );
}

void tv_str_split_ch_data ( gchar *data )
{
    gchar **lines;
    lines = g_strsplit ( data, ":", 0 );

        if ( !g_str_has_prefix ( data, "#" ) )
            tv_add_channels ( lines[0], data );

    g_strfreev ( lines );
}

void tv_add_channels ( const gchar *name_ch, gchar *data )
{
    GtkTreeIter iter;
    GtkTreeModel *model = gtk_tree_view_get_model ( GTK_TREE_VIEW ( tv_treeview ) );
    guint ind = gtk_tree_model_iter_n_children ( model, NULL );

    gtk_list_store_append ( GTK_LIST_STORE ( model ), &iter);
    gtk_list_store_set    ( GTK_LIST_STORE ( model ), &iter,
                            COL_NUM, ind+1,
                            COL_FILES_CH, name_ch,
                            COL_URI_DATA, data,
                            -1 );
}

void tv_set_sgn_snr ( GstElement *element, GtkLabel *label, gdouble sgb, gdouble srb, gboolean hlook )
{
    gchar *texta = g_strdup_printf ( "Signal %d%s", (int)sgb, "%" );
    gchar *textb = g_strdup_printf ( "Snr %d%s",    (int)srb, "%" );

    const gchar *format = NULL;

    if ( GST_ELEMENT_CAST ( element ) -> current_state == GST_STATE_PLAYING )
    {
        if ( hlook )
            format = "<span>\%s</span><span foreground=\"#00ff00\"> ◉ </span><span>\%s</span>";
        else
            format = "<span>\%s</span><span foreground=\"#ff0000\"> ◉ </span><span>\%s</span>";

        gchar *markup = g_markup_printf_escaped ( format, texta, textb );
            gtk_label_set_markup ( label, markup );
        g_free ( markup );
    }
    if ( GST_ELEMENT_CAST ( element ) -> current_state == GST_STATE_NULL )
    {
        gtk_label_set_text ( label, "Signal 0  &  Snr 0" );
    }

    g_free ( texta );
    g_free ( textb );
}



static void tv_message_dialog ( gchar *f_error, gchar *file_or_info, GtkMessageType mesg_type )
{
    GtkMessageDialog *dialog = ( GtkMessageDialog *)gtk_message_dialog_new (
                                 main_window, GTK_DIALOG_MODAL,
                                 mesg_type,   GTK_BUTTONS_CLOSE,
                                 "%s\n%s",    f_error, file_or_info );

    gtk_dialog_run     ( GTK_DIALOG ( dialog ) );
    gtk_widget_destroy ( GTK_WIDGET ( dialog ) );
}

static gchar * tv_get_time_date_str ()
{
    GDateTime *datetime = g_date_time_new_now_local ();

    gint doy = g_date_time_get_day_of_year ( datetime );

    gint tth = g_date_time_get_hour   ( datetime );
    gint ttm = g_date_time_get_minute ( datetime );
    gint tts = g_date_time_get_second ( datetime );

    return g_strdup_printf ( "%d-%d-%d-%d", doy, tth, ttm, tts );
}

static GstBusSyncReply tv_bus_sync_handler ( GstBus *bus, GstMessage *message )
{
    if ( !gst_is_video_overlay_prepare_window_handle_message ( message ) )
        return GST_BUS_PASS;

    if ( video_window_handle != 0 )
    {
        GstVideoOverlay *xoverlay = GST_VIDEO_OVERLAY ( GST_MESSAGE_SRC ( message ) );
        gst_video_overlay_set_window_handle ( xoverlay, video_window_handle );

    } else { g_warning ( "Should have obtained video_window_handle by now!" ); }

    gst_message_unref ( message );
    return GST_BUS_DROP;
}

static void tv_video_window_realize ( GtkDrawingArea *drawingare )
{
    gulong xid = GDK_WINDOW_XID ( gtk_widget_get_window ( GTK_WIDGET ( drawingare ) ) );
    video_window_handle = xid;
}

static void tv_msg_all ( GstBus *bus, GstMessage *msg )
{
    const GstStructure *structure = gst_message_get_structure ( msg );

    if ( structure )
    {
        gint signal, snr;
        gboolean hlook = FALSE;

        if (  gst_structure_get_int ( structure, "signal", &signal )  )
        {
            gst_structure_get_int ( structure, "snr", &snr );
            gst_structure_get_boolean ( structure, "lock", &hlook );

            tv_set_sgn_snr ( dvbplay, signal_snr, (signal * 100) / 0xffff, (snr * 100) / 0xffff, hlook );
        }
    }
}
static void tv_msg_err ( GstBus *bus, GstMessage *msg )
{
    GError *err = NULL;
    gchar *dbg  = NULL;

    gst_message_parse_error ( msg, &err, &dbg );
    g_printerr ( "ERROR: %s (%s)\n", err->message, (dbg) ? dbg : "no details" );

    if ( msgerr ) tv_message_dialog ( err->message, (dbg) ? dbg : " ", GTK_MESSAGE_ERROR );

    g_error_free ( err );
    g_free ( dbg );

    //if ( msgerr ) tv_stop ();

    msgerr = TRUE;
}

static gboolean tv_gst_create ()
{
    dvb_mpegts_initialize ();

    dvbplay  = gst_pipeline_new ( "pipeline0" );

    if ( !dvbplay )
    {
        g_printerr ( "dvbplay - not created.\n" );
        return FALSE;
    }

    GstBus *bus = gst_element_get_bus ( dvbplay );
    gst_bus_add_signal_watch_full ( bus, G_PRIORITY_DEFAULT );
    gst_bus_set_sync_handler ( bus, (GstBusSyncHandler)tv_bus_sync_handler, NULL, NULL );
    gst_object_unref (bus);

    g_signal_connect ( bus, "message",        G_CALLBACK ( tv_msg_all ), NULL );
    g_signal_connect ( bus, "message::error", G_CALLBACK ( tv_msg_err ), NULL );

    return TRUE;
}

static void tv_gst_pad_link ( GstPad *pad, GstElement *element, const gchar *name, GstElement *element_n )
{
    GstPad *pad_va_sink = gst_element_get_static_pad ( element, "sink" );

    if ( gst_pad_link ( pad, pad_va_sink ) == GST_PAD_LINK_OK )
    	gst_object_unref ( pad_va_sink );
    else
       	g_print ( "Linking demux/decode name: %s & video/audio pad failed \n", name );
}

static void tv_pad_demux_added_audio ( GstElement *element, GstPad *pad, GstElement *element_audio )
{
    const gchar *name = gst_structure_get_name ( gst_caps_get_structure ( gst_pad_query_caps ( pad, NULL ), 0 ) );

    if ( g_str_has_prefix ( name, "audio" ) )
        tv_gst_pad_link ( pad, element_audio, name, element );
}
static void tv_pad_demux_added_video ( GstElement *element, GstPad *pad, GstElement *element_video )
{
    const gchar *name = gst_structure_get_name ( gst_caps_get_structure ( gst_pad_query_caps ( pad, NULL ), 0 ) );

    if ( g_str_has_prefix ( name, "video" ) )
        tv_gst_pad_link ( pad, element_video, name, element );
}

static void tv_pad_decode_added ( GstElement *element, GstPad *pad, GstElement *element_va )
{
    const gchar *name = gst_structure_get_name ( gst_caps_get_structure ( gst_pad_query_caps ( pad, NULL ), 0 ) );

    tv_gst_pad_link ( pad, element_va, name, element );
}

static void tv_gst_tsdemux ()
{

struct dvb_all_list { gchar *name; } dvb_all_list_n[] =
{
    { "dvbsrc"  }, { "tsdemux"   },
    { "queue2"  }, { "decodebin" }, { "videoconvert" }, { "tee" }, { "queue2"  },/*{ "queue2" },*/ { "autovideosink" },
    { "queue2"  }, { "decodebin" }, { "audioconvert" }, { "tee" }, { "queue2"  },  { "volume" },   { "autoaudiosink" }
};

    for ( c = 0; c < G_N_ELEMENTS ( dvb_all_n ); c++ )
    {
        dvb_all_n[c] = gst_element_factory_make ( dvb_all_list_n[c].name, NULL );

    	if ( !dvb_all_n[c] )
    	{
            g_printerr ( "dvb element - %s - not all elements could be created.\n", dvb_all_list_n[c].name );
            return;
        }
    }

    if ( video_enable ) { a = 2; } else { a = 8; }

    gst_bin_add ( GST_BIN ( dvbplay ), dvb_all_n[0] );
    gst_bin_add ( GST_BIN ( dvbplay ), dvb_all_n[1] );

    for ( c = a; c < G_N_ELEMENTS ( dvb_all_n ); c++ )
        gst_bin_add ( GST_BIN ( dvbplay ), dvb_all_n[c] );

    gst_element_link_many ( dvb_all_n[0], dvb_all_n[1], NULL );

    g_signal_connect ( dvb_all_n[1], "pad-added", G_CALLBACK ( tv_pad_demux_added_audio ), dvb_all_n[8] );
    g_signal_connect ( dvb_all_n[1], "pad-added", G_CALLBACK ( tv_pad_demux_added_video ), dvb_all_n[2] );

    if ( video_enable )
    {
        gst_element_link_many ( dvb_all_n[2], dvb_all_n[3], NULL );
        gst_element_link_many ( dvb_all_n[4], dvb_all_n[5], dvb_all_n[6], dvb_all_n[7], NULL );

        g_signal_connect ( dvb_all_n[3], "pad-added", G_CALLBACK ( tv_pad_decode_added ), dvb_all_n[4] );
    }

    gst_element_link_many ( dvb_all_n[8],  dvb_all_n[9], NULL );
    gst_element_link_many ( dvb_all_n[10], dvb_all_n[11], dvb_all_n[12], dvb_all_n[13], dvb_all_n[14], NULL );

    g_signal_connect ( dvb_all_n[9], "pad-added", G_CALLBACK ( tv_pad_decode_added ), dvb_all_n[10] );
}

static void tv_gst_tsdemux_remove ()
{
    gst_bin_remove ( GST_BIN ( dvbplay ), dvb_all_n[0] );
    gst_bin_remove ( GST_BIN ( dvbplay ), dvb_all_n[1] );

    for ( c = a; c < G_N_ELEMENTS ( dvb_all_n ); c++ )
        gst_bin_remove ( GST_BIN ( dvbplay ), dvb_all_n[c] );

    if ( !rec_status ) tv_gst_rec_remove ();
}

static void tv_gst_rec ()
{

struct dvb_rec_all_list { const gchar *name; } dvb_all_rec_list_n[] =
{
    { "queue2" }, { video_encoder }, // enc_video
    { "queue2" }, { audio_encoder }, // enc_audio
    { muxer    }, { "filesink"    }
};

    for ( c = d; c < G_N_ELEMENTS ( dvb_rec_all_n ); c++ )
    {
        dvb_rec_all_n[c] = gst_element_factory_make ( dvb_all_rec_list_n[c].name, NULL );

    	if ( !dvb_rec_all_n[c] )
    	{
            g_printerr ( "dvb rec element - %s - not all elements could be created.\n", dvb_all_rec_list_n[c].name );
            return;
    	}
    }

    if ( video_enable ) { d = 0; } else { d = 2; }

  gst_element_set_state ( dvbplay, GST_STATE_PAUSED );

    for ( c = d; c < G_N_ELEMENTS ( dvb_rec_all_n ); c++ )
        gst_bin_add ( GST_BIN (dvbplay), dvb_rec_all_n[c] );

    if ( video_enable )
        gst_element_link_many ( dvb_all_n[5], dvb_rec_all_n[0], dvb_rec_all_n[1], dvb_rec_all_n[4], NULL );

    gst_element_link_many ( dvb_all_n[11], dvb_rec_all_n[2], dvb_rec_all_n[3], dvb_rec_all_n[4], NULL );

    gst_element_link_many ( dvb_rec_all_n[4], dvb_rec_all_n[5], NULL );

    const gchar *ch_name = gtk_window_get_title ( main_window );
    gchar *file_rec = g_strdup_printf ( "%s/%s_%s.%s", rec_dir, ch_name, tv_get_time_date_str (), file_ext );
    g_object_set ( dvb_rec_all_n[5], "location", file_rec, NULL );
    g_free ( file_rec );

    for ( c = d; c < G_N_ELEMENTS ( dvb_rec_all_n ); c++ )
        gst_element_set_state ( dvb_rec_all_n[c], GST_STATE_PAUSED );

    g_usleep ( 250000 );

  gst_element_set_state ( dvbplay, GST_STATE_PLAYING );

}

static void tv_gst_rec_remove ()
{
    for ( c = d; c < G_N_ELEMENTS ( dvb_rec_all_n ); c++ )
        gst_bin_remove ( GST_BIN (dvbplay), dvb_rec_all_n[c] );

    //g_usleep ( 250000 );
}

static void tv_sensitive ( gboolean set_sensitive, guint start_s, guint end_s )
{
    gtk_widget_set_sensitive ( GTK_WIDGET ( volbutton ), set_sensitive );

    const gchar *menu_n[] = { "Record", "Stop", "Mute"/*, "EQ-Audio", "EQ-Video" */};

    guint i;
    for ( i = 0; i < G_N_ELEMENTS ( menu_n ); i++ )
        g_action_group_action_enabled_changed ( G_ACTION_GROUP (group), menu_n[i], set_sensitive );


    for ( j = start_s; j < end_s; j++ )
        gtk_widget_set_sensitive ( GTK_WIDGET ( tv_get_item_toolbar_media ( toolbar_media, j ) ), set_sensitive );
}

static void tv_checked_video ( gchar *data )
{
	if ( !g_strrstr ( data, "video-pid" ) || g_strrstr ( data, "video-pid=0" ) )
		 video_enable = FALSE;
	else
		 video_enable = TRUE;
}

static gchar * tv_data_split_set_dvb ( gchar *data )
{
    GstElement *element = dvb_all_n[0];
    tv_set_tuning_timeout ( element );

    gchar **fields = g_strsplit ( data, ":", 0 );
    guint numfields = g_strv_length ( fields );

    gchar *ch_name = g_strdup ( fields[0] );

    for ( j = 1; j < numfields; j++ )
    {
        if ( g_strrstr ( fields[j], "delsys" ) || g_strrstr ( fields[j], "audio-pid" ) || g_strrstr ( fields[j], "video-pid" ) ) continue;

        if ( !g_strrstr ( fields[j], "=" ) ) continue;

        gchar **splits = g_strsplit ( fields[j], "=", 0 );

            if ( g_strrstr ( splits[0], "program-number" ) )
                g_object_set ( dvb_all_n[1], "program-number", atoi ( splits[1] ), NULL );
            else if ( g_strrstr ( splits[0], "polarity" ) )
                g_object_set ( element, splits[0], splits[1], NULL);
            else if ( g_strrstr ( splits[0], "lnb-type" ) )
                tv_set_lnb ( element, atoi ( splits[1]) );
            else
                g_object_set ( element, splits[0], atoi ( splits[1] ), NULL);

        g_strfreev (splits);
    }

    g_strfreev (fields);

    return ch_name;
}

static void tv_stop ()
{
    if ( GST_ELEMENT_CAST ( dvbplay ) -> current_state != GST_STATE_NULL )
    {
        gst_element_set_state ( dvbplay, GST_STATE_NULL );
        tv_gst_tsdemux_remove ();
        rec_status = TRUE;

        tv_sensitive  ( FALSE, 0, gtk_toolbar_get_n_items ( GTK_TOOLBAR ( toolbar_media ) ) - 2 );
        gtk_label_set_text ( signal_snr, "Signal 0  &  Snr 0" );
        gtk_window_set_title  ( GTK_WINDOW ( main_window ), "Gtv" );
        gtk_widget_queue_draw ( GTK_WIDGET ( main_window ) );
    }
}

static void tv_play ( gchar *data )
{
    if ( GST_ELEMENT_CAST ( dvbplay ) -> current_state != GST_STATE_PLAYING )
    {
        tv_checked_video ( data );
        tv_gst_tsdemux ();

        gchar *ch_name = tv_data_split_set_dvb ( data );
            gtk_window_set_title ( main_window, ch_name );
        g_free ( ch_name );

        gst_element_set_state ( dvbplay, GST_STATE_PLAYING );
        tv_sensitive ( TRUE, 0, gtk_toolbar_get_n_items ( GTK_TOOLBAR ( toolbar_media ) ) - 2 );
    }
}

static void tv_stop_play ( gchar *data )
{
    gdouble volume = volume_start;

    if ( GST_ELEMENT_CAST ( dvbplay ) -> current_state == GST_STATE_PLAYING )
        volume = gtk_scale_button_get_value ( GTK_SCALE_BUTTON ( volbutton ) );

    tv_stop ();
    tv_play ( data );

    g_object_set ( G_OBJECT (dvb_all_n[13]), "volume", volume, NULL );
}

gboolean trig_lr = TRUE;
gboolean refresh_rec ()
{
    if ( rec_status )
    {
        gtk_label_set_text ( GTK_LABEL ( label_tv_rec ), "" );
        return FALSE;
    }

    gchar *file_rec = NULL;
    g_object_get ( dvb_rec_all_n[5], "location", &file_rec, NULL );

    struct stat sb;

    if ( stat ( file_rec, &sb ) == -1 )
    {
        perror ( "stat" );
        gtk_label_set_text ( GTK_LABEL ( label_tv_rec ), "" );
        return FALSE;
    }

    g_free ( file_rec );

    const gchar *format = NULL;
    gchar *text = NULL;

    long long fsz = (long long)sb.st_size;

        if ( fsz > 1000000 )
            text = g_strdup_printf ( " %lld Mb ", fsz / 1000000 );
        else if ( fsz > 1000 )
            text = g_strdup_printf ( " %lld kb ", fsz / 1000 );

    if ( trig_lr )
        format = "<span foreground=\"#ff0000\"> ◉ </span> \%s ";
    else
        format = "<span foreground=\"#000\">   </span> \%s ";

    gchar *markup = g_markup_printf_escaped ( format, text ? text : "" );
        gtk_label_set_markup ( label_tv_rec, markup );
    g_free ( markup );

    g_free ( text );

    trig_lr = !trig_lr;

    return TRUE;
}

static void tv_rec ()
{

    if ( rec_status )
    {
        tv_gst_rec ();
        tv_time_rec = g_timeout_add_seconds ( 1, (GSourceFunc)refresh_rec, NULL );
    }
    else
    {
        g_source_remove ( tv_time_rec );
        gtk_label_set_text ( GTK_LABEL ( label_tv_rec ), "" );

        gst_element_set_state ( dvbplay, GST_STATE_NULL );
            tv_gst_rec_remove ();
        gst_element_set_state ( dvbplay, GST_STATE_PLAYING );
    }

    rec_status = !rec_status;
}


static void tv_scan  () { tv_win_scan (); }

static void tv_volume_changed ( GtkScaleButton *button, gdouble value )
{
    if ( GST_ELEMENT_CAST ( dvbplay ) -> current_state == GST_STATE_PLAYING )
      g_object_set ( dvb_all_n[13], "volume", value, NULL );
}

static void tv_volume_mute ()
{
    gboolean mute;
    g_object_get ( dvb_all_n[13], "mute", &mute, NULL );
    g_object_set ( dvb_all_n[13], "mute", !mute, NULL );

    gtk_widget_set_sensitive ( GTK_WIDGET ( volbutton ), mute );
}
static void tv_mute ()
{
    if ( GST_ELEMENT_CAST ( dvbplay ) -> current_state == GST_STATE_PLAYING )
      tv_volume_mute ();
}

static void tv_hide () { gtk_widget_hide ( GTK_WIDGET ( tool_hbox ) ); gtk_widget_hide ( GTK_WIDGET ( sw_vbox ) ); }
static void tv_show () { gtk_widget_show ( GTK_WIDGET ( tool_hbox ) ); gtk_widget_hide ( GTK_WIDGET ( sw_vbox ) ); }

static void tv_mini ()
{
    if ( gtk_widget_get_visible ( GTK_WIDGET ( tool_hbox ) ) )
        tv_hide ();
    else
        tv_show ();
}

static void tv_plist ()
{
    if ( gtk_widget_get_visible ( GTK_WIDGET ( sw_vbox ) ) )
        gtk_widget_hide ( GTK_WIDGET ( sw_vbox ) );
    else
        gtk_widget_show ( GTK_WIDGET ( sw_vbox ) );
}

static void tv_flscr ()
{
    GdkWindowState state = gdk_window_get_state ( gtk_widget_get_window ( GTK_WIDGET ( main_window ) ) );

    if ( state & GDK_WINDOW_STATE_FULLSCREEN )
        { gtk_window_unfullscreen ( GTK_WINDOW ( main_window ) ); tv_show ();  }
    else
        { gtk_window_fullscreen   ( GTK_WINDOW ( main_window ) ); tv_hide (); }
}

static void tv_draw_black ( GtkDrawingArea *widget, cairo_t *cr )
{
    GdkRGBA color; color.red = 0; color.green = 0; color.blue = 0; color.alpha = 1.0;

    gint width  = gtk_widget_get_allocated_width  ( GTK_WIDGET ( widget ) );
    gint height = gtk_widget_get_allocated_height ( GTK_WIDGET ( widget ) );

    gint widthl  = gdk_pixbuf_get_width  ( tv_logo );
    gint heightl = gdk_pixbuf_get_height ( tv_logo );

    cairo_rectangle ( cr, 0, 0, width, height );
    gdk_cairo_set_source_rgba ( cr, &color );
    cairo_fill (cr);

    if ( tv_logo != NULL )
    {
        cairo_rectangle ( cr, 0, 0, width, height );
        gdk_cairo_set_source_pixbuf ( cr, tv_logo,
            ( width / 2  ) - ( widthl  / 2 ),
            ( height / 2 ) - ( heightl / 2 ) );

        cairo_fill (cr);
    }
}

static gboolean tv_draw_callback ( GtkDrawingArea *widget, cairo_t *cr )
{
    if (  GST_ELEMENT_CAST ( dvbplay )->current_state == GST_STATE_NULL  )
        { tv_draw_black ( widget, cr ); return TRUE; }

    if (  GST_ELEMENT_CAST ( dvbplay )->current_state != GST_STATE_NULL  )
        if ( !video_enable )
            { tv_draw_black ( widget, cr ); return TRUE; }

    return FALSE;
}

static gboolean tv_press_event ( GtkDrawingArea *drawingare, GdkEventButton *event, GtkMenu *menu )
{
    if ( event->button == 1 && event->type == GDK_2BUTTON_PRESS ) { tv_flscr (); return TRUE; }
    if ( event->button == 2 ) { tv_mute (); return TRUE; }
    if ( event->button == 3 )
    {
        //gtk_menu_popup ( menu, NULL, NULL, NULL, NULL, event->button, event->time );
        gtk_menu_popup_at_pointer ( menu, NULL ); // GTK_VER ( 3,22,0 )
        return TRUE;
    }
    return FALSE;
}


const struct tv_list_media { const gchar *label; gchar *name_icon; void (* activate); const gchar *accel_key; } tv_list_media_n[] =
{
    // Toolbar media
    { "Record",   "media-record",            tv_rec,   "<control>r" }, { "Stop",       "media-playback-stop", tv_stop,  "<control>x" },
    //{ "EQ-Audio", "preferences-desktop",  tv_audio, "<control>a" }, { "EQ-Video",   "preferences-desktop", tv_video, "<control>v" },
    { "Channels", "applications-multimedia", tv_plist, "<control>l" }, { "Scan",       "display",             tv_scan,  "<control>u" },

    // Toolbar sw
    { "Up",       "up",                   tv_goup,  "<control>z" }, { "Down",       "down",                tv_down,  "<control>z" },
    { "Remove",   "remove",               tv_remv,  "<control>z" }, { "Clear",      "trashcan_empty",          tv_clear, "<control>z" },

    // Menu ( only )
    { "Mute",    "audio-volume-muted",    tv_mute,  "<control>m" }, { "Fullscreen", "view-fullscreen",     tv_flscr, "<control>f" },
    { "Mini",    "view-restore",          tv_mini,  "<control>h" }, { "Quit",       "window-close",        tv_quit,  "<control>q" }
};

void tv_create_gaction_entry ( GtkApplication *app )
{
    group = g_simple_action_group_new ();

    GActionEntry entries[ G_N_ELEMENTS ( tv_list_media_n ) ];

    guint i; for ( i = 0; i < G_N_ELEMENTS ( tv_list_media_n ); i++ )
    {
        entries[i].name           = tv_list_media_n[i].label;
        entries[i].activate       = tv_list_media_n[i].activate;
        entries[i].parameter_type = NULL; // g_variant_new_string ( tv_list_media_n[i].accel_key );
        entries[i].state          = NULL;

        gchar *text = g_strconcat ( "app.", tv_list_media_n[i].label, NULL );
        const gchar *accelf[] = { tv_list_media_n[i].accel_key, NULL };
            gtk_application_set_accels_for_action ( app, text, accelf );
        g_free ( text );
    }

    g_action_map_add_action_entries ( G_ACTION_MAP ( app ),   entries, G_N_ELEMENTS ( entries ), NULL );
    g_action_map_add_action_entries ( G_ACTION_MAP ( group ), entries, G_N_ELEMENTS ( entries ), NULL );
}

static GtkToolItem * tv_get_item_toolbar_media ( GtkToolbar *toolbar, gint i )
{
    return gtk_toolbar_get_nth_item ( GTK_TOOLBAR ( toolbar ), i );
}

static GtkToolbar * tv_create_toolbar_all ( guint start, guint stop )
{
    GtkToolbar *toolbar_create = (GtkToolbar *)gtk_toolbar_new ();

    GtkToolItem *item;
    gtk_widget_set_valign ( GTK_WIDGET ( toolbar_create ), GTK_ALIGN_CENTER );

    for ( j = start; j < stop; j++ )
    {
        item = gtk_tool_button_new ( NULL, tv_list_media_n[j].label );
        gtk_tool_button_set_icon_name ( GTK_TOOL_BUTTON ( item ), tv_list_media_n[j].name_icon );
        g_signal_connect ( item, "clicked", G_CALLBACK ( tv_list_media_n[j].activate ), NULL );

        gtk_toolbar_insert ( toolbar_create, item, -1 );
    }

    gtk_toolbar_set_icon_size ( toolbar_create, GTK_ICON_SIZE_MENU ); // GTK_ICON_SIZE_LARGE_TOOLBAR

    return toolbar_create;
}

static GtkToolbar * tv_create_toolbar_media ( guint start_num, guint end_num )
{
    return tv_create_toolbar_all ( start_num, end_num );
}
static GtkToolbar * tv_create_toolbar_sw ( guint start_num, guint end_num )
{
    return tv_create_toolbar_all ( start_num, end_num );
}

GMenu * tv_create_gmenu ()
{
    GMenu *menu = g_menu_new ();
    GMenuItem *mitem;

    gint dat_n[] = { 1, 0, 2, 3, 8, 10, 9, 11 };

    guint i;
    for ( i = 0; i < G_N_ELEMENTS ( dat_n ); i++ )
    {
        gchar *text = g_strconcat ( "menu.", tv_list_media_n[ dat_n[i] ].label, NULL );
            mitem = g_menu_item_new ( tv_list_media_n[ dat_n[i] ].label, text );
        g_free ( text );

        g_menu_item_set_icon ( mitem, G_ICON ( gtk_icon_theme_load_icon ( gtk_icon_theme_get_default (),
                               tv_list_media_n[ dat_n[i] ].name_icon, 16, GTK_ICON_LOOKUP_NO_SVG, NULL ) ) );

        g_menu_item_set_attribute_value ( mitem, "accel", g_variant_new_string ( tv_list_media_n[ dat_n[i] ].accel_key ) );
        g_menu_append_item ( menu, mitem );
    }

    return menu;
}

GtkMenu * tv_create_menu ()
{
    GtkMenu *tv_menu = (GtkMenu *)gtk_menu_new_from_model ( G_MENU_MODEL ( tv_create_gmenu () ) );
    gtk_widget_insert_action_group ( GTK_WIDGET ( tv_menu ), "menu", G_ACTION_GROUP ( group ) );

    return tv_menu;
}


static void tv_tree_view_row_activated ( GtkTreeView *tree_view, GtkTreePath *path/*, GtkTreeViewColumn *column*/ )
{
    GtkTreeIter iter;
    GtkTreeModel *model = gtk_tree_view_get_model ( GTK_TREE_VIEW ( tree_view ) );

    if ( gtk_tree_model_get_iter ( model, &iter, path ) )
    {
        gchar *file_ch = NULL;
            gtk_tree_model_get ( model, &iter, COL_URI_DATA, &file_ch, -1 );
            tv_stop_play ( file_ch );
        g_free ( file_ch );
    }
}

static void tv_create_columns ( GtkTreeView *tree_view, GtkTreeViewColumn *column, GtkCellRenderer *renderer, gchar *name, gint column_id, gboolean col_vis )
{
    column = gtk_tree_view_column_new_with_attributes ( name, renderer, "text", column_id, NULL );
    gtk_tree_view_append_column ( tree_view, column );
    gtk_tree_view_column_set_visible ( column, col_vis );
}
static void tv_add_columns ( GtkTreeView *tree_view, gchar *title, gchar *data )
{
    struct col_title_list { gchar *title; gboolean vis; }
    col_title_list_n[] = { { "Num", TRUE }, { title, TRUE }, { data, FALSE } };

    GtkTreeViewColumn *column_num_ch_data[NUM_COLS];
    GtkCellRenderer *renderer = gtk_cell_renderer_text_new ();

    for ( c = 0; c < NUM_COLS; c++ )
        tv_create_columns ( tree_view, column_num_ch_data[c], renderer, col_title_list_n[c].title, c, col_title_list_n[c].vis );
}

GtkScrolledWindow * tv_scroll_win ( GtkTreeView *tree_view, gchar *title, gchar *data )
{
    GtkScrolledWindow *scroll_win = (GtkScrolledWindow *)gtk_scrolled_window_new ( NULL, NULL );
    gtk_scrolled_window_set_policy ( scroll_win, GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC );
    gtk_widget_set_size_request ( GTK_WIDGET ( scroll_win ), 200, -1 );

    gtk_tree_view_set_search_column ( GTK_TREE_VIEW ( tree_view ), COL_FILES_CH );
    tv_add_columns ( tree_view, title, data );

    g_signal_connect ( tree_view, "row-activated", G_CALLBACK ( tv_tree_view_row_activated ), NULL );
    gtk_container_add ( GTK_CONTAINER ( scroll_win ), GTK_WIDGET ( tree_view ) );

    return scroll_win;
}

static void tv_treeview_reread_mini ( GtkTreeView *tree_view )
{
    GtkTreeIter iter;
    GtkTreeModel *model = gtk_tree_view_get_model ( GTK_TREE_VIEW ( tree_view ) );

    gint row_count = 1;
    gboolean valid;
    for ( valid = gtk_tree_model_get_iter_first ( model, &iter ); valid;
          valid = gtk_tree_model_iter_next ( model, &iter ) )
    {
        gtk_list_store_set ( GTK_LIST_STORE ( model ), &iter, COL_NUM, row_count++, -1 );
    }
}

static void tv_treeview_up_down ( GtkTreeView *tree_view, gboolean up_dw )
{
    GtkTreeIter iter;
    GtkTreeModel *model = gtk_tree_view_get_model ( GTK_TREE_VIEW ( tree_view ) );
    gint ind = gtk_tree_model_iter_n_children ( model, NULL );

    if ( ind > 1 )
    if ( gtk_tree_selection_get_selected ( gtk_tree_view_get_selection ( tree_view ), NULL, &iter ) )
    {
        GtkTreeIter *iter_c = gtk_tree_iter_copy ( &iter );

            if ( up_dw )
                if ( gtk_tree_model_iter_previous ( model, &iter ) )
                    gtk_list_store_move_before ( GTK_LIST_STORE ( model ), iter_c, &iter );

            if ( !up_dw )
                if ( gtk_tree_model_iter_next ( model, &iter ) )
                    gtk_list_store_move_after ( GTK_LIST_STORE ( model ), iter_c, &iter );

        gtk_tree_iter_free ( iter_c );
        tv_treeview_reread_mini ( tree_view );
    }
}

static void tv_treeview_remove ( GtkTreeView *tree_view )
{
    GtkTreeIter iter;
    GtkTreeModel *model = gtk_tree_view_get_model ( GTK_TREE_VIEW ( tree_view ) );

    if ( gtk_tree_selection_get_selected ( gtk_tree_view_get_selection ( tree_view ), NULL, &iter ) )
    {
        gtk_list_store_remove ( GTK_LIST_STORE ( model ), &iter );
        tv_treeview_reread_mini ( tree_view );
    }
}

static void tv_treeview_clear ( GtkTreeView *tree_view )
{
    GtkTreeModel *model = gtk_tree_view_get_model ( GTK_TREE_VIEW ( tree_view ) );
    gtk_list_store_clear ( GTK_LIST_STORE ( model) );
}

static void tv_read_file_ch_to_treeview ( GtkTreeView *tree_view, const gchar *filename )
{
    guint n = 0;
    gchar *contents;
    GError *err = NULL;

    if ( g_file_get_contents ( filename, &contents, 0, &err ) )
    {
        gchar **lines = g_strsplit ( contents, "\n", 0 );

        for ( n = 0; lines[n] != NULL; n++ )
            if ( *lines[n] )
                tv_str_split_ch_data ( lines[n] );

        g_strfreev ( lines );
        g_free ( contents );
    }
    else
        g_critical ( "ERROR: %s\n", err->message );

        if ( err ) g_error_free ( err );
}

static void mp_treeview_to_file ( GtkTreeView *tree_view, gchar *filename )
{
    GString *gstring = g_string_new ( "# Convert DVB 5 format \n" );

    GtkTreeIter iter;
    GtkTreeModel *model = gtk_tree_view_get_model ( GTK_TREE_VIEW ( tree_view ) );

    gboolean valid;
    for ( valid = gtk_tree_model_get_iter_first ( model, &iter ); valid;
          valid = gtk_tree_model_iter_next ( model, &iter ) )
    {
        gchar *data = NULL;

            gtk_tree_model_get ( model, &iter, COL_URI_DATA, &data, -1 );

            gstring = g_string_append ( gstring, data );
            gstring = g_string_append ( gstring, "\n" );

        g_free ( data );
    }

    if ( !g_file_set_contents ( filename, gstring->str, -1, NULL ) )
        g_print ( "Save failed: file %s. \n", filename );

    g_string_free ( gstring, TRUE );
}




static void tv_goup  ()
{
    tv_treeview_up_down ( tv_treeview, TRUE );
}
static void tv_down  ()
{
    tv_treeview_up_down ( tv_treeview, FALSE );
}
static void tv_clear ()
{
    tv_treeview_clear ( tv_treeview );
}
static void tv_remv ()
{
    tv_treeview_remove ( tv_treeview );
}



gchar * tv_rec_dir ()
{
    GtkFileChooserDialog *dialog = ( GtkFileChooserDialog *)gtk_file_chooser_dialog_new (
                    "Folder",  main_window, GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
                    "gtk-cancel", GTK_RESPONSE_CANCEL,
                    "gtk-apply",  GTK_RESPONSE_ACCEPT,
                     NULL );

    gtk_file_chooser_set_current_folder ( GTK_FILE_CHOOSER ( dialog ), g_get_home_dir () );

    gchar *dirname = NULL;

    if ( gtk_dialog_run ( GTK_DIALOG ( dialog ) ) == GTK_RESPONSE_ACCEPT )
        dirname = gtk_file_chooser_get_filename ( GTK_FILE_CHOOSER ( dialog ) );

    gtk_widget_destroy ( GTK_WIDGET ( dialog ) );

    return dirname;
}

gchar * tv_openf ()
{
    gchar *filename = NULL;

    GtkFileChooserDialog *dialog = ( GtkFileChooserDialog *)gtk_file_chooser_dialog_new (
                    "Open File",  main_window, GTK_FILE_CHOOSER_ACTION_OPEN,
                    "gtk-cancel", GTK_RESPONSE_CANCEL,
                    "gtk-open",   GTK_RESPONSE_ACCEPT,
                     NULL );

    gtk_file_chooser_set_current_folder  ( GTK_FILE_CHOOSER ( dialog ), g_get_home_dir () );
    gtk_file_chooser_set_select_multiple ( GTK_FILE_CHOOSER ( dialog ), FALSE );

    if ( gtk_dialog_run ( GTK_DIALOG ( dialog ) ) == GTK_RESPONSE_ACCEPT )
        filename = gtk_file_chooser_get_filename ( GTK_FILE_CHOOSER ( dialog ) );

    gtk_widget_destroy ( GTK_WIDGET ( dialog ) );

    return filename;
}

static GtkBox * tv_create_sgn_snr ()
{
    GtkBox *tbar_dvb = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_VERTICAL, 0 );

    signal_snr = (GtkLabel *)gtk_label_new ( "Signal 0  &  Snr 0" );
    gtk_box_pack_start ( tbar_dvb, GTK_WIDGET ( signal_snr  ), FALSE, FALSE, 10 );

    return tbar_dvb;
}

static void tv_strat_hide ()
{
    tv_sensitive ( FALSE, 0, gtk_toolbar_get_n_items ( GTK_TOOLBAR ( toolbar_media ) ) - 2 );
}

static void tv_init ()
{
    gchar *dir_conf = g_strdup_printf ( "%s/gtv", g_get_user_config_dir () );

    if ( !g_file_test ( dir_conf, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_DIR ) )
    {
        g_mkdir ( dir_conf, 0777 );
        g_print ( "Creating %s directory. \n", dir_conf );
    }

    g_free ( dir_conf );

    channels_conf = g_strconcat ( g_get_user_config_dir (), "/gtv/gtv-channel.conf", NULL );
    rec_dir  = g_strdup ( g_get_home_dir () );

    audio_encoder = g_strdup ( "vorbisenc" );
    video_encoder = g_strdup ( "theoraenc" );
    muxer = g_strdup ( "oggmux" );
    file_ext = g_strdup ( "ogg" );

    if ( g_file_test ( "/usr/share/themes/Adwaita-dark", G_FILE_TEST_EXISTS | G_FILE_TEST_IS_DIR ) )
        g_object_set ( gtk_settings_get_default (), "gtk-theme-name", "Adwaita-dark", NULL );

    //g_object_set ( gtk_settings_get_default (), "gtk-icon-theme-name", "gnome", NULL );

    tv_logo = gtk_icon_theme_load_icon ( gtk_icon_theme_get_default (),
              "applications-multimedia", 64, GTK_ICON_LOOKUP_USE_BUILTIN, NULL );
}

static void tv_read_ch ( GtkTreeView *tree_view )
{
    if ( g_file_test ( channels_conf, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_REGULAR ) )
        tv_read_file_ch_to_treeview ( tree_view, channels_conf );
    else
        tv_win_scan ();
}

static void tv_auto_save ()
{
    GtkTreeModel *model = gtk_tree_view_get_model ( GTK_TREE_VIEW ( tv_treeview ) );
    guint ind = gtk_tree_model_iter_n_children ( model, NULL );

    if ( ind > 0 ) mp_treeview_to_file ( tv_treeview, channels_conf );
}

static void tv_quit ( /*GtkWindow *window*/ )
{
    tv_auto_save ();
    tv_stop ();

    gtk_widget_destroy ( GTK_WIDGET ( main_window ) );
}

static void tv_win_base ( GtkApplication *app )
{
    tv_init ();

    GtkBox *main_box, *main_hbox, *tool_hbox_sw;
    GtkListStore *liststore;
    GtkScrolledWindow *scrollwin;

    main_window = (GtkWindow *)gtk_application_window_new ( app );
    gtk_window_set_title ( main_window, "Gtv" );
    gtk_window_set_default_size ( main_window, 900, 400 );
    gtk_window_set_default_icon_name ( "display" );
    g_signal_connect ( main_window, "destroy", G_CALLBACK ( tv_quit ), NULL );

    video_window = (GtkDrawingArea *)gtk_drawing_area_new ();
    g_signal_connect ( video_window, "realize", G_CALLBACK ( tv_video_window_realize ), video_window );
    g_signal_connect ( video_window, "draw",    G_CALLBACK ( tv_draw_callback ), NULL );

    GtkMenu *menu = tv_create_menu ();

    gtk_widget_add_events ( GTK_WIDGET ( video_window ), GDK_BUTTON_PRESS_MASK );
    g_signal_connect ( video_window, "button-press-event", G_CALLBACK ( tv_press_event ), menu );

    main_hbox   = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_HORIZONTAL, 0 );
    liststore   = (GtkListStore *)gtk_list_store_new ( 3, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING );
    tv_treeview = (GtkTreeView *)gtk_tree_view_new_with_model ( GTK_TREE_MODEL ( liststore ) );

    scrollwin        = tv_scroll_win ( tv_treeview, "Channels", "Data" );
    toolbar_media    = tv_create_toolbar_media ( 0, 4 );
    toolbar_media_sw = tv_create_toolbar_sw    ( 4, 8 );

    volbutton = (GtkVolumeButton *)gtk_volume_button_new ();
    gtk_scale_button_set_value ( GTK_SCALE_BUTTON ( volbutton ), volume_start );
    g_signal_connect ( volbutton, "value-changed", G_CALLBACK ( tv_volume_changed ), NULL );

    tool_hbox_sw = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_HORIZONTAL, 0 );

    GtkButton *button_mute = (GtkButton *)gtk_button_new_from_icon_name ( "audio-volume-muted", GTK_ICON_SIZE_SMALL_TOOLBAR );
    g_signal_connect ( button_mute, "clicked", G_CALLBACK ( tv_mute ), NULL );

    label_tv_rec = (GtkLabel *)gtk_label_new ( "" );

    tool_hbox = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_HORIZONTAL, 0 );
        gtk_box_pack_start ( tool_hbox, GTK_WIDGET ( toolbar_media ), TRUE,  TRUE,  0  );
        gtk_box_pack_end   ( tool_hbox, GTK_WIDGET ( volbutton   ),   FALSE, FALSE, 0  );
        gtk_box_pack_end   ( tool_hbox, GTK_WIDGET ( button_mute ),   FALSE, FALSE, 0  );
        gtk_box_pack_end   ( tool_hbox, GTK_WIDGET ( label_tv_rec ),  FALSE, FALSE, 10 );

    sw_vbox = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_VERTICAL, 0 );
    gtk_box_pack_start ( sw_vbox, GTK_WIDGET ( scrollwin ), TRUE, TRUE, 0 );
    gtk_box_pack_start ( sw_vbox, GTK_WIDGET ( tv_create_sgn_snr () ), FALSE, FALSE, 0 );
    gtk_box_pack_start ( sw_vbox, GTK_WIDGET ( toolbar_media_sw ), FALSE, FALSE, 0 );
    gtk_box_pack_start ( sw_vbox, GTK_WIDGET ( tool_hbox_sw ),     FALSE, FALSE, 0 );
    gtk_widget_set_size_request ( GTK_WIDGET ( sw_vbox ), 200, -1 );

    GtkPaned *hpaned = (GtkPaned *)gtk_paned_new ( GTK_ORIENTATION_HORIZONTAL );
    gtk_paned_add1 ( hpaned, GTK_WIDGET ( sw_vbox ) );
    gtk_paned_add2 ( hpaned, GTK_WIDGET ( video_window ) );
        gtk_box_pack_start ( main_hbox, GTK_WIDGET ( hpaned ), TRUE, TRUE, 0 );

    main_box = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_VERTICAL, 0 );
        gtk_box_pack_start ( main_box, GTK_WIDGET ( main_hbox ), TRUE,  TRUE,  0 );
        gtk_box_pack_start ( main_box, GTK_WIDGET ( tool_hbox ), FALSE, FALSE, 0 );
    gtk_container_add ( GTK_CONTAINER ( main_window ), GTK_WIDGET ( main_box ) );

    gtk_widget_realize  ( GTK_WIDGET ( video_window ) );

    gtk_widget_show_all ( GTK_WIDGET ( main_window  ) );
    tv_strat_hide ();

    tv_read_ch ( tv_treeview );
}

static void activate ( GtkApplication *app )
{
    tv_create_gaction_entry ( app );
    tv_win_base ( app );
}

int main ()
{
    gst_init ( NULL, NULL );

    if ( !tv_gst_create () ) return -1;

    GtkApplication *app = gtk_application_new ( NULL, G_APPLICATION_FLAGS_NONE );
    g_signal_connect ( app, "activate", G_CALLBACK ( activate ),  NULL );

    int status = g_application_run ( G_APPLICATION ( app ), 0, NULL );
    g_object_unref ( app );

    return status;
}
