/*
 * Copyright 2014 - 2018 Stepan Perun
 * This program is free software.
 * License: Gnu Lesser General Public License
 * http://www.gnu.org/licenses/lgpl.html
*/


#include <stdlib.h>
#include <stdio.h>
#include <glib/gstdio.h>


#include <locale.h>
#include <glib/gi18n.h>

#include "gtv-dvb.h"
#include "gtv-gst.h"
#include "gtv-scan.h"
#include "gtv-toolbar.h"
#include "gtv-treeview.h"
#include "gtv-service.h"
#include "gtv-pref.h"
#include "gtv-eqa.h"
#include "gtv-eqv.h"



struct GtvBase
{
	GtkWindow *main_window;
	GSimpleActionGroup *group;
	
	GtkBox *tool_hbox, *sw_vbox;
	GtkVolumeButton *volbutton;
	GtkComboBoxText *combo_lang;
	GtkLabel *label_tv_rec;

	GtkTreeView *gtv_tree_view;
	GtkToolbar  *toolbar_media, *toolbar_sw;
	
	guint time_rec;
	gdouble volume_set;
	gulong combo_signal_id;
	
	gchar *gtv_conf, *channels_conf;
	gchar *ch_name;
};

struct GtvBase gtvbase;


static guint j = 0, c = 0;



void gtv_audio ()
{
    if ( GST_ELEMENT_CAST ( gtvgstdvb.dvbplay )->current_state == GST_STATE_PLAYING )
        gtv_eqa_win ( gtv_gstelement_eqa () );
}
void gtv_video ()
{
    if ( gtvdvb.video_enable && GST_ELEMENT_CAST ( gtvgstdvb.dvbplay )->current_state == GST_STATE_PLAYING )
        gtv_eqv_win ( gtv_gstelement_eqv () );
}

void gtv_pref  () { gtv_win_pref  (); }
void gtv_scan  () { gtv_win_scan  (); }
void gtv_about () { gtv_win_about ( gtvbase.main_window ); }
void gtv_goup  () { gtv_treeview_up_down ( gtvbase.gtv_tree_view, TRUE  ); }
void gtv_down  () { gtv_treeview_up_down ( gtvbase.gtv_tree_view, FALSE ); }
void gtv_remv  () { gtv_treeview_remove  ( gtvbase.main_window, gtvbase.gtv_tree_view ); }
void gtv_clear () { gtv_treeview_clear   ( gtvbase.main_window, gtvbase.gtv_tree_view ); }

void gtv_remove_all_combo ( GtkComboBoxText *combo );



static void gtv_sensitive ( gboolean set_sensitive, guint start_s, guint end_s )
{
    gtk_widget_set_sensitive ( GTK_WIDGET ( gtvbase.volbutton ), set_sensitive );

    const gchar *menu_n[] = { "Record", "Stop", "Mute", "EQ-Audio", "EQ-Video" };

    for ( c = 0; c < G_N_ELEMENTS ( menu_n ); c++ )
        g_action_group_action_enabled_changed ( G_ACTION_GROUP (gtvbase.group), menu_n[c], set_sensitive );


    for ( j = start_s; j < end_s; j++ )
        gtk_widget_set_sensitive ( GTK_WIDGET ( gtv_get_item_toolbar ( gtvbase.toolbar_media, j ) ), set_sensitive );
}

static void gtv_checked_video ( gchar *data )
{
	if ( !g_strrstr ( data, "video-pid" ) || g_strrstr ( data, "video-pid=0" ) )
		 gtvdvb.video_enable = FALSE;
	else
		 gtvdvb.video_enable = TRUE;
}

static void gtv_set_tuning_timeout ( GstElement *element )
{
    guint64 timeout = 0;
    g_object_get ( element, "tuning-timeout", &timeout, NULL );
    g_object_set ( element, "tuning-timeout", (guint64)timeout / 5, NULL );
}

static void gtv_check_a_f_new ( GstElement *element )
{
	guint adapter_new = 0, frontend_new = 0;
	g_object_get ( element, "adapter",  &adapter_new,  NULL );
    g_object_get ( element, "frontend", &frontend_new, NULL );
    
    if ( gtvdvb.adapter != adapter_new || gtvdvb.frontend != frontend_new )
		gtv_get_dvb_info ( FALSE, FALSE, FALSE, adapter_new, frontend_new );

	gtvdvb.adapter  = adapter_new;
	gtvdvb.frontend = frontend_new;
}

static gchar * gtv_data_split_set_dvb ( gchar *data )
{
    GstElement *element = gtv_gstelement_src ();
    gtv_set_tuning_timeout ( element );

    gchar **fields = g_strsplit ( data, ":", 0 );
    guint numfields = g_strv_length ( fields );

    gchar *ch_name = g_strdup ( fields[0] );

    for ( j = 1; j < numfields; j++ )
    {
        if ( g_strrstr ( fields[j], "delsys" ) || g_strrstr ( fields[j], "audio-pid" ) || g_strrstr ( fields[j], "video-pid" ) ) continue;

        if ( !g_strrstr ( fields[j], "=" ) ) continue;

        gchar **splits = g_strsplit ( fields[j], "=", 0 );
        
			if ( g_strrstr ( splits[0], "polarity" ) )
            {
				if ( splits[1][0] == 'v' || splits[1][0] == 'V' || splits[1][0] == '0' )
					g_object_set ( element, "polarity", "V", NULL );
                else
					g_object_set ( element, "polarity", "H", NULL );
				
				continue;
			}
			
			long dat = atol ( splits[1] );

            if ( g_strrstr ( splits[0], "program-number" ) )
            {
                gtvdvb.sid = dat;
                g_object_set ( gtv_gstelement_mts (), "program-number", dat, NULL );
            }
            else if ( g_strrstr ( splits[0], "symbol-rate" ) )
				g_object_set ( element, "symbol-rate", ( dat > 100000) ? dat/1000 : dat, NULL );
            else if ( g_strrstr ( splits[0], "lnb-type" ) )
                gtv_set_lnb ( element, dat );
            else
                g_object_set ( element, splits[0], dat, NULL );

        g_strfreev (splits);
    }

    g_strfreev (fields);

	gtv_check_a_f_new ( element );

    return ch_name;
}

void gtv_str_split_ch_data ( gchar *data )
{
    gchar **lines = g_strsplit ( data, ":", 0 );

        if ( !g_str_has_prefix ( data, "#" ) )
            gtv_add_channels ( gtvbase.gtv_tree_view, lines[0], data );

    g_strfreev ( lines );
}

void gtv_stop ()
{
    if ( GST_ELEMENT_CAST ( gtvgstdvb.dvbplay )->current_state != GST_STATE_NULL )
    {
        gst_element_set_state ( gtvgstdvb.dvbplay, GST_STATE_NULL );
        gtv_gst_tsdemux_remove ();
        gtvdvb.rec_status = TRUE;
        gtvpref.gtvpshed.sched_rec = TRUE;

        gtv_remove_all_combo ( gtvbase.combo_lang );

        gtv_sensitive  ( FALSE, 0, gtk_toolbar_get_n_items ( GTK_TOOLBAR ( gtvbase.toolbar_media ) ) - 2 );
        
        gtk_window_set_title  ( GTK_WINDOW ( gtvbase.main_window ), "Gtv-Dvb" );
        gtk_widget_queue_draw ( GTK_WIDGET ( gtvbase.main_window ) );
        
        gtk_label_set_text ( gtvdvb.signal_snr, gtvdvb.lv_snr );
        gtk_progress_bar_set_fraction ( gtvdvb.bar_sgn, 0 );
		gtk_progress_bar_set_fraction ( gtvdvb.bar_snr, 0 );
    }
}

static void gtv_play ( gchar *data )
{
    if ( GST_ELEMENT_CAST ( gtvgstdvb.dvbplay )->current_state != GST_STATE_PLAYING )
    {
        gtv_checked_video ( data );
        gtv_gst_tsdemux ();
        gtvdvb.first_msg = TRUE;
		
		g_object_set ( gtv_gstelement_mut (), "volume", gtvbase.volume_set, NULL );

		g_free ( gtvbase.ch_name );
		gtvbase.ch_name = gtv_data_split_set_dvb ( data );
            gtk_window_set_title ( gtvbase.main_window, gtvbase.ch_name );

        gst_element_set_state ( gtvgstdvb.dvbplay, GST_STATE_PLAYING );
        gtv_sensitive ( TRUE, 0, gtk_toolbar_get_n_items ( GTK_TOOLBAR ( gtvbase.toolbar_media ) ) - 2 );
    }
}

void gtv_stop_play ( gchar *data )
{
    gtv_stop ();
    gtv_play ( data );
}

gboolean trig_lr = TRUE;
static gboolean gtv_refresh_rec ()
{

    if ( gtvdvb.rec_status )
    {
        gtk_label_set_text ( GTK_LABEL ( gtvbase.label_tv_rec ), "" );
        return FALSE;
    }

    gchar *file_rec = NULL;
    g_object_get ( gtv_gstelement_rec (), "location", &file_rec, NULL );

    struct stat sb;

    if ( stat ( file_rec, &sb ) == -1 )
    {
        perror ( "stat" );
        gtk_label_set_text ( GTK_LABEL ( gtvbase.label_tv_rec ), "" );
        g_free ( file_rec );
        
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
        gtk_label_set_markup ( gtvbase.label_tv_rec, markup );
    g_free ( markup );

    g_free ( text );

    trig_lr = !trig_lr;

    return TRUE;
}

void gtv_rec ()
{
    if ( gtvdvb.rec_status )
    {
        if ( gtv_gst_rec ( gtvbase.ch_name ) )
        {
			gtvbase.time_rec = g_timeout_add_seconds ( 1, (GSourceFunc)gtv_refresh_rec, NULL );
			gtvdvb.rec_status = !gtvdvb.rec_status;
		}
    }
    else
    {
        g_source_remove ( gtvbase.time_rec );
        gtk_label_set_text ( GTK_LABEL ( gtvbase.label_tv_rec ), "" );

        gst_element_set_state ( gtvgstdvb.dvbplay, GST_STATE_NULL );
            gtv_gst_rec_remove ();
            gtv_remove_all_combo ( gtvbase.combo_lang );
            gtvdvb.count_audio = 0;
            gtvdvb.audio_ind   = 0;
        gst_element_set_state ( gtvgstdvb.dvbplay, GST_STATE_PLAYING );
        
        gtvdvb.rec_status = !gtvdvb.rec_status;
    }
}

gboolean gtv_rec_schedule ()
{
	if ( !gtvdvb.rec_status || gtvpref.gtvpshed.ch_number < 1 ) return FALSE;
	
	gboolean res = TRUE;
	
    GtkTreeIter iter;
    GtkTreeModel *model = gtk_tree_view_get_model ( GTK_TREE_VIEW ( gtvbase.gtv_tree_view ) );

    gboolean valid;
    for ( valid = gtk_tree_model_get_iter_first ( model, &iter ); valid;
          valid = gtk_tree_model_iter_next ( model, &iter ) )
    {
		guint num = 1;
		gtk_tree_model_get ( model, &iter, COL_NUM, &num, -1 );
		
		if ( num == gtvpref.gtvpshed.ch_number )
		{
			gchar *data = NULL;
			gtk_tree_model_get ( model, &iter, COL_DATA, &data, -1 );
			
			gtv_stop_play ( data );
			g_usleep ( 2000000 );
				
			if ( GST_ELEMENT_CAST ( gtvgstdvb.dvbplay )->current_state == GST_STATE_PLAYING )
				gtv_rec ();
			else
			{
				gtv_stop_play ( data );
				g_usleep ( 4000000 );
				
				if ( GST_ELEMENT_CAST ( gtvgstdvb.dvbplay )->current_state == GST_STATE_PLAYING )
					gtv_rec ();
				else
				{
					gtv_stop ();
					res = FALSE;
					g_warning ( "gtv_rec_schedule:: not record \n" );
				}
			}
				
			g_free ( data );
			break;
		}
    }
    
    return res;
}
void gtv_rec_stop_schedule ()
{
	gtv_stop ();
}


static void gtv_volume_changed ( GtkScaleButton *button, gdouble value )
{
    if ( GST_ELEMENT_CAST ( gtvgstdvb.dvbplay )->current_state == GST_STATE_PLAYING )
      g_object_set ( gtv_gstelement_mut (), "volume", value, NULL );

	gtvbase.volume_set = value;
}

static void gtv_volume_mute ()
{
    gboolean mute;
    g_object_get ( gtv_gstelement_mut (), "mute", &mute, NULL );
    g_object_set ( gtv_gstelement_mut (), "mute", !mute, NULL );

    gtk_widget_set_sensitive ( GTK_WIDGET ( gtvbase.volbutton ), mute );
}
void gtv_mute ()
{
    if ( GST_ELEMENT_CAST ( gtvgstdvb.dvbplay )->current_state == GST_STATE_PLAYING )
      gtv_volume_mute ();
}

static void gtv_hide () { gtk_widget_hide ( GTK_WIDGET ( gtvbase.tool_hbox ) ); gtk_widget_hide ( GTK_WIDGET ( gtvbase.sw_vbox ) ); }
static void gtv_show () { gtk_widget_show ( GTK_WIDGET ( gtvbase.tool_hbox ) ); gtk_widget_hide ( GTK_WIDGET ( gtvbase.sw_vbox ) ); }

void gtv_mini ()
{
    if ( gtk_widget_get_visible ( GTK_WIDGET ( gtvbase.tool_hbox ) ) )
        gtv_hide ();
    else
        gtv_show ();
}

void gtv_plist ()
{
    if ( gtk_widget_get_visible ( GTK_WIDGET ( gtvbase.sw_vbox ) ) )
        gtk_widget_hide ( GTK_WIDGET ( gtvbase.sw_vbox ) );
    else
        gtk_widget_show ( GTK_WIDGET ( gtvbase.sw_vbox ) );
}

void gtv_flscr ()
{
    GdkWindowState state = gdk_window_get_state ( gtk_widget_get_window ( GTK_WIDGET ( gtvbase.main_window ) ) );

    if ( state & GDK_WINDOW_STATE_FULLSCREEN )
        { gtk_window_unfullscreen ( GTK_WINDOW ( gtvbase.main_window ) ); gtv_show ();  }
    else
        { gtk_window_fullscreen   ( GTK_WINDOW ( gtvbase.main_window ) ); gtv_hide (); }
}


static void gtv_draw_black ( GtkDrawingArea *widget, cairo_t *cr )
{
    GdkRGBA color; color.red = 0; color.green = 0; color.blue = 0; color.alpha = 1.0;

    gint width  = gtk_widget_get_allocated_width  ( GTK_WIDGET ( widget ) );
    gint height = gtk_widget_get_allocated_height ( GTK_WIDGET ( widget ) );

    gint widthl  = gdk_pixbuf_get_width  ( gtvdvb.gtv_logo );
    gint heightl = gdk_pixbuf_get_height ( gtvdvb.gtv_logo );

    cairo_rectangle ( cr, 0, 0, width, height );
    gdk_cairo_set_source_rgba ( cr, &color );
    cairo_fill (cr);

    if ( gtvdvb.gtv_logo != NULL )
    {
        cairo_rectangle ( cr, 0, 0, width, height );
        gdk_cairo_set_source_pixbuf ( cr, gtvdvb.gtv_logo,
            ( width / 2  ) - ( widthl  / 2 ),
            ( height / 2 ) - ( heightl / 2 ) );

        cairo_fill (cr);
    }
}

static gboolean gtv_draw_callback ( GtkDrawingArea *widget, cairo_t *cr )
{
    if (  GST_ELEMENT_CAST ( gtvgstdvb.dvbplay )->current_state == GST_STATE_NULL  )
        { gtv_draw_black ( widget, cr ); return TRUE; }

    if (  GST_ELEMENT_CAST ( gtvgstdvb.dvbplay )->current_state != GST_STATE_NULL  )
        if ( !gtvdvb.video_enable )
            { gtv_draw_black ( widget, cr ); return TRUE; }

    return FALSE;
}

static gboolean gtv_press_event ( GtkDrawingArea *draw, GdkEventButton *event, GtkMenu *menu )
{
    if ( event->button == 1 && event->type == GDK_2BUTTON_PRESS ) { gtv_flscr (); return TRUE; }
    if ( event->button == 2 ) { gtv_mute (); return TRUE; }
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


void gtv_remove_all_combo ( GtkComboBoxText *combo )
{
    g_signal_handler_block   ( combo, gtvbase.combo_signal_id );
        gtk_combo_box_text_remove_all ( GTK_COMBO_BOX_TEXT (combo) );
    g_signal_handler_unblock ( combo, gtvbase.combo_signal_id );

    gtk_widget_hide ( GTK_WIDGET (gtvbase.combo_lang) );
}
void gtv_set_combo_lang ( gchar *text_lang, guint apid )
{
    g_signal_handler_block   ( gtvbase.combo_lang, gtvbase.combo_signal_id );

        if ( text_lang )
            gtk_combo_box_text_append_text ( gtvbase.combo_lang, text_lang );
        else
        {
            gchar *text = g_strdup_printf ( "%d", apid );
                gtk_combo_box_text_append_text ( gtvbase.combo_lang, text );
            g_free ( text );
        }

        gtk_combo_box_set_active ( GTK_COMBO_BOX ( gtvbase.combo_lang ), gtvdvb.audio_ind );

        if ( gtvdvb.count_audio > 1 ) gtk_widget_show ( GTK_WIDGET (gtvbase.combo_lang) );

    g_signal_handler_unblock ( gtvbase.combo_lang, gtvbase.combo_signal_id );
}
static void gtv_combo_lang ( GtkComboBox *combo_box )
{
    if ( !gtvdvb.rec_status )
    {
        g_signal_handler_block   ( gtvbase.combo_lang, gtvbase.combo_signal_id );
            gtk_combo_box_set_active ( GTK_COMBO_BOX ( gtvbase.combo_lang ), gtvdvb.audio_ind );
        g_signal_handler_unblock ( gtvbase.combo_lang, gtvbase.combo_signal_id );

        return;
    }

    gtvdvb.audio_ind = gtk_combo_box_get_active ( GTK_COMBO_BOX ( combo_box ) );
	gtvdvb.count_audio = 0;

    gtv_remove_all_combo ( gtvbase.combo_lang );

    gst_element_set_state ( gtvgstdvb.dvbplay, GST_STATE_NULL );
    gst_element_set_state ( gtvgstdvb.dvbplay, GST_STATE_PLAYING );
}


struct toolbar_size { guint e_num; const gchar *name; } toolbar_size_n[] =
{
	{ GTK_ICON_SIZE_SMALL_TOOLBAR,  "16 Small"   },
	{ GTK_ICON_SIZE_LARGE_TOOLBAR,  "24 Large"   },
	{ GTK_ICON_SIZE_DND,  			"32 Dnd" 	 },
	{ GTK_ICON_SIZE_DIALOG,  		"48 Dialogs" }
};
	
void gtv_combo_size_add ( GtkComboBoxText *combo, guint num )
{
	guint k = 0; for ( k = 0; k < G_N_ELEMENTS ( toolbar_size_n ); k++ )
		gtk_combo_box_text_append_text ( combo, toolbar_size_n[k].name );
		
	gtk_combo_box_set_active ( GTK_COMBO_BOX ( combo ), ( num == 2 ) ? gtvdvb.size_ind_m : gtvdvb.size_ind_p );
}
static void gtv_combo_size_all ( GtkComboBox *combo_box, GtkToolbar *toolbar, guint ind )
{
	gchar *text = gtk_combo_box_text_get_active_text ( GTK_COMBO_BOX_TEXT ( combo_box ) );

	for ( j = 0; j < G_N_ELEMENTS ( toolbar_size_n ); j++ )
		if ( g_strrstr ( text, toolbar_size_n[j].name ) )
		{
			gtk_toolbar_set_icon_size ( toolbar, toolbar_size_n[j].e_num );
			if ( ind == 0 ) gtvdvb.size_ind_m = j; else gtvdvb.size_ind_p = j;
		}
		
	g_free ( text );
}
void gtv_combo_size_m ( GtkComboBox *combo_box )
{
	gtv_combo_size_all ( combo_box, gtvbase.toolbar_media, 0 );
}
void gtv_combo_size_p ( GtkComboBox *combo_box )
{
	gtv_combo_size_all ( combo_box, gtvbase.toolbar_sw, 1 );
}


static GtkBox * gtv_create_sgn_snr ()
{
    GtkBox *tbar_dvb = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_VERTICAL, 0 );
    gtk_widget_set_margin_start ( GTK_WIDGET ( tbar_dvb ), 5 );
    gtk_widget_set_margin_end   ( GTK_WIDGET ( tbar_dvb ), 5 );

    gtvdvb.signal_snr = (GtkLabel *)gtk_label_new ( gtvdvb.lv_snr );
    gtk_box_pack_start ( tbar_dvb, GTK_WIDGET ( gtvdvb.signal_snr  ), FALSE, FALSE, 5 );

    gtvdvb.bar_sgn = (GtkProgressBar *)gtk_progress_bar_new ();
    gtvdvb.bar_snr = (GtkProgressBar *)gtk_progress_bar_new ();
    
    gtk_box_pack_start ( tbar_dvb, GTK_WIDGET ( gtvdvb.bar_sgn  ), FALSE, FALSE, 0 );
    gtk_box_pack_start ( tbar_dvb, GTK_WIDGET ( gtvdvb.bar_snr  ), FALSE, FALSE, 3 );

    return tbar_dvb;
}

static void gtv_strat_hide ()
{
    gtv_sensitive ( FALSE, 0, gtk_toolbar_get_n_items ( GTK_TOOLBAR ( gtvbase.toolbar_media ) ) - 2 );
    gtk_widget_hide ( GTK_WIDGET ( gtvbase.combo_lang ) );
}

static void gtv_read_ch ()
{
    if ( g_file_test ( gtvbase.channels_conf, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_REGULAR ) )
        gtv_file_to_treeview ( gtvbase.channels_conf );
    else
        gtv_win_scan ();
}

static void gtv_auto_save ()
{
    gtv_save_config ( gtvbase.gtv_conf, gtvbase.main_window );

    gtv_treeview_to_file ( gtvbase.gtv_tree_view, gtvbase.channels_conf );
}

static void gtv_style_theme ()
{
    GtkCssProvider  *provider = gtk_css_provider_new ();
    GdkDisplay      *display  = gdk_display_get_default ();
    GdkScreen       *screen   = gdk_display_get_default_screen (display);

    gtk_style_context_add_provider_for_screen ( screen,
            GTK_STYLE_PROVIDER (provider), GTK_STYLE_PROVIDER_PRIORITY_USER );

    gtk_css_provider_load_from_data ( GTK_CSS_PROVIDER (provider), // GTK_CHECK_VERSION(3,20,0) GtkProgressBar:min-height
            "GtkProgressBar {\n"
            "   -GtkProgressBar-min-horizontal-bar-height: 8;\n"
            "}\n",
            -1, NULL );
}

static void gtv_init ( GtkApplication *app )
{
	gtv_style_theme ();
	
	gtvdvb.adapter  = 0;
	gtvdvb.frontend = 0;
	gtv_get_dvb_info ( FALSE, FALSE, FALSE, gtvdvb.adapter, gtvdvb.frontend );
	
    gchar *dir_conf = g_strdup_printf ( "%s/gtv", g_get_user_config_dir () );

    if ( !g_file_test ( dir_conf, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_DIR ) )
    {
        g_mkdir ( dir_conf, 0777 );
        g_print ( "Creating %s directory. \n", dir_conf );
    }

    g_free ( dir_conf );

    gtvbase.gtv_conf      = g_strconcat ( g_get_user_config_dir (), "/gtv/gtv.conf", NULL );
    gtvbase.channels_conf = g_strconcat ( g_get_user_config_dir (), "/gtv/gtv-channel.conf", NULL );

	gtvbase.group = g_simple_action_group_new ();
	gtv_create_gaction_entry ( app, gtvbase.group );
	
	gtvgstdvb.video_window_handle = 0;

	gtvpref.rec_dir       = g_strdup ( g_get_home_dir ()    );
    gtvpref.audio_encoder = g_strdup ( "vorbisenc"          );
    gtvpref.audio_enc_p   = g_strdup ( "int=bitrate=192000" );
    gtvpref.video_encoder = g_strdup ( "theoraenc"          );
    gtvpref.video_enc_p   = g_strdup ( "int=bitrate=2000"   );
    gtvpref.muxer         = g_strdup ( "oggmux"             );
    gtvpref.file_ext      = g_strdup ( "ogg"                );
    
    gtvpref.gtvpshed.sched = g_strdup ( "45=120=1" );
	gtvpref.gtvpshed.offset    = 45;
	gtvpref.gtvpshed.duration  = 120;
	gtvpref.gtvpshed.ch_number = 1;
    gtvpref.gtvpshed.sched_rec = TRUE;

	gtvdvb.lv_snr = "Level  &  Quality";
	gtvbase.ch_name = NULL;
	gtvbase.volume_set = 0.5;
	gtvbase.time_rec = 0;

	gtvdvb.sid = 0;
	gtvdvb.audio_ind   = 0;
	gtvdvb.count_audio = 0;
	gtvdvb.first_msg = TRUE;
	gtvdvb.video_enable  = TRUE;
	gtvdvb.rec_status    = TRUE;
	gtvdvb.rec_en_ts     = FALSE;;
	
	gtvservice.main_win_width  = 900;
	gtvservice.main_win_height = 400;
	gtvdvb.size_ind_m = 0;
	gtvdvb.size_ind_p = 0;


    if ( g_file_test ( gtvbase.gtv_conf, G_FILE_TEST_EXISTS ) )
        gtv_read_config ( gtvbase.gtv_conf );

    gtk_icon_theme_add_resource_path ( gtk_icon_theme_get_default (), "/gtv/res" );

    gtvdvb.gtv_logo = gtk_icon_theme_load_icon ( gtk_icon_theme_get_default (),
              "gtv-dvb", 48, GTK_ICON_LOOKUP_USE_BUILTIN, NULL );
	
	if ( gtvdvb.gtv_logo == NULL )
		gtvdvb.gtv_logo = gtk_icon_theme_load_icon ( gtk_icon_theme_get_default (),
			"applications-multimedia", 64, GTK_ICON_LOOKUP_USE_BUILTIN, NULL );
}

gboolean first_close = TRUE;
void gtv_quit ( /*GtkWindow *window*/ )
{
    if ( !first_close ) return;

    first_close = FALSE;
    gtv_auto_save ();
    gtv_stop ();

    gtk_widget_destroy ( GTK_WIDGET ( gtvbase.main_window ) );
}

static void gtv_win_base ( GtkApplication *app )
{
    gtv_init ( app );

    GtkDrawingArea *video_window;
    GtkBox *main_box, *main_hbox, *tool_hbox_sw;
    GtkListStore *liststore;
    GtkScrolledWindow *scrollwin;

    gtvbase.main_window = (GtkWindow *)gtk_application_window_new ( app );
    gtk_window_set_title ( gtvbase.main_window, "Gtv-Dvb" );
    gtk_window_set_default_size ( gtvbase.main_window, gtvservice.main_win_width, gtvservice.main_win_height );
    g_signal_connect ( gtvbase.main_window, "destroy", G_CALLBACK ( gtv_quit ), NULL );
    
    if ( gtvdvb.gtv_logo )
		gtk_window_set_icon ( gtvbase.main_window, gtvdvb.gtv_logo );
    else
		gtk_window_set_default_icon_name ( "display" );

    video_window = (GtkDrawingArea *)gtk_drawing_area_new ();
    g_signal_connect ( video_window, "realize", G_CALLBACK ( gtv_video_window_realize ), video_window );
    g_signal_connect ( video_window, "draw",    G_CALLBACK ( gtv_draw_callback ), NULL );

    GtkMenu *menu = gtv_create_menu ( gtvbase.group );

    gtk_widget_add_events ( GTK_WIDGET ( video_window ), GDK_BUTTON_PRESS_MASK );
    g_signal_connect ( video_window, "button-press-event", G_CALLBACK ( gtv_press_event ), menu );

    main_hbox   = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_HORIZONTAL, 0 );
    
    liststore   = (GtkListStore *)gtk_list_store_new ( 3, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING );
    gtvbase.gtv_tree_view = (GtkTreeView *)gtk_tree_view_new_with_model ( GTK_TREE_MODEL ( liststore ) );

	struct trw_columns trw_cols_n[] =
	{
		{ "Num",      TRUE  },
		{ "Channels", TRUE  },
		{ "Data",     FALSE }
	};

    scrollwin     = gtv_treeview ( gtvbase.gtv_tree_view, trw_cols_n, G_N_ELEMENTS ( trw_cols_n ) );
    gtvbase.toolbar_media = gtv_create_toolbar ( 0, 6  );
    gtvbase.toolbar_sw    = gtv_create_toolbar ( 6, 10 );

    gtvbase.volbutton = (GtkVolumeButton *)gtk_volume_button_new ();
    gtk_scale_button_set_value ( GTK_SCALE_BUTTON ( gtvbase.volbutton ), gtvbase.volume_set );
    g_signal_connect ( gtvbase.volbutton, "value-changed", G_CALLBACK ( gtv_volume_changed ), NULL );

    gtvbase.combo_lang = (GtkComboBoxText *)gtk_combo_box_text_new ();
    gtk_widget_set_size_request ( GTK_WIDGET ( gtvbase.combo_lang ), 190, -1 );
    gtvbase.combo_signal_id = g_signal_connect ( gtvbase.combo_lang, "changed", G_CALLBACK ( gtv_combo_lang ), NULL );

    tool_hbox_sw = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_HORIZONTAL, 0 );
    gtk_box_pack_start ( tool_hbox_sw, GTK_WIDGET ( gtvbase.combo_lang ), FALSE, FALSE, 5 );

    gtvbase.label_tv_rec = (GtkLabel *)gtk_label_new ( "" );

    gtvbase.tool_hbox = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_HORIZONTAL, 0 );
        gtk_box_pack_start ( gtvbase.tool_hbox, GTK_WIDGET ( gtvbase.toolbar_media ), TRUE,  TRUE,  0  );
        gtk_box_pack_end   ( gtvbase.tool_hbox, GTK_WIDGET ( gtvbase.volbutton   ),   FALSE, FALSE, 0  );

    g_signal_connect ( gtv_image_tool_button ( gtvbase.tool_hbox, "audio-volume-muted", 16, FALSE ), "clicked", G_CALLBACK ( gtv_mute ), NULL );
    gtk_box_pack_end   ( gtvbase.tool_hbox, GTK_WIDGET ( gtvbase.label_tv_rec ),  FALSE, FALSE, 10 );

    gtvbase.sw_vbox = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_VERTICAL, 0 );
    gtk_box_pack_start ( gtvbase.sw_vbox, GTK_WIDGET ( scrollwin ), TRUE, TRUE, 0 );
    gtk_box_pack_start ( gtvbase.sw_vbox, GTK_WIDGET ( gtv_create_sgn_snr () ), FALSE, FALSE, 0 );
    gtk_box_pack_start ( gtvbase.sw_vbox, GTK_WIDGET ( gtvbase.toolbar_sw ),   FALSE, FALSE, 0 );
    gtk_box_pack_start ( gtvbase.sw_vbox, GTK_WIDGET ( tool_hbox_sw ), FALSE, FALSE, 0 );
    gtk_widget_set_size_request ( GTK_WIDGET ( gtvbase.sw_vbox ), 200, -1 );

    GtkPaned *hpaned = (GtkPaned *)gtk_paned_new ( GTK_ORIENTATION_HORIZONTAL );
    gtk_paned_add1 ( hpaned, GTK_WIDGET ( gtvbase.sw_vbox ) );
    gtk_paned_add2 ( hpaned, GTK_WIDGET ( video_window ) );
        gtk_box_pack_start ( main_hbox, GTK_WIDGET ( hpaned ), TRUE, TRUE, 0 );

    main_box = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_VERTICAL, 0 );
        gtk_box_pack_start ( main_box, GTK_WIDGET ( main_hbox ), TRUE,  TRUE,  0 );
        gtk_box_pack_start ( main_box, GTK_WIDGET ( gtvbase.tool_hbox ), FALSE, FALSE, 0 );
    gtk_container_add ( GTK_CONTAINER ( gtvbase.main_window ), GTK_WIDGET ( main_box ) );

    gtk_widget_realize  ( GTK_WIDGET ( video_window ) );

    gtk_widget_show_all ( GTK_WIDGET ( gtvbase.main_window  ) );
    gtv_strat_hide ();

    gtv_read_ch ();
}

static void gtv_activate ( GtkApplication *app )
{
    gtv_win_base ( app );
}

static void gtv_set_locale ()
{
    setlocale ( LC_ALL, "" );
    bindtextdomain ( "gtv-dvb", "/usr/share/locale/" );
    textdomain ( "gtv-dvb" );
}

int main ()
{
    gst_init ( NULL, NULL );

    if ( !gtv_gst_create () ) return -1;

    gtv_set_locale ();

    GtkApplication *app = gtk_application_new ( NULL, G_APPLICATION_FLAGS_NONE );
    g_signal_connect ( app, "activate", G_CALLBACK ( gtv_activate ),  NULL );

    int status = g_application_run ( G_APPLICATION ( app ), 0, NULL );
    g_object_unref ( app );

    return status;
}
