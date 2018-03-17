/*
 * Copyright 2014 - 2018 Stepan Perun
 * This program is free software.
 * License: Gnu Lesser General Public License
 * http://www.gnu.org/licenses/lgpl.html
*/


#include <stdlib.h>
#include <time.h>
#include <glib/gi18n.h>

#include "gtv-pref.h"
#include "gtv-dvb.h"
#include "gtv-service.h"


struct GtvPrefData
{
	GtkEntry *entry_enc[6], *entry_rec;
	GstElementFactory *element_find;
	
	gboolean gtv_pref_win;
	guint shed_time_rec;
};

struct GtvPrefData gtvprefdata;


static guint c = 0;


static gchar * gtv_get_prop ( const gchar *prop )
{
    gchar *name = NULL;
        g_object_get ( gtk_settings_get_default (), prop, &name, NULL );
    return name;
}

static void gtv_set_prop ( const gchar *prop, gchar *path )
{
    gchar *i_file = g_strconcat ( path, "/index.theme", NULL );

        if ( g_file_test ( i_file, G_FILE_TEST_EXISTS ) )
        {
            gchar *name = g_path_get_basename ( path );
                g_object_set ( gtk_settings_get_default (), prop, name, NULL );
            g_free ( name );
        }

    g_free ( i_file );
}

static void gtv_set_theme ( GtkEntry *entry )
{
    gchar *path = gtv_open_dir ( "/usr/share/themes" );

        if ( path )
        {
            gtv_set_prop ( "gtk-theme-name", path );

            gchar *name = g_path_get_basename ( path );
                gtk_entry_set_text ( entry, name );
            g_free ( name );
        }

    g_free ( path );
}

static void gtv_set_icon ( GtkEntry *entry )
{
    gchar *path = gtv_open_dir ( "/usr/share/icons" );

        if ( path )
        {
            gtv_set_prop ( "gtk-icon-theme-name", path );

            gchar *name = g_path_get_basename ( path );
                gtk_entry_set_text ( entry, name );
            g_free ( name );
        }

    g_free ( path );
}

static void gtv_set_rec_dir ( GtkEntry *entry )
{
    g_free ( gtvpref.rec_dir ); gtvpref.rec_dir = gtv_open_dir ( g_get_home_dir () );
    if ( gtvpref.rec_dir ) gtk_entry_set_text ( entry, gtvpref.rec_dir );
}


static gboolean gtv_schedule_stop_rec ()
{
	if ( gtvpref.gtvpshed.sched_rec )
		return FALSE;
	
	gtv_rec_stop_schedule ();
	gtvpref.gtvpshed.sched_rec = TRUE;
	
	if ( gtvprefdata.gtv_pref_win )
	{
		gtk_widget_set_sensitive ( GTK_WIDGET ( gtvprefdata.entry_rec ), gtvpref.gtvpshed.sched_rec );
		g_object_set ( gtvprefdata.entry_rec, "editable", gtvpref.gtvpshed.sched_rec, NULL );

		gtk_entry_set_icon_from_icon_name ( gtvprefdata.entry_rec, 
			GTK_ENTRY_ICON_SECONDARY, gtvpref.gtvpshed.sched_rec ? "media-record" : "media-playback-stop" );		
	}
	
	return FALSE;
}
static gboolean gtv_schedule_start_rec ()
{
	gint64 sched_stop_rec  = gtvpref.gtvpshed.duration * 60;
	
	if ( gtv_rec_schedule () )
	{		
		if ( gtvprefdata.gtv_pref_win )
			gtk_widget_set_sensitive ( GTK_WIDGET ( gtvprefdata.entry_rec ), gtvpref.gtvpshed.sched_rec );

		gtvpref.gtvpshed.sched_rec = FALSE;
		g_timeout_add_seconds ( sched_stop_rec, (GSourceFunc)gtv_schedule_stop_rec, NULL );
		gtvprefdata.shed_time_rec = 0;	
	}
	
	return FALSE;
}

static void gtv_apply_schedule ()
{
	if ( gtvpref.gtvpshed.offset < 1 || gtvpref.gtvpshed.duration < 1 )
		return;
	
	gint64 sched_start_rec = gtvpref.gtvpshed.offset * 60;
	
	if ( gtvpref.gtvpshed.sched_rec )
		gtvprefdata.shed_time_rec = g_timeout_add_seconds ( sched_start_rec, (GSourceFunc)gtv_schedule_start_rec, NULL );
	else
	{
		g_source_remove ( gtvprefdata.shed_time_rec );
		gtvprefdata.shed_time_rec = 0;
	}
		
	gtvpref.gtvpshed.sched_rec = !gtvpref.gtvpshed.sched_rec;
	gtk_entry_set_icon_from_icon_name ( GTK_ENTRY ( gtvprefdata.entry_rec ), 
		GTK_ENTRY_ICON_SECONDARY, gtvpref.gtvpshed.sched_rec ? "media-record" : "media-playback-stop" );
	
	g_object_set ( gtvprefdata.entry_rec, "editable", gtvpref.gtvpshed.sched_rec, NULL );
}

static void gtv_set_schedule ( GtkEntry *entry )
{
    const gchar *text = gtk_entry_get_text ( entry );

	if ( !g_strrstr ( text, "=" ) ) return;

    gchar **splits = g_strsplit ( text, "=", 0 );
    guint numsplits = g_strv_length ( splits );

    if ( numsplits == 3 )
    {
		gtvpref.gtvpshed.offset    = atoi ( splits[0] );
		gtvpref.gtvpshed.duration  = atoi ( splits[1] );
		gtvpref.gtvpshed.ch_number = atoi ( splits[2] );
			
		g_debug ( "Offset=%d  Duration=%d  Number=%d\n", atoi ( splits[0] ), atoi ( splits[1] ), atoi ( splits[2] ) );
    }
    else
		g_debug ( "Not set:  numsplits != 3 \n" );

    g_strfreev ( splits );

	g_free ( gtvpref.gtvpshed.sched );
	gtvpref.gtvpshed.sched = g_strdup ( text );
}

static gboolean gtv_check_enc ( GtkEntry *entry )
{
    const gchar *text = gtk_entry_get_text ( entry );

    if ( text != NULL && *text != '\0' )
    {
        gtvprefdata.element_find = gst_element_factory_find ( text );

        if ( gtvprefdata.element_find )
            gtk_entry_set_icon_from_icon_name ( GTK_ENTRY ( entry ), GTK_ENTRY_ICON_SECONDARY, "dialog-yes" );
        else
            gtk_entry_set_icon_from_icon_name ( GTK_ENTRY ( entry ), GTK_ENTRY_ICON_SECONDARY, "dialog-error" );
    }
    else
        return FALSE;

    return ( gtvprefdata.element_find ) ? TRUE : FALSE;
}

static void gtv_check_enc_prop ( GtkEntry *entry )
{
    const gchar *text = gtk_entry_get_text ( entry );
    
    gtk_entry_set_icon_from_icon_name ( GTK_ENTRY ( entry ), GTK_ENTRY_ICON_SECONDARY, "info" );
    gtk_entry_set_icon_tooltip_text ( GTK_ENTRY ( entry ), GTK_ENTRY_ICON_SECONDARY, 
		_("Type=Property=Value[space]\n    Type[int|double|bool|char]\nExample:\n    int=bitrate=2048000 bool=dct8x8=true char=option-string=...") );

    if ( g_str_has_prefix ( text, " " ) || g_strrstr ( text, " =" ) || g_strrstr ( text, "= " ) || g_strrstr ( text, "  " ) )
        gtk_entry_set_icon_from_icon_name ( GTK_ENTRY ( entry ), GTK_ENTRY_ICON_SECONDARY, "dialog-error" );
}

static void gtv_set_rec_p_venc ( GtkEntry *entry )
{
    gtv_check_enc_prop ( entry );

    g_free ( gtvpref.video_enc_p );
    gtvpref.video_enc_p = g_strdup ( gtk_entry_get_text ( entry ) );
}

static void gtv_set_rec_p_aenc ( GtkEntry *entry )
{
    gtv_check_enc_prop ( entry );

    g_free ( gtvpref.audio_enc_p );
    gtvpref.audio_enc_p = g_strdup ( gtk_entry_get_text ( entry ) );
}

static void gtv_set_rec_data_venc ( GtkEntry *entry )
{
    if ( !gtv_check_enc ( entry ) ) return;

    g_free ( gtvpref.video_encoder );
    gtvpref.video_encoder = g_strdup ( gtk_entry_get_text ( entry ) );
}

static void gtv_set_rec_data_aenc ( GtkEntry *entry )
{
    if ( !gtv_check_enc ( entry ) ) return;

    g_free ( gtvpref.audio_encoder );
    gtvpref.audio_encoder = g_strdup ( gtk_entry_get_text ( entry ) );
}

static void gtv_set_rec_data_mux ( GtkEntry *entry )
{
    if ( !gtv_check_enc ( entry ) ) return;

    g_free ( gtvpref.muxer );
    gtvpref.muxer = g_strdup ( gtk_entry_get_text ( entry ) );
}

static void gtv_set_rec_data_ext ( GtkEntry *entry )
{
    g_free ( gtvpref.file_ext );
    gtvpref.file_ext = g_strdup ( gtk_entry_get_text ( entry ) );
}

static void gtv_changed_sw ( GtkSwitch *switch_p )
{
    gboolean statte = gtk_switch_get_state ( switch_p );

    gtvdvb.rec_en_ts = statte;

    for ( c = 0; c < 6; c++ )
        gtk_widget_set_sensitive ( GTK_WIDGET ( gtvprefdata.entry_enc[c] ), gtvdvb.rec_en_ts );
}

static GtkBox * gtv_scan_pref ()
{
    GtkBox *g_box  = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_VERTICAL, 0 );
    gtk_widget_set_margin_start ( GTK_WIDGET ( g_box ), 10 );
    gtk_widget_set_margin_end   ( GTK_WIDGET ( g_box ), 10 );

    GtkGrid *grid = (GtkGrid *)gtk_grid_new();
    gtk_grid_set_column_homogeneous ( GTK_GRID ( grid ), TRUE );
    gtk_box_pack_start ( g_box, GTK_WIDGET ( grid ), TRUE, TRUE, 10 );
    
    gchar *theme_n = gtv_get_prop ( "gtk-theme-name" );
    gchar *icon_n  = gtv_get_prop ( "gtk-icon-theme-name" );

    struct data_a { GtkWidget *label; const gchar *ltext; GtkWidget *widget; const gchar *etext; void (* activate); gboolean icon_set; }
    data_a_n[] =
    {
        { gtk_label_new ( "" ), N_("Theme"),            gtk_entry_new  (), theme_n, gtv_set_theme, TRUE  },
        { gtk_label_new ( "" ), N_("Icons"),       		gtk_entry_new  (), icon_n,  gtv_set_icon,  TRUE  },
		
		{ gtk_label_new ( "" ), N_("↳ Size ( media )"), gtk_combo_box_text_new (), NULL, gtv_combo_size_m, FALSE  },
		{ gtk_label_new ( "" ), N_("↳ Size ( panel )"), gtk_combo_box_text_new (), NULL, gtv_combo_size_p, FALSE  },

        { gtk_label_new ( "" ), NULL,                   NULL,              NULL,          NULL,           FALSE },
        { gtk_label_new ( "" ), N_("Recording folder"), gtk_entry_new  (), gtvpref.rec_dir,        gtv_set_rec_dir,  TRUE  },
        { gtk_label_new ( "" ), N_("Timer recording"),  gtk_entry_new  (), gtvpref.gtvpshed.sched, gtv_set_schedule, FALSE },
        { gtk_label_new ( "" ), NULL,                   NULL,              NULL,          NULL,           FALSE },

        { gtk_label_new ( "" ), N_("TS / Encoder"),     gtk_switch_new (), NULL,          		  gtv_changed_sw,        FALSE },

        { gtk_label_new ( "" ), N_("Audio encoder"),    gtk_entry_new  (), gtvpref.audio_encoder, gtv_set_rec_data_aenc, FALSE },
        { gtk_label_new ( "" ), N_("↳ Properties"),   	gtk_entry_new  (), gtvpref.audio_enc_p,   gtv_set_rec_p_aenc,    FALSE },

        { gtk_label_new ( "" ), N_("Video encoder"),    gtk_entry_new  (), gtvpref.video_encoder, gtv_set_rec_data_venc, FALSE },
        { gtk_label_new ( "" ), N_("↳ Properties"),   	gtk_entry_new  (), gtvpref.video_enc_p,   gtv_set_rec_p_venc,    FALSE },

        { gtk_label_new ( "" ), N_("Muxer"),            gtk_entry_new  (), gtvpref.muxer,         gtv_set_rec_data_mux,  FALSE },
        { gtk_label_new ( "" ), N_("File extension"),   gtk_entry_new  (), gtvpref.file_ext,      gtv_set_rec_data_ext,  FALSE }
    };

    guint d = 0, z = 0;
    for ( d = 0; d < G_N_ELEMENTS ( data_a_n ); d++ )
    {
        gtk_label_set_label ( GTK_LABEL ( data_a_n[d].label ), _(data_a_n[d].ltext) );
        gtk_widget_set_halign ( GTK_WIDGET ( data_a_n[d].label ), GTK_ALIGN_START );
        gtk_grid_attach ( GTK_GRID ( grid ), GTK_WIDGET ( data_a_n[d].label ), 0, d, 1, 1 );

        if ( !data_a_n[d].widget ) continue;

        if ( d == 8 )
        {
            gtk_grid_attach ( GTK_GRID ( grid ), GTK_WIDGET ( data_a_n[d].widget ), 1, d, 1, 1 );
            gtk_switch_set_state ( GTK_SWITCH ( data_a_n[d].widget ), gtvdvb.rec_en_ts );
            g_signal_connect ( data_a_n[d].widget, "notify::active", G_CALLBACK ( data_a_n[d].activate ), NULL );
            continue;
        }
        
        if ( d == 2 || d == 3 )
			gtv_combo_size_add ( GTK_COMBO_BOX_TEXT ( data_a_n[d].widget ), d );
		else
			gtk_entry_set_text ( GTK_ENTRY ( data_a_n[d].widget ), data_a_n[d].etext );

		if ( d == 10 || d == 12 ) gtv_check_enc_prop ( GTK_ENTRY ( data_a_n[d].widget ) );
		if ( d == 9 || d == 11 || d == 13 ) gtv_check_enc ( GTK_ENTRY ( data_a_n[d].widget ) );

        gtk_grid_attach ( GTK_GRID ( grid ), GTK_WIDGET ( data_a_n[d].widget ), 1, d, 1, 1 );
        
        if ( d > 8 ) gtvprefdata.entry_enc[z++] = GTK_ENTRY ( data_a_n[d].widget );

        if ( d == 6 )
        {
			gtk_entry_set_icon_from_icon_name ( GTK_ENTRY ( data_a_n[d].widget ), 
				GTK_ENTRY_ICON_SECONDARY, gtvpref.gtvpshed.sched_rec ? "media-record" : "media-playback-stop" );
			
            gtk_entry_set_icon_from_icon_name ( GTK_ENTRY ( data_a_n[d].widget ), GTK_ENTRY_ICON_PRIMARY, "info" );
            gtk_entry_set_icon_tooltip_text ( GTK_ENTRY ( data_a_n[d].widget ), GTK_ENTRY_ICON_PRIMARY, 
				_("Time=Duration=Number\n    Offset[Minutes]\n    Duration[Minutes]\n    Number[Сhannel number]") );
			
			g_signal_connect ( data_a_n[d].widget, "icon-press", G_CALLBACK ( gtv_apply_schedule ), NULL );
			g_object_set ( data_a_n[d].widget, "editable", gtvpref.gtvpshed.sched_rec, NULL );
						
			if ( gtvprefdata.shed_time_rec == 0 )
				gtk_widget_set_sensitive ( GTK_WIDGET ( data_a_n[d].widget ), gtvpref.gtvpshed.sched_rec );
			
			gtvprefdata.entry_rec = GTK_ENTRY ( data_a_n[d].widget );
        }

        if ( data_a_n[d].icon_set )
        {
            g_object_set ( data_a_n[d].widget, "editable", FALSE, NULL );
            gtk_entry_set_icon_from_icon_name ( GTK_ENTRY ( data_a_n[d].widget ), GTK_ENTRY_ICON_SECONDARY, "document-open" );
            g_signal_connect ( data_a_n[d].widget, "icon-press", G_CALLBACK ( data_a_n[d].activate ), NULL );
        }
        else
			g_signal_connect ( data_a_n[d].widget, "changed", G_CALLBACK ( data_a_n[d].activate ), NULL );			
    }

    for ( c = 0; c < 6; c++ )
        gtk_widget_set_sensitive ( GTK_WIDGET ( gtvprefdata.entry_enc[c] ), gtvdvb.rec_en_ts );

	
    g_free ( theme_n );
    g_free ( icon_n );

    return g_box;
}

static void gtv_pref_quit ( GtkWindow *window )
{
	gtvprefdata.gtv_pref_win = FALSE;
    gtk_widget_destroy ( GTK_WIDGET ( window ) );
}

static void gtv_pref_close ( GtkButton *button, GtkWindow *window )
{
    gtv_pref_quit ( window );
}

void gtv_win_pref ()
{
	gtvprefdata.gtv_pref_win = FALSE;
	
    GtkWindow *window =      (GtkWindow *)gtk_window_new ( GTK_WINDOW_TOPLEVEL );
    gtk_window_set_modal     ( window, TRUE );
    gtk_window_set_position  ( window, GTK_WIN_POS_CENTER );
    gtk_window_set_title     ( window, _("Preferences") );
	g_signal_connect         ( window, "destroy", G_CALLBACK ( gtv_pref_quit ), NULL );

	gtk_window_set_type_hint ( GTK_WINDOW (window), GDK_WINDOW_TYPE_HINT_UTILITY );

    GtkBox *m_box = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_VERTICAL,   0 );
    GtkBox *h_box = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_HORIZONTAL, 0 );

    GtkNotebook *notebook = (GtkNotebook *)gtk_notebook_new ();
    gtk_notebook_set_scrollable ( notebook, TRUE );

    GtkBox *m_box_n = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_VERTICAL, 0 );
    gtk_box_pack_start ( m_box_n, GTK_WIDGET ( gtv_scan_pref () ), TRUE, TRUE, 0 );
    gtk_notebook_append_page ( notebook, GTK_WIDGET ( m_box_n ),  gtk_label_new ( _("Preferences") ) );

    gtk_notebook_set_tab_pos ( notebook, GTK_POS_TOP );
    gtk_box_pack_start ( m_box, GTK_WIDGET (notebook), TRUE, TRUE, 0 );

    GtkButton *button_close = (GtkButton *)gtk_button_new_from_icon_name ( "window-close", GTK_ICON_SIZE_SMALL_TOOLBAR );
    g_signal_connect ( button_close, "clicked", G_CALLBACK ( gtv_pref_close ), window );
    gtk_box_pack_end ( h_box, GTK_WIDGET ( button_close ), FALSE, FALSE, 5 );

    gtk_box_pack_start ( m_box, GTK_WIDGET ( h_box ), FALSE, FALSE, 5 );
    gtk_container_set_border_width ( GTK_CONTAINER ( m_box ), 5 );
    gtk_container_add ( GTK_CONTAINER ( window ), GTK_WIDGET ( m_box ) );

    gtk_widget_show_all ( GTK_WIDGET ( window ) );
    
    gtvprefdata.gtv_pref_win = TRUE;
}
