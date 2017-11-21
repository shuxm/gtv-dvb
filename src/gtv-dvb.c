/*
 * Copyright 2014 - 2017 Stepan Perun
 * This program is free software.
 * License: GNU LESSER GENERAL PUBLIC LICENSE
 * http://www.gnu.org/licenses/lgpl.html
*/


#include <gtk/gtk.h>
#include <gst/gst.h>

#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gst/video/videooverlay.h>
#include <stdlib.h>
#include <glib/gstdio.h>

#include <fcntl.h>
#include <time.h>

#include <linux/dvb/frontend.h>
#include <sys/ioctl.h>

#define GST_USE_UNSTABLE_API
#include <gst/mpegts/mpegts.h>

#include <locale.h>
#include <glib/gi18n.h>


static GstElement *dvbplay, *dvb_all_n[17], *dvb_rec_all_n[6];

static GtkWindow *main_window;
static GtkDrawingArea *video_window;
static GdkPixbuf *tv_logo = NULL;
static GSimpleActionGroup *group;

static GtkBox *tool_hbox, *sw_vbox;
static GtkVolumeButton *volbutton;
static GtkLabel *signal_snr, *label_tv_rec;

static GtkTreeView *tv_treeview;
static GtkToolbar  *toolbar_media, *toolbar_media_sw;
static GtkToolItem * tv_get_item_toolbar_media ( GtkToolbar *toolbar, gint i );
enum { COL_NUM, COL_FILES_CH, COL_URI_DATA, NUM_COLS };

static guintptr video_window_handle = 0;
static gdouble volume_start = 0.5;
static guint j = 0, a = 0, b = 0, c = 0, tv_time_rec = 0;
static gboolean video_enable = TRUE, rec_status = TRUE, rec_en_ts = TRUE, w_info = FALSE, fmsg_i = TRUE;

static void tv_stop ();
static void tv_gst_rec_remove ();

static void tv_goup  ();
static void tv_down  ();
static void tv_remv  ();
static void tv_clear ();
static void tv_quit  ( /*GtkWindow *window*/ );

gchar *channels_conf, *rec_dir, *video_parser, *audio_parser,
      *audio_encoder, *video_encoder, *muxer, *file_ext;


static void dvb_mpegts_initialize ();
const gchar * enum_name ( GType instance_type, gint val );

static void tv_set_lnb ( GstElement *element, gint num_lnb );
static void tv_win_scan ();



static void tv_message_dialog ( gchar *f_error, gchar *file_or_info, GtkMessageType mesg_type )
{
    GtkMessageDialog *dialog = ( GtkMessageDialog *)gtk_message_dialog_new (
                                 main_window, GTK_DIALOG_MODAL,
                                 mesg_type,   GTK_BUTTONS_CLOSE,
                                 "%s\n%s",    f_error, file_or_info );

    gtk_dialog_run     ( GTK_DIALOG ( dialog ) );
    gtk_widget_destroy ( GTK_WIDGET ( dialog ) );
}

static void tv_set_sgn_snr ( GstElement *element, GtkLabel *label, gdouble sgb, gdouble srb, gboolean hlook )
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

static gchar * tv_get_time_date_str ()
{
    GDateTime *datetime = g_date_time_new_now_local ();

    gint doy = g_date_time_get_day_of_year ( datetime );

    gint tth = g_date_time_get_hour   ( datetime );
    gint ttm = g_date_time_get_minute ( datetime );
    gint tts = g_date_time_get_second ( datetime );

    return g_strdup_printf ( "%d-%d-%d-%d", doy, tth, ttm, tts );
}

static void tv_info_widget_name ( GtkWidget *widget )
{
    if ( w_info ) g_print ( "Widget name: %s \n", gtk_widget_get_name ( widget ) );
}
static void tv_info_object_name ( GstObject *object )
{
    if ( w_info )
    {
        gchar *object_name = gst_object_get_name ( object );
            g_print ( "Object name: %s \n", object_name );
        g_free ( object_name );
    }
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

    tv_info_object_name ( GST_OBJECT ( bus ) );

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
        tv_info_object_name ( GST_OBJECT ( bus ) );
    }
}
static void tv_msg_err ( GstBus *bus, GstMessage *msg )
{
    GError *err = NULL;
    gchar *dbg  = NULL;

    gst_message_parse_error ( msg, &err, &dbg );
    g_printerr ( "ERROR: %s (%s)\n", err->message, (dbg) ? dbg : "no details" );

    if ( fmsg_i )
        tv_message_dialog ( err->message, (dbg) ? dbg : " ", GTK_MESSAGE_ERROR );

    fmsg_i = FALSE;

    tv_info_object_name ( GST_OBJECT ( bus ) );

    g_error_free ( err );
    g_free ( dbg );

    // tv_stop ();
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

static void tv_set_tuning_timeout ( GstElement *element )
{
    guint64 timeout = 0;
    g_object_get ( element, "tuning-timeout", &timeout, NULL );
    g_object_set ( element, "tuning-timeout", (guint64)timeout / 5, NULL );
}

struct list_types { gchar *type; gchar *parser; } list_types_n;

struct list_types list_type_video_n[] =
{
	{ "mpeg4", "mpeg4videoparse" }, { "mpegts", "mpegvideoparse"  },
	{ "mpeg",  "mpegvideoparse"  }, { "h264",   "h264parse"       }
};
struct list_types list_type_audio_n[] =
{
    { "mpeg", "mpegaudioparse" }, { "ac3", "ac3parse" }, { "aac", "aacparse" }
};

static void tv_checked_type ( const gchar *name, guint num, struct list_types list_types_all[] )
{
    for ( c = 0; c < num; c++ )
    {
        if ( g_str_has_suffix ( name, list_types_all[c].type ) )
        {
            if ( g_str_has_prefix ( name, "audio" ) ) audio_parser = list_types_all[c].parser;
            if ( g_str_has_prefix ( name, "video" ) ) video_parser = list_types_all[c].parser;
        }
    }
}

static void tv_gst_pad_link ( GstPad *pad, GstElement *element, const gchar *name, GstElement *element_n )
{
    GstPad *pad_va_sink = gst_element_get_static_pad ( element, "sink" );

    if ( gst_pad_link ( pad, pad_va_sink ) == GST_PAD_LINK_OK )
    	gst_object_unref ( pad_va_sink );
    else
    {
        if ( w_info )
        {
            g_printerr ( "Linking demux/decode name: %s & video/audio pad failed \n", name );
            tv_info_object_name ( GST_OBJECT ( element_n ) );
        }
    }
}

static void tv_pad_demux_added_audio ( GstElement *element, GstPad *pad, GstElement *element_audio )
{
    const gchar *name = gst_structure_get_name ( gst_caps_get_structure ( gst_pad_query_caps ( pad, NULL ), 0 ) );

    if ( g_str_has_prefix ( name, "audio" ) )
    {
        tv_checked_type ( name, G_N_ELEMENTS ( list_type_audio_n ), list_type_audio_n );
        tv_gst_pad_link ( pad, element_audio, name, element );
    }
}
static void tv_pad_demux_added_video ( GstElement *element, GstPad *pad, GstElement *element_video )
{
    const gchar *name = gst_structure_get_name ( gst_caps_get_structure ( gst_pad_query_caps ( pad, NULL ), 0 ) );

    if ( g_str_has_prefix ( name, "video" ) )
    {
        tv_checked_type ( name, G_N_ELEMENTS ( list_type_video_n ), list_type_video_n );
        tv_gst_pad_link ( pad, element_video, name, element );
    }
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
    { "dvbsrc" }, { "tsdemux" },
    { "tee"    }, { "queue2"  }, { "decodebin" }, { "videoconvert" }, { "tee" }, { "queue2"  },/*{ "queue2" },*/ { "autovideosink" },
    { "tee"    }, { "queue2"  }, { "decodebin" }, { "audioconvert" }, { "tee" }, { "queue2"  },  { "volume" },   { "autoaudiosink" }
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

    if ( video_enable ) { a = 2; } else { a = 9; }

    gst_bin_add ( GST_BIN ( dvbplay ), dvb_all_n[0] );
    gst_bin_add ( GST_BIN ( dvbplay ), dvb_all_n[1] );

    for ( c = a; c < G_N_ELEMENTS ( dvb_all_n ); c++ )
        gst_bin_add ( GST_BIN ( dvbplay ), dvb_all_n[c] );

    gst_element_link_many ( dvb_all_n[0], dvb_all_n[1], NULL );

    g_signal_connect ( dvb_all_n[1], "pad-added", G_CALLBACK ( tv_pad_demux_added_audio ), dvb_all_n[9] );
    g_signal_connect ( dvb_all_n[1], "pad-added", G_CALLBACK ( tv_pad_demux_added_video ), dvb_all_n[2] );

    if ( video_enable )
    {
        gst_element_link_many ( dvb_all_n[2], dvb_all_n[3], dvb_all_n[4], NULL );
        gst_element_link_many ( dvb_all_n[5], dvb_all_n[6], dvb_all_n[7], dvb_all_n[8], NULL );

        g_signal_connect ( dvb_all_n[4], "pad-added", G_CALLBACK ( tv_pad_decode_added ), dvb_all_n[5] );
    }

    gst_element_link_many ( dvb_all_n[9],  dvb_all_n[10], dvb_all_n[11], NULL );
    gst_element_link_many ( dvb_all_n[12], dvb_all_n[13], dvb_all_n[14], dvb_all_n[15], dvb_all_n[16], NULL );

    g_signal_connect ( dvb_all_n[11], "pad-added", G_CALLBACK ( tv_pad_decode_added ), dvb_all_n[12] );
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
    { "queue2" }, { rec_en_ts ? video_encoder : video_parser },
    { "queue2" }, { rec_en_ts ? audio_encoder : audio_parser },
    { rec_en_ts ? muxer : "mpegtsmux" }, { "filesink" }
};

    for ( c = 0; c < G_N_ELEMENTS ( dvb_rec_all_n ); c++ )
    {
        dvb_rec_all_n[c] = gst_element_factory_make ( dvb_all_rec_list_n[c].name, NULL );

    	if ( !dvb_rec_all_n[c] )
    	{
            g_printerr ( "dvb rec element - %s - not all elements could be created.\n", dvb_all_rec_list_n[c].name );
            return;
    	}
    }

    if ( video_enable ) { b = 0; } else { b = 2; }

  gst_element_set_state ( dvbplay, GST_STATE_PAUSED );

    for ( c = b; c < G_N_ELEMENTS ( dvb_rec_all_n ); c++ )
        gst_bin_add ( GST_BIN (dvbplay), dvb_rec_all_n[c] );

    if ( video_enable )
        gst_element_link_many ( rec_en_ts ? dvb_all_n[6] : dvb_all_n[2], dvb_rec_all_n[0], dvb_rec_all_n[1], dvb_rec_all_n[4], NULL );

    gst_element_link_many ( rec_en_ts ? dvb_all_n[13] : dvb_all_n[9], dvb_rec_all_n[2], dvb_rec_all_n[3], dvb_rec_all_n[4], NULL );

    gst_element_link_many ( dvb_rec_all_n[4], dvb_rec_all_n[5], NULL );

    const gchar *ch_name = gtk_window_get_title ( main_window );
    gchar *file_rec = g_strdup_printf ( "%s/%s_%s.%s", rec_dir, ch_name, tv_get_time_date_str (), rec_en_ts ? file_ext : "mpeg" );
    g_object_set ( dvb_rec_all_n[5], "location", file_rec, NULL );
    g_free ( file_rec );

    for ( c = b; c < G_N_ELEMENTS ( dvb_rec_all_n ); c++ )
        gst_element_set_state ( dvb_rec_all_n[c], GST_STATE_PAUSED );

    g_usleep ( 250000 );

  gst_element_set_state ( dvbplay, GST_STATE_PLAYING );

}

static void tv_gst_rec_remove ()
{
    for ( c = b; c < G_N_ELEMENTS ( dvb_rec_all_n ); c++ )
        gst_bin_remove ( GST_BIN (dvbplay), dvb_rec_all_n[c] );

    //g_usleep ( 250000 );
}

static void tv_sensitive ( gboolean set_sensitive, guint start_s, guint end_s )
{
    gtk_widget_set_sensitive ( GTK_WIDGET ( volbutton ), set_sensitive );

    const gchar *menu_n[] = { "Record", "Stop", "Mute"/*, "EQ-Audio", "EQ-Video" */};

    for ( c = 0; c < G_N_ELEMENTS ( menu_n ); c++ )
        g_action_group_action_enabled_changed ( G_ACTION_GROUP (group), menu_n[c], set_sensitive );


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
        gtk_window_set_title  ( GTK_WINDOW ( main_window ), "Gtv-Dvb" );
        gtk_widget_queue_draw ( GTK_WIDGET ( main_window ) );
    }
}

static void tv_play ( gchar *data )
{
    if ( GST_ELEMENT_CAST ( dvbplay ) -> current_state != GST_STATE_PLAYING )
    {
        tv_checked_video ( data );
        tv_gst_tsdemux ();
        fmsg_i = TRUE;

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

    g_object_set ( G_OBJECT (dvb_all_n[15]), "volume", volume, NULL );
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
    tv_info_widget_name ( GTK_WIDGET ( button ) );

    if ( GST_ELEMENT_CAST ( dvbplay ) -> current_state == GST_STATE_PLAYING )
      g_object_set ( dvb_all_n[15], "volume", value, NULL );
}

static void tv_volume_mute ()
{
    gboolean mute;
    g_object_get ( dvb_all_n[15], "mute", &mute, NULL );
    g_object_set ( dvb_all_n[15], "mute", !mute, NULL );

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
    tv_info_widget_name ( GTK_WIDGET ( drawingare ) );

    if ( event->button == 1 && event->type == GDK_2BUTTON_PRESS ) { tv_flscr (); return TRUE; }
    if ( event->button == 2 ) { tv_mute (); return TRUE; }
    if ( event->button == 3 )
    {
        #if GTK_CHECK_VERSION(3,22,0)
            gtk_menu_popup_at_pointer ( menu, NULL );
        #else
            gtk_menu_popup ( menu, NULL, NULL, NULL, NULL, event->button, event->time );
        #endif

        return TRUE;
    }
    return FALSE;
}


const struct tv_list_media { const gchar *label; gchar *name_icon; void (* activate); const gchar *accel_key; } tv_list_media_n[] =
{
    // Toolbar media
    { N_("Record"),     "media-record",            tv_rec,   "<control>r" },
    { N_("Stop"),       "media-playback-stop",     tv_stop,  "<control>x" },
    //{ N_("EQ-Audio"),   "preferences-desktop",     tv_audio, "<control>a" },
    //{ N_("EQ-Video"),   "preferences-desktop",     tv_video, "<control>v" },
    { N_("Channels"),   "applications-multimedia", tv_plist, "<control>l" },
    { N_("Scan"),       "display",                 tv_scan,  "<control>u" },

    // Toolbar sw
    { N_("Up"),         "up",                      tv_goup,  "<control>z" },
    { N_("Down"),       "down",                    tv_down,  "<control>z" },
    { N_("Remove"),     "remove",                  tv_remv,  "<control>z" },
    { N_("Clear"),      "edit-clear",              tv_clear, "<control>z" },

    // Menu ( only )
    { N_("Mute"),       "audio-volume-muted",      tv_mute,  "<control>m" },
    { N_("Full-screen"),"view-fullscreen",         tv_flscr, "<control>f" },
    { N_("Mini"),       "view-restore",            tv_mini,  "<control>h" },
    { N_("Quit"),       "system-shutdown",         tv_quit,  "<control>q" }
};

static void tv_create_gaction_entry ( GtkApplication *app )
{
    group = g_simple_action_group_new ();

    GActionEntry entries[ G_N_ELEMENTS ( tv_list_media_n ) ];

    for ( j = 0; j < G_N_ELEMENTS ( tv_list_media_n ); j++ )
    {
        entries[j].name           = tv_list_media_n[j].label;
        entries[j].activate       = tv_list_media_n[j].activate;
        entries[j].parameter_type = NULL; // g_variant_new_string ( tv_list_media_n[j].accel_key );
        entries[j].state          = NULL;

        gchar *text = g_strconcat ( "app.", tv_list_media_n[j].label, NULL );
        const gchar *accelf[] = { tv_list_media_n[j].accel_key, NULL };
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

    gtk_toolbar_set_icon_size ( toolbar_create, GTK_ICON_SIZE_MENU ); // GTK_ICON_SIZE_MENU GTK_ICON_SIZE_LARGE_TOOLBAR

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

static GMenu * tv_create_gmenu ()
{
    GMenu *menu = g_menu_new ();
    GMenuItem *mitem;

    gint dat_n[] = { 1, 0, 2, 3, 8, 10, 9, 11 };

    for ( j = 0; j < G_N_ELEMENTS ( dat_n ); j++ )
    {
        gchar *text = g_strconcat ( "menu.", tv_list_media_n[ dat_n[j] ].label, NULL );
            mitem = g_menu_item_new ( _(tv_list_media_n[ dat_n[j] ].label), text );
        g_free ( text );

        g_menu_item_set_icon ( mitem, G_ICON ( gtk_icon_theme_load_icon ( gtk_icon_theme_get_default (),
                               tv_list_media_n[ dat_n[j] ].name_icon, 16, GTK_ICON_LOOKUP_NO_SVG, NULL ) ) );

        g_menu_item_set_attribute_value ( mitem, "accel", g_variant_new_string ( tv_list_media_n[ dat_n[j] ].accel_key ) );
        g_menu_append_item ( menu, mitem );
    }

    return menu;
}

static GtkMenu * tv_create_menu ()
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

static GtkScrolledWindow * tv_scroll_win ( GtkTreeView *tree_view, gchar *title, gchar *data )
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


static void tv_add_channels ( const gchar *name_ch, gchar *data )
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

static void tv_str_split_ch_data ( gchar *data )
{
    gchar **lines;
    lines = g_strsplit ( data, ":", 0 );

        if ( !g_str_has_prefix ( data, "#" ) )
            tv_add_channels ( lines[0], data );

    g_strfreev ( lines );
}

static void tv_read_file_ch_to_treeview ( const gchar *filename )
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
    GString *gstring = g_string_new ( "# Gtv-Dvbf channel format \n" );

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
        g_printerr ( "Save failed: file %s. \n", filename );

    g_string_free ( gstring, TRUE );
}

static void tv_goup  () { tv_treeview_up_down ( tv_treeview, TRUE  ); }
static void tv_down  () { tv_treeview_up_down ( tv_treeview, FALSE ); }
static void tv_clear () { tv_treeview_clear   ( tv_treeview ); }
static void tv_remv  () { tv_treeview_remove  ( tv_treeview ); }

static gchar * tv_rec_dir ()
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

static gchar * tv_openf ()
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

static void tv_init ( GtkApplication *app )
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
    muxer =         g_strdup ( "oggmux" );
    file_ext =      g_strdup ( "ogg" );

    const gchar *th_name = "Dark-media";
    gchar *th_home = g_strconcat ( g_get_home_dir (), "/.themes/", th_name, NULL );
    gchar *th_root = g_strconcat ( "/usr/share/themes/", th_name, NULL );

    if ( g_file_test ( th_home, G_FILE_TEST_EXISTS ) || g_file_test ( th_root, G_FILE_TEST_EXISTS ) )
        g_object_set ( gtk_settings_get_default (), "gtk-theme-name", th_name, NULL );

    g_free ( th_home );
    g_free ( th_root );

    const gchar *ic_name = "Light-media";
    gchar *ic_home = g_strconcat ( g_get_home_dir (), "/.icons/", ic_name, NULL );
    gchar *ic_root = g_strconcat ( "/usr/share/icons/", ic_name, NULL );

    if ( g_file_test ( ic_home, G_FILE_TEST_EXISTS ) || g_file_test ( ic_root, G_FILE_TEST_EXISTS ) )
        g_object_set ( gtk_settings_get_default (), "gtk-icon-theme-name", ic_name, NULL );

    g_free ( ic_home );
    g_free ( ic_root );

    tv_logo = gtk_icon_theme_load_icon ( gtk_icon_theme_get_default (),
              "applications-multimedia", 64, GTK_ICON_LOOKUP_USE_BUILTIN, NULL );

    tv_create_gaction_entry ( app );
}

static void tv_read_ch ()
{
    if ( g_file_test ( channels_conf, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_REGULAR ) )
        tv_read_file_ch_to_treeview ( channels_conf );
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
    tv_init ( app );

    GtkBox *main_box, *main_hbox, *tool_hbox_sw;
    GtkListStore *liststore;
    GtkScrolledWindow *scrollwin;

    main_window = (GtkWindow *)gtk_application_window_new ( app );
    gtk_window_set_title ( main_window, "Gtv-Dvb" );
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
    label_tv_rec = (GtkLabel *)gtk_label_new ( "" );

    GtkImage *icon_widget = ( GtkImage * )gtk_image_new_from_icon_name ( "audio-volume-muted", GTK_ICON_SIZE_MENU );
    gtk_image_set_pixel_size ( icon_widget, 16 );
    GtkToolItem *button_mute = gtk_tool_button_new ( GTK_WIDGET ( icon_widget ), NULL );
    g_signal_connect ( button_mute, "clicked", G_CALLBACK ( tv_mute ), NULL );

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

    tv_read_ch ();
}

static void activate ( GtkApplication *app )
{
    tv_win_base ( app );
}

static void set_locale ()
{
    setlocale ( LC_ALL, "" );
	/* "/usr/share/locale/" */
    bindtextdomain ( "gtv-dvb", "/usr/share/locale" );
    textdomain ( "gtv-dvb" );
}

int main ()
{
    gst_init ( NULL, NULL );

    if ( !tv_gst_create () ) return -1;

    set_locale ();

    GtkApplication *app = gtk_application_new ( NULL, G_APPLICATION_FLAGS_NONE );
    g_signal_connect ( app, "activate", G_CALLBACK ( activate ),  NULL );

    int status = g_application_run ( G_APPLICATION ( app ), 0, NULL );
    g_object_unref ( app );

    return status;
}




// Scan & convert

gint DVB_DELSYS = SYS_UNDEFINED;
const gchar *dvb_type_str = "UNDEFINED";

//static guint j = 0, c = 0;
guint adapter_ct = 0, frontend_ct = 0, lnb_type = 0;
gchar *pol = "H";
gboolean msg_info = FALSE, w_scan_info = FALSE, fmsg_is = TRUE;

time_t t_start, t_cur;

static GstElement *dvb_scan, *dvbsrc_tune;
static GtkLabel *scan_snr_dvbt, *scan_snr_dvbs, *scan_snr_dvbc;

static void tv_scan_set_all_ch ( gint all_ch, gint c_tv, gint c_ro );
static void tv_scan_stop ( GtkButton *button, gpointer data );


// Mini GST scanner

static void dvb_mpegts_initialize ()
{
    gst_mpegts_initialize ();
    g_type_class_ref ( GST_TYPE_MPEGTS_STREAM_TYPE          );
    g_type_class_ref ( GST_TYPE_MPEGTS_DVB_SERVICE_TYPE     );
    g_type_class_ref ( GST_TYPE_MPEGTS_DESCRIPTOR_TYPE      );
    g_type_class_ref ( GST_TYPE_MPEGTS_DVB_DESCRIPTOR_TYPE  );
    g_type_class_ref ( GST_TYPE_MPEGTS_ISO639_AUDIO_TYPE    );
    g_type_class_ref ( GST_TYPE_MPEGTS_RUNNING_STATUS       );
}

const gchar * enum_name ( GType instance_type, gint val )
{
    GEnumValue *env = g_enum_get_value ( G_ENUM_CLASS ( g_type_class_peek ( instance_type ) ), val );

    if ( !env )
        return "UNKNOWN";

    return env->value_nick;
}

#define MAX_RUN_PAT 128

static struct dvb_gst_scan_sdt { guint pmn_pid; gchar *name;             } dvb_gst_scan_sdt_n[MAX_RUN_PAT];
static struct dvb_gst_scan_pat { guint pmn_pid; guint  nmap;             } dvb_gst_scan_pat_n[MAX_RUN_PAT];
static struct dvb_gst_scan_pmt { guint pmn_pid; guint  vpid; guint apid; } dvb_gst_scan_pmt_n[MAX_RUN_PAT];

static gint pat_count = 0, pmt_count = 0, sdt_count = 0;
static gboolean pat_done = FALSE, pmt_done = FALSE, sdt_done = FALSE;

static void start_clear_dvb_gst_scan ()
{
    fmsg_is = TRUE;

    sdt_done = FALSE;
    pat_done = FALSE;
    pmt_done = FALSE;

    pat_count = 0;
    pmt_count = 0;
    sdt_count = 0;

    for ( j = 0; j < MAX_RUN_PAT; j++ )
    {
        dvb_gst_scan_pmt_n[j].pmn_pid = 0;
        dvb_gst_scan_pmt_n[j].apid = 0;
        dvb_gst_scan_pmt_n[j].vpid = 0;

        dvb_gst_scan_pat_n[j].pmn_pid = 0;
        dvb_gst_scan_pat_n[j].nmap = 0;

        dvb_gst_scan_sdt_n[j].pmn_pid = 0;
        dvb_gst_scan_sdt_n[j].name = NULL;
    }
}

static void dump_pat ( GstMpegtsSection *section )
{
    GPtrArray *pat = gst_mpegts_section_get_pat ( section );
    g_print ( "\nPAT: %d Programs \n", pat->len );

    guint i = 0;
    for ( i = 0; i < pat->len; i++ )
    {
        if ( i >= MAX_RUN_PAT ) break;

        GstMpegtsPatProgram *patp = g_ptr_array_index ( pat, i );

        if ( patp->program_number == 0 ) continue;

        dvb_gst_scan_pat_n[pat_count].pmn_pid = patp->program_number;
        dvb_gst_scan_pat_n[pat_count].nmap    = patp->network_or_program_map_PID;
        pat_count++;

        g_print ( "     Program number: %6d (0x%04x) | network or pg-map pid: 0x%04x \n",
            patp->program_number, patp->program_number, patp->network_or_program_map_PID );
    }

    g_ptr_array_unref ( pat );

    pat_done = TRUE;
    ( i >= MAX_RUN_PAT ) ? g_print ( "MAX %d: PAT scan stop \n", MAX_RUN_PAT ) : g_print ( "PAT Done  \n\n" );
}

static void dump_pmt ( GstMpegtsSection *section )
{
    if ( pmt_count >= MAX_RUN_PAT )
    {
        g_print ( "MAX %d: PMT scan stop \n", MAX_RUN_PAT );
        return;
    }

    guint i = 0, len = 0;
    gboolean first_audio = TRUE;

    const GstMpegtsPMT *pmt = gst_mpegts_section_get_pmt ( section );
    len = pmt->streams->len;
    dvb_gst_scan_pmt_n[pmt_count].pmn_pid = pmt->program_number;

    g_print ( "\nPMT: %d  ( %d ) \n", pmt_count+1, len );

    g_print ( "     Program number     : %d (0x%04x) \n", pmt->program_number, pmt->program_number );
    g_print ( "     Pcr pid            : %d (0x%04x) \n", pmt->pcr_pid, pmt->pcr_pid );
    g_print ( "     %d Streams: \n", len );

    for ( i = 0; i < len; i++ )
    {
        GstMpegtsPMTStream *stream = g_ptr_array_index (pmt->streams, i);

        g_print ( "       pid: %d (0x%04x) , stream_type:0x%02x (%s) \n", stream->pid, stream->pid, stream->stream_type,
            enum_name (GST_TYPE_MPEGTS_STREAM_TYPE, stream->stream_type) );

        const gchar *name_t = enum_name ( GST_TYPE_MPEGTS_STREAM_TYPE, stream->stream_type );

        if ( g_strrstr ( name_t, "video" ) )
            dvb_gst_scan_pmt_n[pmt_count].vpid = stream->pid;

        if ( g_strrstr ( name_t, "audio" ) )
        {
            if ( first_audio ) dvb_gst_scan_pmt_n[pmt_count].apid = stream->pid;

            first_audio = FALSE;
        }
    }
    pmt_count++;

    if ( pat_count > 0 )
    if ( pmt_count == pat_count )
    {
        pmt_done = TRUE;
        g_print ( "\nPMT Done \n\n" );
    }
}

static void dump_sdt ( GstMpegtsSection *section )
{
    if ( sdt_count >= MAX_RUN_PAT )
    {
        g_print ( "MAX %d: SDT scan stop \n", MAX_RUN_PAT );
        return;
    }

    guint i = 0, len = 0;
    gint  z = 0;

    const GstMpegtsSDT *sdt = gst_mpegts_section_get_sdt ( section );

    len = sdt->services->len;
    g_print ( "Services: %d  ( %d ) \n", sdt_count+1, len );

    for ( i = 0; i < len; i++ )
    {
        GstMpegtsSDTService *service = g_ptr_array_index ( sdt->services, i );

        dvb_gst_scan_sdt_n[sdt_count].name = NULL;
        dvb_gst_scan_sdt_n[sdt_count].pmn_pid = service->service_id;

        gboolean get_descr = FALSE;

        if ( pat_done )
        {
            for ( z = 0; z < pat_count; z++ )
                if ( dvb_gst_scan_pat_n[z].pmn_pid == service->service_id )
                    {  get_descr = TRUE; break; }
        }

        if ( !get_descr ) continue;

        g_print ( "     Service id: %d  %d 0x%04x \n", sdt_count+1, service->service_id, service->service_id );

        GPtrArray *descriptors = service->descriptors;
        for ( c = 0; c < descriptors->len; c++ )
        {
            GstMpegtsDescriptor *desc = g_ptr_array_index ( descriptors, c );

            gchar *service_name, *provider_name;
            GstMpegtsDVBServiceType service_type;

            if ( desc->tag == GST_MTS_DESC_DVB_SERVICE )
            {
                if ( gst_mpegts_descriptor_parse_dvb_service ( desc, &service_type, &service_name, &provider_name ) )
                {
                    dvb_gst_scan_sdt_n[sdt_count].name = g_strdup ( service_name );

                    g_print ( "   Service Descriptor, type:0x%02x (%s) \n",
                        service_type, enum_name (GST_TYPE_MPEGTS_DVB_SERVICE_TYPE, service_type) );
                    g_print ( "      Service  (name) : %s \n", service_name  );
                    g_print ( "      Provider (name) : %s \n", provider_name );

                    g_free ( service_name  );
                    g_free ( provider_name );
                }
            }
        }

        if ( dvb_gst_scan_sdt_n[sdt_count].name == NULL )
            dvb_gst_scan_sdt_n[sdt_count].name = g_strdup_printf ( "PGMN-%d", service->service_id );

        sdt_count++;
    }

    if ( pat_count > 0 && pmt_count > 0 )
    if ( sdt_count == pat_count || sdt_count == pmt_count )
    {
        sdt_done = TRUE;
        g_print ( "SDT Done \n\n" );
    }
}

static void parse_gst_mpegts_section ( GstMessage *message )
{
    GstMpegtsSection *section = gst_message_parse_mpegts_section ( message );
    if ( section )
    {
        switch ( GST_MPEGTS_SECTION_TYPE ( section ) )
        {
            case GST_MPEGTS_SECTION_PAT:
                dump_pat (section);
                break;
            case GST_MPEGTS_SECTION_PMT:
                dump_pmt (section);
                break;
            case GST_MPEGTS_SECTION_SDT:
                dump_sdt (section);
                break;

            default:
                break;
        }
        gst_mpegts_section_unref ( section );
    }

    if ( pat_done && pmt_done && sdt_done )
      tv_scan_stop ( NULL, NULL );

    time ( &t_cur );
    if ( ( t_cur - t_start ) >= 10 )
    {
        g_print ( "Warning. Time stop %ld (sec) \n", t_cur - t_start );
        tv_scan_stop ( NULL, NULL );
    }
}

static void scan_msg_all ( GstBus *bus, GstMessage *message )
{
    const GstStructure *structure = gst_message_get_structure ( message );

    if ( structure )
    {
        gint signal, snr;
        gboolean hlook = FALSE;

        if (  gst_structure_get_int ( structure, "signal", &signal )  )
        {
            gst_structure_get_boolean ( structure, "lock", &hlook );
            gst_structure_get_int ( structure, "snr", &snr);

            tv_set_sgn_snr ( dvb_scan, scan_snr_dvbt, (signal * 100) / 0xffff, (snr * 100) / 0xffff, hlook );
            tv_set_sgn_snr ( dvb_scan, scan_snr_dvbs, (signal * 100) / 0xffff, (snr * 100) / 0xffff, hlook );
            tv_set_sgn_snr ( dvb_scan, scan_snr_dvbc, (signal * 100) / 0xffff, (snr * 100) / 0xffff, hlook );
        }
        tv_info_object_name ( GST_OBJECT ( bus ) );
    }
    parse_gst_mpegts_section ( message );
}

static void scan_msg_err ( GstBus *bus, GstMessage *msg )
{
    GError *err = NULL;
    gchar *dbg = NULL;

    gst_message_parse_error ( msg, &err, &dbg );
    g_printerr ( "ERROR: %s (%s)\n", err->message, (dbg) ? dbg : "no details" );
    tv_info_object_name ( GST_OBJECT ( bus ) );

    if ( fmsg_is )
        tv_message_dialog ( err->message, (dbg) ? dbg : " ", GTK_MESSAGE_ERROR );

    fmsg_is = FALSE;

    g_error_free ( err );
    g_free ( dbg );

    tv_scan_stop ( NULL, NULL );
}

static void scan_set_tune ()
{
    tv_set_tuning_timeout ( dvbsrc_tune );
}

static void scan_gst_create ()
{
    GstElement *dvbsrc_parse, *dvbsrc_files;

    dvb_scan     = gst_pipeline_new ( "pipeline_scan" );
    dvbsrc_tune  = gst_element_factory_make ( "dvbsrc",   NULL );
    dvbsrc_parse = gst_element_factory_make ( "tsparse",  NULL );
    dvbsrc_files = gst_element_factory_make ( "filesink", NULL );

    if ( !dvb_scan || !dvbsrc_tune || !dvbsrc_parse || !dvbsrc_files )
        g_printerr ("dvb_scan - not be created.\n");

    gst_bin_add_many ( GST_BIN ( dvb_scan ), dvbsrc_tune, dvbsrc_parse, dvbsrc_files, NULL );
    gst_element_link_many ( dvbsrc_tune, dvbsrc_parse, dvbsrc_files, NULL );

    g_object_set ( dvbsrc_files, "location", "/dev/null", NULL);

    GstBus *bus_scan = gst_element_get_bus ( dvb_scan );
    gst_bus_add_signal_watch ( bus_scan );
    gst_object_unref ( bus_scan );

    g_signal_connect ( bus_scan, "message",        G_CALLBACK ( scan_msg_all ), NULL );
    g_signal_connect ( bus_scan, "message::error", G_CALLBACK ( scan_msg_err ), NULL );

    scan_set_tune ();
}

// End Mini GST scanner

static GtkNotebook *notebook;
static GtkLabel *all_channels;
static GtkTreeView *scan_treeview;
static GtkEntry *entry_enc[4];
static void tv_scan_add_to_treeview ( gchar *name_ch, gchar *data );

const struct labels_scan { gchar *name; } labels_scan_n[] =
{
    { "General" }, { "Scan / Convert" }, { "DVB-T/T2" }, { "DVB-S/S2" }, { "DVB-C" }, { "Channels" }
};

const gchar *dvbt[] =
{
    "Frequency  MHz",
    "Bandwidth",
    "Inversion",
    "Code Rate HP",
    "Code Rate LP",
    "Modulation",
    "Transmission",
    "Guard interval",
    "Hierarchy",
    "Stream ID"
};

const gchar *dvbs[] =
{
    "DiSEqC",
    "Frequency  MHz",
    "Symbol rate  kBd",
    "Polarity",
    "FEC",
    "Modulation",
    "Pilot",
    "Rolloff",
    "Stream ID",
    "LNB"
};

const gchar *dvbc[] =
{
    "Frequency  MHz",
    "Symbol rate  kBd",
    "Inversion",
    "Code Rate HP",
    "Modulation"
};

// 1 digits; 2 enum descriptors

const gint dvbet[] = { 1, 1, 2, 2, 2, 2, 2, 2, 2, 1 };
const gint dvbes[] = { 2, 1, 1, 2, 2, 2, 2, 2, 1, 2 };
const gint dvbec[] = { 1, 1, 2, 2, 2 };


const struct dvb_descr_all { gint descr_num; const gchar *dvb_v5_name; const gchar *text_vis; } dvb_descr_all_n;

const struct dvb_descr_all dvb_descr_inversion_type_n[] =
{
    { INVERSION_OFF,  "OFF",  "Off"  },
    { INVERSION_ON,   "ON",   "On"   },
    { INVERSION_AUTO, "AUTO", "Auto" }
};
const struct dvb_descr_all dvb_descr_coderate_type_n[] =
{
    { FEC_NONE, "NONE", "None" },
    { FEC_1_2,  "1/2",  "1/2"  },
    { FEC_2_3,  "2/3",  "2/3"  },
    { FEC_3_4,  "3/4",  "3/4"  },
    { FEC_4_5,  "4/5",  "4/5"  },
    { FEC_5_6,  "5/6",  "5/6"  },
    { FEC_6_7,  "6/7",  "6/7"  },
    { FEC_7_8,  "7/8",  "7/8"  },
    { FEC_8_9,  "8/9",  "8/9"  },
    { FEC_AUTO, "AUTO", "Auto" },
    { FEC_3_5,  "3/5",  "3/5"  },
    { FEC_9_10, "9/10", "9/10" },
    { FEC_2_5,  "2/5",  "2/5"  }
};
const struct dvb_descr_all dvb_descr_modulation_type_n[] =
{
    { QPSK,     "QPSK",     "QPSK"     },
    { QAM_16,   "QAM/16",   "QAM 16"   },
    { QAM_32,   "QAM/32",   "QAM 32"   },
    { QAM_64,   "QAM/64",   "QAM 64"   },
    { QAM_128,  "QAM/128",  "QAM 128"  },
    { QAM_256,  "QAM/256",  "QAM 256"  },
    { QAM_AUTO, "QAM/AUTO", "Auto"     },
    { VSB_8,    "VSB/8",    "VSB 8"    },
    { VSB_16,   "VSB/16",   "VSB 16"   },
    { PSK_8,    "PSK/8",    "PSK 8"    },
    { APSK_16,  "APSK/16",  "APSK 16"  },
    { APSK_32,  "APSK/32",  "APSK 32"  },
    { DQPSK,    "DQPSK",    "DQPSK"    },
    { QAM_4_NR, "QAM/4_NR", "QAM 4 NR" }
};
const struct dvb_descr_all dvb_descr_transmode_type_n[] =
{
    { TRANSMISSION_MODE_2K,   "2K",   "2K"   },
    { TRANSMISSION_MODE_8K,   "8K",   "8K"   },
    { TRANSMISSION_MODE_AUTO, "AUTO", "Auto" },
    { TRANSMISSION_MODE_4K,   "4K",   "4K"   },
    { TRANSMISSION_MODE_1K,   "1K",   "1K"   },
    { TRANSMISSION_MODE_16K,  "16K",  "16K"  },
    { TRANSMISSION_MODE_32K,  "32K",  "32K"  },
    { TRANSMISSION_MODE_C1,   "C1",   "C1"   },
    { TRANSMISSION_MODE_C3780,"C3780","C3780"}
};
const struct dvb_descr_all dvb_descr_guard_type_n[] =
{
    { GUARD_INTERVAL_1_32,   "1/32",   "32"     },
    { GUARD_INTERVAL_1_16,   "1/16",   "16"     },
    { GUARD_INTERVAL_1_8,    "1/8",    "8"      },
    { GUARD_INTERVAL_1_4,    "1/4",    "4"      },
    { GUARD_INTERVAL_AUTO,   "AUTO",   "Auto"   },
    { GUARD_INTERVAL_1_128,  "1/128",  "128"    },
    { GUARD_INTERVAL_19_128, "19/128", "19/128" },
    { GUARD_INTERVAL_19_256, "19/256", "19/256" },
    { GUARD_INTERVAL_PN420,  "PN420",  "PN 420" },
    { GUARD_INTERVAL_PN595,  "PN595",  "PN 595" },
    { GUARD_INTERVAL_PN945,  "PN945",  "PN 945" }
};
const struct dvb_descr_all dvb_descr_hierarchy_type_n[] =
{
    { HIERARCHY_NONE, "NONE", "None" },
    { HIERARCHY_1,    "1",    "1"    },
    { HIERARCHY_2,    "2",    "2"    },
    { HIERARCHY_4,    "4",    "4"    },
    { HIERARCHY_AUTO, "AUTO", "Auto" }

};
const struct dvb_descr_all dvb_descr_pilot_type_n[] =
{
    { PILOT_ON,   "ON",   "On"   },
    { PILOT_OFF,  "OFF",  "Off"  },
    { PILOT_AUTO, "AUTO", "Auto" }
};
const struct dvb_descr_all dvb_descr_roll_type_n[] =
{
    { ROLLOFF_35,   "35",   "35"   },
    { ROLLOFF_20,   "20",   "20"   },
    { ROLLOFF_25,   "25",   "25"   },
    { ROLLOFF_AUTO, "AUTO", "Auto" }
};
const struct dvb_descr_all dvb_descr_polarity_type_n[] =
{
    { SEC_VOLTAGE_18,  "HORIZONTAL", "H  18V" },
    { SEC_VOLTAGE_13,  "VERTICAL",   "V  13V" },
    { SEC_VOLTAGE_18,  "LEFT",       "L  18V" },
    { SEC_VOLTAGE_13,  "RIGHT",      "R  13V" },
    { SEC_VOLTAGE_OFF, "OFF",        "Off"    }
};

const struct dvb_descr_all dvb_descr_lnb_type_n[] =
{
    { 0, "UNIVERSAL", "Universal" },
    { 1, "DBS",	      "DBS"       },
    { 2, "EXTENDED",  "Extended"  },
    { 3, "STANDARD",  "Standard"  },
    { 4, "ENHANCED",  "Enhanced"  },
    { 5, "C-BAND",	  "C-Band"    },
    { 6, "C-MULT",	  "C-Mult"    },
    { 7, "DISHPRO",	  "Dishpro"   },
    { 8, "110BS",	  "110 BS"    },
};

struct lnb_types_lhs { gchar	*name; guint low_val; guint high_val; guint switch_val; } lnb_n[] =
{
	{ "UNIVERSAL",  9750000,  10600000, 11700000 },
 	{ "DBS",		11250000, 0, 0               },
 	{ "EXTENDED",   9750000,  10600000, 11700000 },
	{ "STANDARD",	10000000, 0, 0               },
	{ "ENHANCED",	9750000,  0, 0               },
	{ "C-BAND",		5150000,  0, 0               },
	{ "C-MULT",		5150000,  5750000,  0        },
	{ "DISHPRO",	11250000, 14350000, 0        },
	{ "110BS",		10678000, 0, 0               }
};
static void tv_set_lnb ( GstElement *element, gint num_lnb )
{
    g_object_set ( element, "lnb-lof1", lnb_n[num_lnb].low_val,    NULL );
    g_object_set ( element, "lnb-lof2", lnb_n[num_lnb].high_val,   NULL );
    g_object_set ( element, "lnb-slof", lnb_n[num_lnb].switch_val, NULL );
}

struct gst_param_dvb_descr_all { gchar *name; const gchar *dvb_v5_name; gchar *gst_param; const struct dvb_descr_all *dvb_descr; guint cdsc; } gst_param_dvb_descr_n[] =
{
    // descr
    { "Inversion",      "INVERSION",         "inversion",        dvb_descr_inversion_type_n,  G_N_ELEMENTS (dvb_descr_inversion_type_n)  },
    { "Code Rate HP",   "CODE_RATE_HP",      "code-rate-hp",     dvb_descr_coderate_type_n,   G_N_ELEMENTS (dvb_descr_coderate_type_n)   },
    { "Code Rate LP",   "CODE_RATE_LP",      "code-rate-lp",     dvb_descr_coderate_type_n,   G_N_ELEMENTS (dvb_descr_coderate_type_n)   },
    { "FEC",            "INNER_FEC",         "code-rate-hp",     dvb_descr_coderate_type_n,   G_N_ELEMENTS (dvb_descr_coderate_type_n)   },
    { "Modulation",     "MODULATION",        "modulation",       dvb_descr_modulation_type_n, G_N_ELEMENTS (dvb_descr_modulation_type_n) },
    { "Transmission",   "TRANSMISSION_MODE", "trans-mode",       dvb_descr_transmode_type_n,  G_N_ELEMENTS (dvb_descr_transmode_type_n)  },
    { "Guard interval", "GUARD_INTERVAL",    "guard",            dvb_descr_guard_type_n,      G_N_ELEMENTS (dvb_descr_guard_type_n)      },
    { "Hierarchy",      "HIERARCHY",         "hierarchy",        dvb_descr_hierarchy_type_n,  G_N_ELEMENTS (dvb_descr_hierarchy_type_n)  },
    { "Pilot",          "PILOT",             "pilot",            dvb_descr_pilot_type_n,      G_N_ELEMENTS (dvb_descr_pilot_type_n)      },
    { "Rolloff",        "ROLLOFF",           "rolloff",          dvb_descr_roll_type_n,       G_N_ELEMENTS (dvb_descr_roll_type_n)       },
    { "Polarity",       "POLARIZATION",      "polarity",         dvb_descr_polarity_type_n,   G_N_ELEMENTS (dvb_descr_polarity_type_n)   },
    { "LNB",            "LNB",               "lnb-type",         dvb_descr_lnb_type_n,        G_N_ELEMENTS (dvb_descr_lnb_type_n)        },

    // digits
    { "Frequency",      "FREQUENCY",         "frequency",        NULL, 0 },
    { "Bandwidth",      "BANDWIDTH_HZ",      "bandwidth-hz",     NULL, 0 },
    { "Symbol rate",    "SYMBOL_RATE",       "symbol-rate",      NULL, 0 },
    { "Stream ID",      "STREAM_ID",         "stream-id",        NULL, 0 },
    { "DiSEqC",         "SAT_NUMBER",        "diseqc-source",    NULL, 0 },
    { "Service Id",     "SERVICE_ID",        "program-number",   NULL, 0 },
    { "Audio Pid",      "AUDIO_PID",         "audio-pid",        NULL, 0 },
    { "Video Pid",      "VIDEO_PID",         "video-pid",        NULL, 0 }
};

static gchar * tv_strip_ch_name ( gchar *name )
{
    guint i = 0;
    for ( i = 0; name[i] != '\0'; i++ )
    {
        if ( g_ascii_isprint ( name[i] ) )
        {
            if ( name[i] == ':' || name[i] == '[' || name[i] == ']' ) name[i] = ' ';
        }
        else
            name[i] = ' ';
    }
    return g_strstrip ( name );
}

static void tv_convert_dvb5 ( const gchar *filename )
{
    guint n = 0, z = 0, x = 0;
    gchar *contents;
    GError *err = NULL;

    GString *gstring = g_string_new ( "# Convert DVB 5 format" );

    if ( g_file_get_contents ( filename, &contents, 0, &err ) )
    {
        gchar **lines = g_strsplit ( contents, "\n", 0 );

        for ( n = 0; lines[n] != NULL; n++ )
        {
            if ( g_str_has_prefix ( lines[n], "[" ) )
            {
                gstring = g_string_append ( gstring, "\n" );
                g_string_append_printf ( gstring, "%s", tv_strip_ch_name ( lines[n] ) );
                g_string_append_printf ( gstring, ":adapter=%d:frontend=%d", adapter_ct, frontend_ct );
            }

            for ( z = 0; z < G_N_ELEMENTS ( gst_param_dvb_descr_n ); z++ )
            {
                if ( g_strrstr ( lines[n], gst_param_dvb_descr_n[z].dvb_v5_name ) )
                {
                    gchar **value_key = g_strsplit ( lines[n], " = ", 0 );

                    if ( gst_param_dvb_descr_n[z].cdsc == 0 )
                    {
                        guint data = atoi ( value_key[1] );
                        if ( g_strrstr ( value_key[0], "SYMBOL_RATE" ) && data > 100000 ) data /= 1000;
                        g_string_append_printf ( gstring, ":%s=%d", gst_param_dvb_descr_n[z].gst_param, data );
                    }
                    else
                    {
                        for ( x = 0; x < gst_param_dvb_descr_n[z].cdsc; x++ )
                        {
                            if ( g_strrstr ( value_key[1], gst_param_dvb_descr_n[z].dvb_descr[x].dvb_v5_name ) )
                            {
                                if ( g_strrstr ( value_key[0], "POLARIZATION" ) )
                                {
                                    g_string_append_printf ( gstring, ":%s=%s",
                                        gst_param_dvb_descr_n[z].gst_param,
                                        gst_param_dvb_descr_n[z].dvb_descr[x].descr_num ? "H" : "V" );
                                }
                                else
                                {
                                    g_string_append_printf ( gstring, ":%s=%d",
                                        gst_param_dvb_descr_n[z].gst_param,
                                        gst_param_dvb_descr_n[z].dvb_descr[x].descr_num );
                                }
                            }
                        }
                    }
                }
            }
        }

        g_strfreev ( lines );
        g_free ( contents );
    }
    else
    {
        g_critical ( "ERROR: %s\n", err->message );
        if ( err ) g_error_free ( err );
        g_string_free ( gstring, FALSE );
        return;
    }

    if ( msg_info ) g_print ( "All data: %s", gstring->str );

    // tv_treeview_clear ( tv_treeview );

        gchar **lines_all = g_strsplit ( gstring->str, "\n", 0 );

            for ( n = 0; lines_all[n] != NULL; n++ )
			{
				if ( g_strrstr ( lines_all[n], "audio-pid" ) || g_strrstr ( lines_all[n], "video-pid" ) )
					tv_str_split_ch_data ( lines_all[n] );
			}

        g_strfreev ( lines_all );

    g_string_free ( gstring, TRUE );
}


static glong tv_set_label_freq_ext ( GtkLabel *label_set, glong num )
{
    gint numpage = gtk_notebook_get_current_page ( notebook );

    GtkWidget *page = gtk_notebook_get_nth_page ( notebook, numpage );
    GtkLabel *label = (GtkLabel *)gtk_notebook_get_tab_label ( notebook, page );
    const gchar *name_tab = gtk_label_get_text ( label );

    if ( g_strrstr ( name_tab, "DVB-S" ) )
    {
        if ( num < 100000 )
        {
            num *= 1000;
            gtk_label_set_text ( label_set, " Frequency  MHz " );
        }
        else
            gtk_label_set_text ( label_set, " Frequency  KHz " );
    }
    else
    {
        if ( num < 1000 )
        {
            num *= 1000000;
            gtk_label_set_text ( label_set, " Frequency  MHz " );
        }
        else if ( num < 1000000 )
        {
            num *= 1000;
            gtk_label_set_text ( label_set, " Frequency  KHz " );
        }
    }

    if ( msg_info ) g_print ( "numpage = %d | %s\n", numpage, name_tab );

    return num;
}

static void scan_changed_spin_all ( GtkSpinButton *button, GtkLabel *label )
{
    gtk_spin_button_update ( button );

    glong num = gtk_spin_button_get_value  ( button );
    const gchar *name = gtk_label_get_text ( label  );

    if ( g_strrstr ( name, "Frequency" ) ) num = tv_set_label_freq_ext ( label, num );

    guint c = 0;
    for ( c = 0; c < G_N_ELEMENTS ( gst_param_dvb_descr_n ); c++ )
        if ( g_strrstr ( name, gst_param_dvb_descr_n[c].name ) )
        {
            g_object_set ( dvbsrc_tune, gst_param_dvb_descr_n[c].gst_param, num, NULL );

            if ( msg_info )
		g_print ( "name = %s | num = %ld | gst_param = %s \n", gtk_label_get_text ( label ),
			  num, gst_param_dvb_descr_n[c].gst_param );
	}
}
static void scan_changed_combo_all ( GtkComboBox *combo_box, GtkLabel *label )
{
    guint num = gtk_combo_box_get_active ( combo_box );
    const gchar *name = gtk_label_get_text ( label );

    if ( g_strrstr ( name, "LNB" ) )
    {
        lnb_type = num;
        tv_set_lnb ( dvbsrc_tune, num );
        if ( msg_info ) g_print ( "name %s | set %s: %d \n", name, lnb_n[num].name, num );
        return;
    }
    if ( g_strrstr ( name, "DiSEqC" ) )
    {
        g_object_set ( dvbsrc_tune, "diseqc-source", num-1, NULL );
        if ( msg_info ) g_print ( "name = %s | set = %d | gst_param = diseqc-source \n", name, num-1 );
        return;
    }

    guint c = 0;
    for ( c = 0; c < G_N_ELEMENTS ( gst_param_dvb_descr_n ); c++ )
        if ( g_strrstr ( name, gst_param_dvb_descr_n[c].name ) )
        {
            if ( g_strrstr ( name, "Polarity" ) )
            {
                pol = gst_param_dvb_descr_n[c].dvb_descr[num].descr_num ? "H" : "V";
                g_object_set ( dvbsrc_tune, gst_param_dvb_descr_n[c].gst_param, pol, NULL );
            }
            else
                g_object_set ( dvbsrc_tune, gst_param_dvb_descr_n[c].gst_param,
                    gst_param_dvb_descr_n[c].dvb_descr[num].descr_num, NULL );

            if ( msg_info ) g_print ( "name = %s | num = %d | gst_param = %s | descr_text_vis = %s | descr_num = %d \n",
                name, num, gst_param_dvb_descr_n[c].gst_param,
                gst_param_dvb_descr_n[c].dvb_descr[num].text_vis,
                gst_param_dvb_descr_n[c].dvb_descr[num].descr_num );
        }
}


static void tv_scan_get_tp_data ( GString *gstring )
{
    guint c = 0, d = 0;
    gint  d_data = 0, DVBTYPE = 0;

    g_object_get ( dvbsrc_tune, "delsys", &DVBTYPE, NULL );
    if ( msg_info ) g_print ( "delsys: %d | %d \n", DVBTYPE, SYS_DVBS );
    //g_string_append_printf ( gstring, ":delsys=%d", DVBTYPE );

    if ( DVBTYPE == SYS_UNDEFINED )
    {
        if ( DVB_DELSYS == SYS_UNDEFINED )
        {
            if ( g_strrstr ( dvb_type_str, "DVB-T" ) ) DVBTYPE = SYS_DVBT2;
            if ( g_strrstr ( dvb_type_str, "DVB-S" ) ) DVBTYPE = SYS_DVBS2;
            if ( g_strrstr ( dvb_type_str, "DVB-C" ) ) DVBTYPE = SYS_DVBC_ANNEX_A;
        }
        else
            DVBTYPE = DVB_DELSYS;
    }

    const gchar *dvb_f[] = { "adapter", "frontend" };

    for ( d = 0; d < G_N_ELEMENTS ( dvb_f ); d++ )
    {
        g_object_get ( dvbsrc_tune, dvb_f[d], &d_data, NULL );
        g_string_append_printf ( gstring, ":%s=%d", dvb_f[d], d_data );
    }

    if ( DVBTYPE == SYS_DVBT || DVBTYPE == SYS_DVBT2 || DVBTYPE == SYS_DTMB /* || SYS_DVBT2 */ )
    {
        for ( c = 0; c < G_N_ELEMENTS ( dvbt ); c++ )
        {
            if ( DVBTYPE == SYS_DVBT )
                if ( g_strrstr ( dvbt[c], "Stream ID" ) ) continue;

            if ( DVBTYPE == SYS_DTMB )
                if ( g_strrstr ( dvbt[c], "Code Rate LP" ) || g_strrstr ( dvbt[c], "Hierarchy" ) || g_strrstr ( dvbt[c], "Stream ID" ) ) continue;


            for ( d = 0; d < G_N_ELEMENTS ( gst_param_dvb_descr_n ); d++ )
            {
                if ( g_strrstr ( dvbt[c], gst_param_dvb_descr_n[d].name ) )
                {
                    g_object_get ( dvbsrc_tune, gst_param_dvb_descr_n[d].gst_param, &d_data, NULL );
                    g_string_append_printf ( gstring, ":%s=%d", gst_param_dvb_descr_n[d].gst_param, d_data );

                    break;
                }
            }
        }
    }

    if ( DVBTYPE == SYS_DVBS || DVBTYPE == SYS_TURBO || DVBTYPE == SYS_DVBS2 /* || SYS_ISDBS */ )
    {
        for ( c = 0; c < G_N_ELEMENTS ( dvbs ); c++ )
        {
            if ( DVBTYPE == SYS_TURBO )
                if ( g_strrstr ( dvbs[c], "Pilot" ) || g_strrstr ( dvbs[c], "Rolloff" ) || g_strrstr ( dvbs[c], "Stream ID" ) ) continue;

            if ( DVBTYPE == SYS_DVBS )
                if ( g_strrstr ( dvbs[c], "Modulation" ) || g_strrstr ( dvbs[c], "Pilot" ) || g_strrstr ( dvbs[c], "Rolloff" ) || g_strrstr ( dvbs[c], "Stream ID" ) ) continue;

            for ( d = 0; d < G_N_ELEMENTS ( gst_param_dvb_descr_n ); d++ )
            {
                if ( g_strrstr ( dvbs[c], gst_param_dvb_descr_n[d].name ) )
                {
                    if ( g_strrstr ( "polarity", gst_param_dvb_descr_n[d].gst_param ) )
                    {
                        g_string_append_printf ( gstring, ":polarity=%s", pol );
                        continue;
                    }

                    if ( g_strrstr ( "lnb-type", gst_param_dvb_descr_n[d].gst_param ) )
                    {
                        g_string_append_printf ( gstring, ":%s=%d", "lnb-type", lnb_type );
                        continue;
                    }

                    g_object_get ( dvbsrc_tune, gst_param_dvb_descr_n[d].gst_param, &d_data, NULL );
                    g_string_append_printf ( gstring, ":%s=%d", gst_param_dvb_descr_n[d].gst_param, d_data );

                    break;
                }
            }
        }
    }

    if ( DVBTYPE == SYS_DVBC_ANNEX_A || DVBTYPE == SYS_DVBC_ANNEX_C || DVBTYPE == SYS_DVBC_ANNEX_B || DVBTYPE == SYS_ATSC )
    {
        for ( c = 0; c < G_N_ELEMENTS ( dvbc ); c++ )
        {
            if ( DVBTYPE == SYS_DVBC_ANNEX_B || DVBTYPE == SYS_ATSC )
                if ( g_strrstr ( dvbc[c], "Inversion" ) || g_strrstr ( dvbc[c], "Symbol rate" ) || g_strrstr ( dvbc[c], "Code Rate HP" ) ) continue;

            for ( d = 0; d < G_N_ELEMENTS ( gst_param_dvb_descr_n ); d++ )
            {
                if ( g_strrstr ( dvbc[c], gst_param_dvb_descr_n[d].name ) )
                {
                    g_object_get ( dvbsrc_tune, gst_param_dvb_descr_n[d].gst_param, &d_data, NULL );
                    g_string_append_printf ( gstring, ":%s=%d", gst_param_dvb_descr_n[d].gst_param, d_data );

                    break;
                }
            }
        }
    }
}

static void tv_scan_set_info_ch ()
{
    gint c = 0, c_tv = 0, c_ro = 0;

    for ( c = 0; c < sdt_count; c++ )
    {
        if ( dvb_gst_scan_pmt_n[c].vpid > 0 ) c_tv++;
        if ( dvb_gst_scan_pmt_n[c].apid > 0 && dvb_gst_scan_pmt_n[c].vpid == 0 ) c_ro++;
    }

    tv_scan_set_all_ch ( sdt_count, c_tv, c_ro );
    g_print ( "\nAll Channels: %d    TV: %d    Radio: %d    Other: %d\n\n", sdt_count, c_tv, c_ro, sdt_count-c_tv-c_ro );
}

static void tv_scan_read_ch_to_treeview ()
{
    GString *gstr_data = g_string_new ( NULL );
    tv_scan_get_tp_data ( gstr_data );
    if ( msg_info ) g_print ( "%s \n", gstr_data->str );

    gint i = 0, c = 0;
    for ( i = 0; i < sdt_count; i++ )
    {
        for ( c = 0; c < pmt_count; c++ )
        {
            if ( dvb_gst_scan_sdt_n[i].pmn_pid == dvb_gst_scan_pmt_n[c].pmn_pid )
                break;
        }

        GString *gstring = g_string_new ( tv_strip_ch_name ( dvb_gst_scan_sdt_n[i].name ) );
        g_string_append_printf ( gstring, ":program-number=%d:video-pid=%d:audio-pid=%d",
            dvb_gst_scan_pmt_n[c].pmn_pid, dvb_gst_scan_pmt_n[c].vpid, dvb_gst_scan_pmt_n[c].apid );

        g_string_append_printf ( gstring, "%s", gstr_data->str );

        if ( /*win_scan &&*/ dvb_gst_scan_pmt_n[c].apid != 0 ) // ignore other
            tv_scan_add_to_treeview ( dvb_gst_scan_sdt_n[i].name, gstring->str );

        g_print ( "%s \n", dvb_gst_scan_sdt_n[i].name );
        if ( msg_info ) g_print ( "%s \n", gstring->str );

        g_free ( dvb_gst_scan_sdt_n[i].name );
        g_string_free ( gstring, TRUE );
    }

    g_string_free ( gstr_data, TRUE );
    tv_scan_set_info_ch ();
}


// "DVB-T, DVB-S, DVB-C"

static void tv_scan_start ( GtkButton *button, gpointer data )
{
    if ( GST_ELEMENT_CAST ( dvb_scan ) -> current_state == GST_STATE_PLAYING )
        return;

    start_clear_dvb_gst_scan ();

    g_object_set ( dvbsrc_tune, "adapter",  adapter_ct,  NULL );
    g_object_set ( dvbsrc_tune, "frontend", frontend_ct, NULL );

    time ( &t_start );
    gst_element_set_state ( dvb_scan, GST_STATE_PLAYING );

    dvb_type_str = (gchar *)data;

    g_print ( "tv_scan_start: %s \n", dvb_type_str );
    tv_info_widget_name ( GTK_WIDGET ( button ) );
}
static void tv_scan_stop ( GtkButton *button, gpointer data )
{
    if ( GST_ELEMENT_CAST ( dvb_scan ) -> current_state == GST_STATE_NULL )
        return;

    gst_element_set_state ( dvb_scan, GST_STATE_NULL );

    gtk_label_set_text ( scan_snr_dvbt, "Signal level 0  &  Signal quality 0" );
    gtk_label_set_text ( scan_snr_dvbs, "Signal level 0  &  Signal quality 0" );
    gtk_label_set_text ( scan_snr_dvbc, "Signal level 0  &  Signal quality 0" );

    tv_scan_read_ch_to_treeview ();

    if ( (gchar *)data )
    {
        g_print ( "tv_scan_stop: %s \n", (gchar *)data );
        tv_info_widget_name ( GTK_WIDGET ( button ) );
    }
}

static void tv_scan_set_all_ch ( gint all_ch, gint c_tv, gint c_ro )
{
    gchar *text = g_strdup_printf ( " All Channels: %d \n TV: %d -- Radio: %d -- Other: %d\n", all_ch, c_tv, c_ro, all_ch - c_tv - c_ro );
    gtk_label_set_text ( GTK_LABEL ( all_channels ), text );
    g_free ( text );
}

static GtkBox * tv_scan_battons_box ( const gchar *type )
{
    GtkBox *g_box = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_VERTICAL, 0 );

    GtkBox *hb_box = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_HORIZONTAL, 0 );

    GtkLabel *label = (GtkLabel *)gtk_label_new ( "Signal level 0  &  Signal quality 0" );
    if ( g_strrstr ( type, "DVB-T" ) ) scan_snr_dvbt = label;
    if ( g_strrstr ( type, "DVB-S" ) ) scan_snr_dvbs = label;
    if ( g_strrstr ( type, "DVB-C" ) ) scan_snr_dvbc = label;
    gtk_box_pack_start ( g_box, GTK_WIDGET  ( label ), FALSE, FALSE, 10 );

    GtkButton *button_scan = (GtkButton *)gtk_button_new_with_label ( " Scan " );
    g_signal_connect ( button_scan, "clicked", G_CALLBACK ( tv_scan_start ), (gpointer)type );
    gtk_box_pack_start ( hb_box, GTK_WIDGET ( button_scan ), FALSE, FALSE, 0 );

    GtkButton *button_stop = (GtkButton *) gtk_button_new_with_label ( " Stop " );
    g_signal_connect ( button_stop, "clicked", G_CALLBACK ( tv_scan_stop ),  (gpointer)type );
    gtk_box_pack_start ( hb_box, GTK_WIDGET ( button_stop ), FALSE, FALSE, 0 );

    gtk_box_pack_start ( g_box, GTK_WIDGET  ( hb_box ), FALSE, FALSE, 0 );

    return g_box;
}

static GtkBox * tv_scan_dvb_all  ( guint num, const gchar *dvball[], const gint dvbeall[], const gchar *type )
{
    GtkBox *g_box = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_VERTICAL, 0 );
    gtk_widget_set_margin_start ( GTK_WIDGET ( g_box ), 10 );
    gtk_widget_set_margin_end   ( GTK_WIDGET ( g_box ), 10 );

    GtkGrid *grid = (GtkGrid *)gtk_grid_new();
    gtk_grid_set_column_homogeneous ( GTK_GRID ( grid ), TRUE );
    gtk_box_pack_start ( g_box, GTK_WIDGET ( grid ), TRUE, TRUE, 10 );

    GtkLabel *label;
    GtkSpinButton *spinbutton;
    GtkComboBoxText *scan_combo_box;

    guint d = 0, c = 0, z = 0;
    for ( d = 0; d < num; d++ )
    {
        label = (GtkLabel *)gtk_label_new ( dvball[d] );
        gtk_widget_set_halign ( GTK_WIDGET ( label ), GTK_ALIGN_START );
        gtk_grid_attach ( GTK_GRID ( grid ), GTK_WIDGET ( label ), 0, d, 1, 1 );

        if ( dvbeall[d] == 1 )
        {
            spinbutton = (GtkSpinButton *) gtk_spin_button_new_with_range ( 0, 100000000, 1 );
            gtk_grid_attach ( GTK_GRID ( grid ), GTK_WIDGET ( spinbutton ), 1, d, 1, 1 );
            if ( g_strrstr ( dvball[d], "Stream ID" ) )
            {
                gtk_spin_button_set_range ( spinbutton, -1, 255 );
                gtk_spin_button_set_value ( spinbutton, -1 );
            }
            if ( g_strrstr ( dvball[d], "Bandwidth" ) )
            {
                gtk_spin_button_set_range ( spinbutton, 0, 25000000 );
                gtk_spin_button_set_value ( spinbutton, 8000000 );
            }
            g_signal_connect ( spinbutton, "changed", G_CALLBACK ( scan_changed_spin_all ), label );
        }
        else
        {
            scan_combo_box = (GtkComboBoxText *) gtk_combo_box_text_new ();
            gtk_grid_attach ( GTK_GRID ( grid ), GTK_WIDGET ( scan_combo_box ), 1, d, 1, 1 );

            for ( c = 0; c < G_N_ELEMENTS ( gst_param_dvb_descr_n ); c++ )
            {
              if ( g_strrstr ( dvball[d], gst_param_dvb_descr_n[c].name ) )
              {
                for ( z = 0; z < gst_param_dvb_descr_n[c].cdsc; z++ )
                    gtk_combo_box_text_append_text ( scan_combo_box, gst_param_dvb_descr_n[c].dvb_descr[z].text_vis );

                if ( g_strrstr ( gst_param_dvb_descr_n[c].gst_param, "polarity" ) || g_strrstr ( gst_param_dvb_descr_n[c].gst_param, "lnb-type" ) )
                    continue;

                gint d_data = 0;
                g_object_get ( dvbsrc_tune, gst_param_dvb_descr_n[c].gst_param, &d_data, NULL );
                gtk_combo_box_set_active ( GTK_COMBO_BOX ( scan_combo_box ), d_data );
              }
            }

            if ( g_strrstr ( dvball[d], "DiSEqC" ) )
            {
                gtk_combo_box_text_append_text ( scan_combo_box, "None" );
                for ( z = 0; z < 8; z++ )
                {
                    gchar *text = g_strdup_printf  ( "%d  Lnb", z+1 );
                    gtk_combo_box_text_append_text ( scan_combo_box, text );
                    g_free ( text );
                }
            }

            if ( gtk_combo_box_get_active ( GTK_COMBO_BOX ( scan_combo_box ) ) == -1 )
                 gtk_combo_box_set_active ( GTK_COMBO_BOX ( scan_combo_box ), 0 );

            g_signal_connect ( scan_combo_box, "changed", G_CALLBACK ( scan_changed_combo_all ), label );
        }
    }

    gtk_box_pack_start ( g_box, GTK_WIDGET ( tv_scan_battons_box ( type ) ), FALSE, FALSE, 10 );

    return g_box;
}


static void tv_scan_add_to_treeview ( gchar *name_ch, gchar *data )
{
    GtkTreeModel *model = gtk_tree_view_get_model ( GTK_TREE_VIEW ( scan_treeview ) );
    GtkTreeIter iter;

    guint ind = gtk_tree_model_iter_n_children ( gtk_tree_view_get_model ( GTK_TREE_VIEW ( scan_treeview ) ), NULL );

    gtk_list_store_append ( GTK_LIST_STORE ( model ), &iter );
    gtk_list_store_set ( GTK_LIST_STORE ( model ), &iter,
                         COL_NUM,      ind+1,
                         COL_FILES_CH, name_ch,
                         COL_URI_DATA, data,
                         -1 );
}
static void tv_scan_ch_save ()
{
    GtkTreeIter iter;
    GtkTreeModel *model = gtk_tree_view_get_model ( GTK_TREE_VIEW ( scan_treeview ) );

    gboolean valid;
    for ( valid = gtk_tree_model_get_iter_first ( model, &iter ); valid;
          valid = gtk_tree_model_iter_next ( model, &iter ) )
    {
        gchar *name, *data;
        gtk_tree_model_get ( model, &iter, 1, &name, -1 );
        gtk_tree_model_get ( model, &iter, 2, &data, -1 );

        tv_add_channels ( name, data );

        g_free ( name );
        g_free ( data );
    }
}
static void tv_scan_ch_clear ()
{
    GtkTreeModel *model = gtk_tree_view_get_model ( GTK_TREE_VIEW ( scan_treeview ) );
    gtk_list_store_clear ( GTK_LIST_STORE ( model ) );

    tv_scan_set_all_ch ( 0, 0, 0 );
}

static GtkBox * tv_scan_channels_battons_box ()
{
    GtkBox *g_box = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_VERTICAL, 0 );

    GtkBox *hb_box = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_HORIZONTAL, 0 );

    //GtkButton *button_save = (GtkButton *)gtk_button_new_with_label ( " Save " );
    GtkButton *button_save = (GtkButton *)gtk_button_new_from_icon_name ( "document-save", GTK_ICON_SIZE_SMALL_TOOLBAR );
    g_signal_connect ( button_save, "clicked", G_CALLBACK ( tv_scan_ch_save ), NULL );
    gtk_box_pack_end ( hb_box, GTK_WIDGET ( button_save ), FALSE, FALSE, 0 );

    //GtkButton *button_clear = (GtkButton *) gtk_button_new_with_label ( " Clear " );
    GtkButton *button_clear = (GtkButton *)gtk_button_new_from_icon_name ( "edit-clear", GTK_ICON_SIZE_SMALL_TOOLBAR );
    g_signal_connect ( button_clear, "clicked", G_CALLBACK ( tv_scan_ch_clear ), NULL );
    gtk_box_pack_end ( hb_box, GTK_WIDGET ( button_clear ), FALSE, FALSE, 0 );

    gtk_box_pack_start ( g_box, GTK_WIDGET  ( hb_box ), FALSE, FALSE, 0 );

    return g_box;
}

static GtkBox * tv_scan_channels  ()
{
    GtkBox *g_box  = (GtkBox *)gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_margin_start ( GTK_WIDGET ( g_box ), 10 );
    gtk_widget_set_margin_end   ( GTK_WIDGET ( g_box ), 10 );

    all_channels = (GtkLabel *)gtk_label_new ( " All Channels \n" );
    gtk_widget_set_halign ( GTK_WIDGET ( all_channels ),    GTK_ALIGN_START );

    scan_treeview = (GtkTreeView *)gtk_tree_view_new_with_model ( GTK_TREE_MODEL ( gtk_list_store_new ( 3, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING ) ) );

    gtk_box_pack_start ( g_box, GTK_WIDGET ( tv_scroll_win ( scan_treeview, "Channels", "Data" ) ), TRUE, TRUE, 10 );
    gtk_box_pack_start ( g_box, GTK_WIDGET ( all_channels ),  FALSE, FALSE, 0 );

    gtk_box_pack_start ( g_box, GTK_WIDGET ( tv_scan_channels_battons_box () ), FALSE, FALSE, 10 );

    return g_box;
}


static void tv_get_dvb_name ( GtkLabel *label )
{
    gchar *dvb_name = NULL;

    gint fd;
    gchar *fd_name = g_strdup_printf ( "/dev/dvb/adapter%d/frontend%d", adapter_ct, frontend_ct );

    if ( ( fd = g_open ( fd_name, O_RDONLY ) ) == -1 )
    {
        const gchar *errno_info = g_strerror ( errno );
            g_printerr ( "ERROR: %s %s\n", fd_name, errno_info );
        dvb_name = g_strdup ( errno_info );
    }
    else
    {
        struct dvb_frontend_info info;

        if ( ( ioctl ( fd, FE_GET_INFO, &info ) ) == -1 )
            perror ( "find_dvb: ioctl FE_GET_INFO failed \n" );
        else
            dvb_name = g_strdup ( info.name );


        struct dtv_property p[] = { { .cmd = DTV_DELIVERY_SYSTEM } };
        struct dtv_properties cmdseq = { .num = 1, .props = p };

        if ( (ioctl(fd, FE_GET_PROPERTY, &cmdseq)) == -1 )
            perror("FE_GET_PROPERTY failed");
        else
            DVB_DELSYS = p[0].u.data;

        g_print ( "DVB Name: %s \nDelivery System: %d \n\n", dvb_name, DVB_DELSYS );

        g_close ( fd, NULL );
    }

    g_free  ( fd_name );

    gtk_label_set_text ( label, dvb_name );

    if ( dvb_name ) g_free  ( dvb_name );
}

static void tv_sc_set_adapter ( GtkSpinButton *button, GtkLabel *label )
{
    gtk_spin_button_update ( button );
    adapter_ct = gtk_spin_button_get_value ( button );

    tv_get_dvb_name ( label );
}
static void tv_sc_set_frontend ( GtkSpinButton *button, GtkLabel *label )
{
    gtk_spin_button_update ( button );
    frontend_ct = gtk_spin_button_get_value ( button );

    tv_get_dvb_name ( label );
}

static void tv_convert ()
{
    gchar *filename = tv_openf ();

    if ( filename )
        tv_convert_dvb5 ( filename );

     g_free ( filename );
}


static GtkBox * tv_scan_convert ()
{
    GtkBox *g_box  = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_VERTICAL, 0 );
    gtk_widget_set_margin_start ( GTK_WIDGET ( g_box ), 10 );
    gtk_widget_set_margin_end   ( GTK_WIDGET ( g_box ), 10 );

    GtkGrid *grid = (GtkGrid *)gtk_grid_new();
    gtk_grid_set_column_homogeneous ( GTK_GRID ( grid ), TRUE );
    gtk_box_pack_start ( g_box, GTK_WIDGET ( grid ), TRUE, TRUE, 10 );

    struct data_a { GtkWidget *label; gchar *ltext; GtkWidget *widget; gchar *etext; void (* activate); }
    data_a_n[] =
    {
        { gtk_label_new ( "" ), "DVB Device", NULL, NULL, NULL },
        { gtk_label_new ( "" ), NULL,         NULL, NULL, NULL },

        { gtk_label_new ( "" ), "Adapter",  gtk_spin_button_new_with_range ( 0, 16, 1 ), NULL, tv_sc_set_adapter  },
        { gtk_label_new ( "" ), "Frontend", gtk_spin_button_new_with_range ( 0, 16, 1 ), NULL, tv_sc_set_frontend }
    };

    GtkLabel *label_name;

    guint d = 0;
    for ( d = 0; d < G_N_ELEMENTS ( data_a_n ); d++ )
    {
        gtk_label_set_label ( GTK_LABEL ( data_a_n[d].label ), data_a_n[d].ltext );
        gtk_widget_set_halign ( GTK_WIDGET ( data_a_n[d].label ), ( d == 0 ) ? GTK_ALIGN_CENTER : GTK_ALIGN_START );
        gtk_grid_attach ( GTK_GRID ( grid ), GTK_WIDGET ( data_a_n[d].label ), 0, d, ( d == 0 ) ? 2 : 1, 1 );

        if ( d == 0 )
        {
            label_name = GTK_LABEL ( data_a_n[d].label );
            tv_get_dvb_name ( label_name );
            continue;
        }

        if ( !data_a_n[d].ltext ) continue;

        gtk_grid_attach ( GTK_GRID ( grid ), GTK_WIDGET ( data_a_n[d].widget ), 1, d, 1, 1 );

        if ( d == 2 ) gtk_spin_button_set_value ( GTK_SPIN_BUTTON ( data_a_n[d].widget ), adapter_ct  );
        if ( d == 3 ) gtk_spin_button_set_value ( GTK_SPIN_BUTTON ( data_a_n[d].widget ), frontend_ct );

        if ( d == 2 || d == 3 )
            g_signal_connect ( data_a_n[d].widget, "changed", G_CALLBACK ( data_a_n[d].activate ), label_name );
    }

    GtkLabel *label = (GtkLabel *)gtk_label_new ( "Choose file:\n    dvb_channel.conf ( format DVBv5 )" );
    gtk_widget_set_halign ( GTK_WIDGET ( label ), GTK_ALIGN_START );
    gtk_box_pack_start ( g_box, GTK_WIDGET ( label ), FALSE, FALSE, 10 );

    GtkButton *button_convert = (GtkButton *)gtk_button_new_with_label ( "Convert" );
    g_signal_connect ( button_convert, "clicked", G_CALLBACK ( tv_convert ), NULL );
    gtk_box_pack_start ( g_box, GTK_WIDGET ( button_convert ), FALSE, FALSE, 10 );

    return g_box;
}


static gchar * tv_get_prop ( const gchar *prop )
{
    gchar *name = NULL;
        g_object_get ( gtk_settings_get_default (), prop, &name, NULL );
    return name;
}
static void tv_set_prop ( GtkEntry *entry, const gchar *prop, gchar *path )
{
    gchar *i_file = g_strconcat ( path, "/index.theme", NULL );

        if ( g_file_test ( i_file, G_FILE_TEST_EXISTS ) )
        {
            gchar *name = g_path_get_basename ( path );
                g_object_set ( gtk_settings_get_default (), prop, name, NULL );
                gtk_entry_set_text ( entry, name );
            g_free ( name );
        }

    g_free ( i_file );
}
static void tv_set_theme ( GtkEntry *entry )
{
    gchar *path = tv_rec_dir ();
        if ( path ) tv_set_prop ( entry, "gtk-theme-name", path );
    g_free ( path );
}
static void tv_set_icon ( GtkEntry *entry )
{
    gchar *path = tv_rec_dir ();
        if ( path ) tv_set_prop ( entry, "gtk-icon-theme-name", path );
    g_free ( path );
}

static void tv_set_rec_dir ( GtkEntry *entry )
{
    g_free ( rec_dir ); rec_dir = tv_rec_dir ();
    if ( rec_dir ) gtk_entry_set_text ( entry, rec_dir );
}

static void tv_set_rec_data_venc ( GtkEntry *entry )
{
    g_free ( video_encoder );
    video_encoder = g_strdup ( gtk_entry_get_text ( entry ) );
}
static void tv_set_rec_data_aenc ( GtkEntry *entry )
{
    g_free ( audio_encoder );
    audio_encoder = g_strdup ( gtk_entry_get_text ( entry ) );
}
static void tv_set_rec_data_mux ( GtkEntry *entry )
{
    g_free ( muxer );
    muxer = g_strdup ( gtk_entry_get_text ( entry ) );
}
static void tv_set_rec_data_ext ( GtkEntry *entry )
{
    g_free ( file_ext );
    file_ext = g_strdup ( gtk_entry_get_text ( entry ) );
}

static void tv_changed_sw_et ( GtkSwitch *switch_p )
{
    if ( !gtk_switch_get_state (switch_p) )
        rec_en_ts = TRUE;
    else
        rec_en_ts = FALSE;

    for ( c = 0; c < 4; c++ )
        gtk_widget_set_sensitive ( GTK_WIDGET ( entry_enc[c] ), rec_en_ts );
}

static GtkBox * tv_scan_pref ()
{
    GtkBox *g_box  = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_VERTICAL, 0 );
    gtk_widget_set_margin_start ( GTK_WIDGET ( g_box ), 10 );
    gtk_widget_set_margin_end   ( GTK_WIDGET ( g_box ), 10 );

    GtkGrid *grid = (GtkGrid *)gtk_grid_new();
    gtk_grid_set_column_homogeneous ( GTK_GRID ( grid ), TRUE );
    gtk_box_pack_start ( g_box, GTK_WIDGET ( grid ), TRUE, TRUE, 10 );

    struct data_a { GtkWidget *label; gchar *ltext; GtkWidget *widget; gchar *etext; void (* activate); gboolean icon_set; }
    data_a_n[] =
    {
        { gtk_label_new ( "" ), "Theme",            gtk_entry_new  (), tv_get_prop ( "gtk-theme-name"      ), tv_set_theme, TRUE  },
        { gtk_label_new ( "" ), "Icon-theme",       gtk_entry_new  (), tv_get_prop ( "gtk-icon-theme-name" ), tv_set_icon,  TRUE  },

        { gtk_label_new ( "" ), NULL,               NULL,              NULL,          NULL,           FALSE },
        { gtk_label_new ( "" ), "Recording folder", gtk_entry_new  (), rec_dir,       tv_set_rec_dir, TRUE  },
        { gtk_label_new ( "" ), NULL,               NULL,              NULL,          NULL,           FALSE },

        { gtk_label_new ( "" ), "Encoder / Ts",     gtk_switch_new (), NULL,          tv_changed_sw_et,     FALSE },
        { gtk_label_new ( "" ), "Audio encoder",    gtk_entry_new  (), audio_encoder, tv_set_rec_data_aenc, FALSE },
        { gtk_label_new ( "" ), "Video encoder",    gtk_entry_new  (), video_encoder, tv_set_rec_data_venc, FALSE },
        { gtk_label_new ( "" ), "Muxer",            gtk_entry_new  (), muxer,         tv_set_rec_data_mux,  FALSE },
        { gtk_label_new ( "" ), "File ext",         gtk_entry_new  (), file_ext,      tv_set_rec_data_ext,  FALSE }
    };

    guint d = 0, z = 0;
    for ( d = 0; d < G_N_ELEMENTS ( data_a_n ); d++ )
    {
        gtk_label_set_label ( GTK_LABEL ( data_a_n[d].label ), data_a_n[d].ltext );
        gtk_widget_set_halign ( GTK_WIDGET ( data_a_n[d].label ), GTK_ALIGN_START );
        gtk_grid_attach ( GTK_GRID ( grid ), GTK_WIDGET ( data_a_n[d].label ), 0, d, 1, 1 );

        if ( !data_a_n[d].ltext ) continue;

        if ( !data_a_n[d].etext )
        {
            gtk_grid_attach ( GTK_GRID ( grid ), GTK_WIDGET ( data_a_n[d].widget ), 1, d, 1, 1 );
            gtk_switch_set_state ( GTK_SWITCH ( data_a_n[d].widget ), !rec_en_ts );
            g_signal_connect ( data_a_n[d].widget, "notify::active", G_CALLBACK ( data_a_n[d].activate ), NULL );
            continue;
        }

        gtk_entry_set_text ( GTK_ENTRY ( data_a_n[d].widget ), data_a_n[d].etext );
        gtk_grid_attach ( GTK_GRID ( grid ), GTK_WIDGET ( data_a_n[d].widget ), 1, d, 1, 1 );

        if ( data_a_n[d].icon_set )
        {
            g_object_set ( data_a_n[d].widget, "editable", FALSE, NULL );
            gtk_entry_set_icon_from_icon_name ( GTK_ENTRY ( data_a_n[d].widget ), GTK_ENTRY_ICON_SECONDARY, "document-open" );
            g_signal_connect ( data_a_n[d].widget, "icon-press", G_CALLBACK ( data_a_n[d].activate ), NULL );
        }
        else
            g_signal_connect ( data_a_n[d].widget, "changed", G_CALLBACK ( data_a_n[d].activate ), NULL );

        if ( d > 5 ) entry_enc[z++] = GTK_ENTRY ( data_a_n[d].widget );
    }

    for ( c = 0; c < 4; c++ )
        gtk_widget_set_sensitive ( GTK_WIDGET ( entry_enc[c] ), rec_en_ts );

    return g_box;
}


static GtkBox * all_box_scan ( guint i )
{
    GtkBox *only_box = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_VERTICAL, 0 );

    if ( i == 0 )  { return tv_scan_pref     (); }
    if ( i == 1 )  { return tv_scan_convert  (); }
    if ( i == 5 )  { return tv_scan_channels (); }
    if ( i == 2 )  { return tv_scan_dvb_all  ( G_N_ELEMENTS ( dvbt ), dvbt, dvbet, "DVB-T" ); }
    if ( i == 3 )  { return tv_scan_dvb_all  ( G_N_ELEMENTS ( dvbs ), dvbs, dvbes, "DVB-S" ); }
    if ( i == 4 )  { return tv_scan_dvb_all  ( G_N_ELEMENTS ( dvbc ), dvbc, dvbec, "DVB-C" ); }

    return only_box;
}

static void tv_scan_quit ( GtkWindow *window )
{
    gtk_widget_destroy ( GTK_WIDGET ( window ) );
}
static void tv_scan_close ( GtkButton *button, GtkWindow *window )
{
    tv_info_widget_name ( GTK_WIDGET ( button ) );
    tv_scan_quit ( window );
}

static void tv_win_scan ()
{
    scan_gst_create ();

    GtkWindow *window =      (GtkWindow *)gtk_window_new ( GTK_WINDOW_TOPLEVEL );
    gtk_window_set_modal     ( window, TRUE );
    gtk_window_set_position  ( window, GTK_WIN_POS_CENTER );
    gtk_window_set_title     ( window, "Scan" );
	g_signal_connect         ( window, "destroy", G_CALLBACK ( tv_scan_quit ), NULL );

	gtk_window_set_type_hint ( GTK_WINDOW (window), GDK_WINDOW_TYPE_HINT_UTILITY );

    GtkBox *m_box = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_VERTICAL,   0 );
    GtkBox *h_box = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_HORIZONTAL, 0 );

    notebook = (GtkNotebook *)gtk_notebook_new ();
    gtk_notebook_set_scrollable ( notebook, TRUE );

    GtkBox *m_box_n[G_N_ELEMENTS ( labels_scan_n )];

    for ( j = 0; j < G_N_ELEMENTS ( labels_scan_n ); j++ )
    {
        m_box_n[j] = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_VERTICAL, 0 );
        gtk_box_pack_start ( m_box_n[j], GTK_WIDGET ( all_box_scan ( j ) ), TRUE, TRUE, 0 );
        gtk_notebook_append_page ( notebook, GTK_WIDGET ( m_box_n[j] ),  gtk_label_new ( labels_scan_n[j].name ) );
    }

    gtk_notebook_set_tab_pos ( notebook, GTK_POS_TOP );
    gtk_box_pack_start ( m_box, GTK_WIDGET (notebook), TRUE, TRUE, 0 );

    GtkButton *button_close = (GtkButton *)gtk_button_new_from_icon_name ( "window-close", GTK_ICON_SIZE_SMALL_TOOLBAR );
    g_signal_connect ( button_close, "clicked", G_CALLBACK ( tv_scan_close ), window );
    gtk_box_pack_end ( h_box, GTK_WIDGET ( button_close ), FALSE, FALSE, 5 );

    gtk_box_pack_start ( m_box, GTK_WIDGET ( h_box ), FALSE, FALSE, 5 );
    gtk_container_set_border_width ( GTK_CONTAINER ( m_box ), 5 );
    gtk_container_add ( GTK_CONTAINER ( window ), GTK_WIDGET ( m_box ) );

    gtk_widget_show_all ( GTK_WIDGET ( window ) );
}
