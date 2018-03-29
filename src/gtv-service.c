/*
 * Copyright 2014 - 2018 Stepan Perun
 * This program is free software.
 * License: Gnu Lesser General Public License
 * http://www.gnu.org/licenses/lgpl.html
*/


#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <glib/gi18n.h>

#include "gtv-service.h"
#include "gtv-dvb.h"
#include "gtv-treeview.h"


void gtv_win_about ( GtkWindow *window )
{
    GtkAboutDialog *dialog = (GtkAboutDialog *)gtk_about_dialog_new ();
    gtk_window_set_transient_for ( (GtkWindow *)dialog, window );

    const gchar *authors[] = { "Stepan Perun", NULL };

    const gchar *license = "GNU Lesser General Public License \nwww.gnu.org/licenses/lgpl.html";

    gtk_about_dialog_set_program_name ( dialog, " Gtv-Dvb " );
    gtk_about_dialog_set_version ( dialog, "1.1.9" );
    gtk_about_dialog_set_license ( dialog, license );
    gtk_about_dialog_set_authors ( dialog, authors );
    gtk_about_dialog_set_website ( dialog,   "https://github.com/vl-nix/gtv-dvb" );
    gtk_about_dialog_set_copyright ( dialog, "Copyright 2014 - 2018 Gtv-Dvb" );
    gtk_about_dialog_set_comments  ( dialog, "Digital TV player \nDVB-T2/S2/C, ATSC, DTMB" );

    if ( gtvdvb.gtv_logo != NULL )
        gtk_about_dialog_set_logo ( dialog, gtvdvb.gtv_logo );
    else
        gtk_about_dialog_set_logo_icon_name ( dialog, "applications-multimedia" );

    gtk_dialog_run ( GTK_DIALOG (dialog) );

    gtk_widget_destroy ( GTK_WIDGET (dialog) );
}

void gtv_message_dialog ( const gchar *f_error, const gchar *file_or_info, GtkMessageType mesg_type )
{
    GtkMessageDialog *dialog = ( GtkMessageDialog *)gtk_message_dialog_new (
                                 NULL, GTK_DIALOG_MODAL,
                                 mesg_type,   GTK_BUTTONS_CLOSE,
                                 "%s\n%s",    f_error, file_or_info );

    gtk_dialog_run     ( GTK_DIALOG ( dialog ) );
    gtk_widget_destroy ( GTK_WIDGET ( dialog ) );
}

void gtv_set_sgn_snr ( GstElement *element, GtkLabel *label, GtkProgressBar *barsgn, GtkProgressBar *barsnr, gdouble sgl, gdouble snr, gboolean hlook )
{
    gtk_progress_bar_set_fraction ( barsgn, sgl/100 );
    gtk_progress_bar_set_fraction ( barsnr, snr/100 );

    gchar *texta = g_strdup_printf ( "Level %d%s", (int)sgl, "%" );
    gchar *textb = g_strdup_printf ( "Snr %d%s",   (int)snr, "%" );

    const gchar *format = NULL;

    gboolean play = TRUE;
    if ( GST_ELEMENT_CAST ( element )->current_state != GST_STATE_PLAYING ) play = FALSE;

        if ( hlook )
            format = "<span>\%s</span><span foreground=\"#00ff00\"> ◉ </span><span>\%s</span>";
        else
            format = "<span>\%s</span><span foreground=\"#ff0000\"> ◉ </span><span>\%s</span>";

        if ( !play )
            format = "<span>\%s</span><span foreground=\"#ff8000\"> ◉ </span><span>\%s</span>";

        gchar *markup = g_markup_printf_escaped ( format, texta, textb );
            gtk_label_set_markup ( label, markup );
        g_free ( markup );

    g_free ( texta );
    g_free ( textb );
}

gchar * gtv_get_time_date_str ()
{
    GDateTime *datetime = g_date_time_new_now_local ();

    gint doy = g_date_time_get_day_of_year ( datetime );
    gint tth = g_date_time_get_hour   ( datetime );
    gint ttm = g_date_time_get_minute ( datetime );
    gint tts = g_date_time_get_second ( datetime );
    
    gchar *dt = g_strdup_printf ( "%d-%d-%d-%d", doy, tth, ttm, tts );
    
    g_date_time_unref ( datetime );

    return dt;
}

gdouble gtv_str_to_double ( gchar *text )
{
    if ( !g_strrstr ( text, "." ) ) return 0;

    gdouble res = 0;

        gchar **splits = g_strsplit ( text, ".", 0 );

            int len = strlen ( splits[1] );
            int exw = (int)pow ( 10, len );

            gdouble data_a = atoi ( splits[0] );
            gdouble data_b = atoi ( splits[1] );

            res = data_a + ( data_b / exw );

            g_debug ( "gtv_str_to_double: str %s | double %f \n", text, res );

        g_strfreev ( splits );

    return res;
}

GtkImage * gtv_greate_icon_widget ( const gchar *icon_name, gint isz )
{
    GtkImage *icon_widget = ( GtkImage * )gtk_image_new_from_icon_name ( icon_name, GTK_ICON_SIZE_MENU );
    gtk_image_set_pixel_size ( icon_widget, isz );

    return icon_widget;
}
GtkToolItem * gtv_image_tool_button ( GtkBox *h_box, const gchar *icon_name, gint isz, gboolean start_end )
{
    GtkToolItem *item = gtk_tool_button_new ( GTK_WIDGET ( gtv_greate_icon_widget ( icon_name, isz ) ), NULL );

    if ( start_end )
        gtk_box_pack_start ( h_box, GTK_WIDGET ( item ), FALSE, FALSE, 0 );
    else
        gtk_box_pack_end   ( h_box, GTK_WIDGET ( item ), FALSE, FALSE, 0 );

    return item;
}

GtkButton * gtv_image_button ( GtkBox *box, const gchar *icon_name, gboolean start_end )
{
	GtkButton *button = (GtkButton *)gtk_button_new_from_icon_name ( icon_name, GTK_ICON_SIZE_SMALL_TOOLBAR );

    if ( start_end )
        gtk_box_pack_start ( box, GTK_WIDGET ( button ), FALSE, FALSE, 0 );
    else
        gtk_box_pack_end   ( box, GTK_WIDGET ( button ), FALSE, FALSE, 0 );

    return button;
}

GtkScale * gtv_scale ( GtkBox *box, gdouble opacity )
{
    GtkScale *scale = (GtkScale *)gtk_scale_new_with_range ( GTK_ORIENTATION_HORIZONTAL, 0.4, 1.0, 0.01 );

    gtk_range_set_value ( GTK_RANGE ( scale ), opacity );
    gtk_scale_set_draw_value ( GTK_SCALE ( scale ), 0 );
    gtk_widget_set_size_request ( GTK_WIDGET ( scale ), 100, -1 );

    gtk_box_pack_start ( box, GTK_WIDGET ( scale ), FALSE, FALSE, 0 );

    return scale;
}


gchar * gtv_open_dir ( const gchar *path )
{
    GtkFileChooserDialog *dialog = ( GtkFileChooserDialog *)gtk_file_chooser_dialog_new (
                    _("Choose Folder"),  NULL, GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
                    "gtk-cancel", GTK_RESPONSE_CANCEL,
                    "gtk-apply",  GTK_RESPONSE_ACCEPT,
                     NULL );

    gtk_file_chooser_set_current_folder ( GTK_FILE_CHOOSER ( dialog ), path );

    gchar *dirname = NULL;

    if ( gtk_dialog_run ( GTK_DIALOG ( dialog ) ) == GTK_RESPONSE_ACCEPT )
        dirname = gtk_file_chooser_get_filename ( GTK_FILE_CHOOSER ( dialog ) );

    gtk_widget_destroy ( GTK_WIDGET ( dialog ) );

    return dirname;
}

static void gtv_add_filter ( GtkFileChooserDialog *dialog, gchar *name, gchar *filter )
{
    GtkFileFilter *filter_video = gtk_file_filter_new ();
    gtk_file_filter_set_name ( filter_video, name );
    gtk_file_filter_add_pattern ( filter_video, filter );
    gtk_file_chooser_add_filter ( GTK_FILE_CHOOSER ( dialog ), filter_video );
}

gchar * gtv_open_file ( const gchar *path )
{
    GtkFileChooserDialog *dialog = ( GtkFileChooserDialog *)gtk_file_chooser_dialog_new (
                    _("Open File"),  NULL, GTK_FILE_CHOOSER_ACTION_OPEN,
                    "gtk-cancel", GTK_RESPONSE_CANCEL,
                    "gtk-open",   GTK_RESPONSE_ACCEPT,
                     NULL );
	
	gtv_add_filter ( dialog, "conf", "*.conf" );
	
    gtk_file_chooser_set_current_folder  ( GTK_FILE_CHOOSER ( dialog ), path  );
    gtk_file_chooser_set_select_multiple ( GTK_FILE_CHOOSER ( dialog ), FALSE );

    gchar *filename = NULL;

    if ( gtk_dialog_run ( GTK_DIALOG ( dialog ) ) == GTK_RESPONSE_ACCEPT )
        filename = gtk_file_chooser_get_filename ( GTK_FILE_CHOOSER ( dialog ) );

    gtk_widget_destroy ( GTK_WIDGET ( dialog ) );

    return filename;
}

void gtv_file_to_treeview ( gchar *filename )
{
    guint n = 0;
    gchar *contents;
    GError *err = NULL;

    if ( g_file_get_contents ( filename, &contents, 0, &err ) )
    {
        gchar **lines = g_strsplit ( contents, "\n", 0 );

        for ( n = 0; lines[n] != NULL; n++ )
            if ( *lines[n] )
                gtv_str_split_ch_data ( lines[n] );

        g_strfreev ( lines );
        g_free ( contents );
    }
    else
    {
        g_critical ( "gtv_file_to_treeview:: %s\n", err->message );
		g_error_free ( err );
	}
}

void gtv_treeview_to_file ( GtkTreeView *tree_view, gchar *filename )
{
    GString *gstring = g_string_new ( "# Gtv-Dvb channel format \n" );

    GtkTreeIter iter;
    GtkTreeModel *model = gtk_tree_view_get_model ( GTK_TREE_VIEW ( tree_view ) );

    gboolean valid;
    for ( valid = gtk_tree_model_get_iter_first ( model, &iter ); valid;
          valid = gtk_tree_model_iter_next ( model, &iter ) )
    {
        gchar *data = NULL;

            gtk_tree_model_get ( model, &iter, COL_DATA, &data, -1 );

            gstring = g_string_append ( gstring, data );
            gstring = g_string_append ( gstring, "\n" );

        g_free ( data );
    }

    if ( !g_file_set_contents ( filename, gstring->str, -1, NULL ) )
        g_critical ( "gtv_treeview_to_file:: Save file %s failed.\n", filename );

    g_string_free ( gstring, TRUE );
}

static gchar * gtv_get_service_prop ( const gchar *prop )
{
    gchar *name = NULL;
        g_object_get ( gtk_settings_get_default (), prop, &name, NULL );
    return name;
}

void gtv_read_config ( gchar *file )
{
    guint n = 0;
    gchar *contents;
    GError *err = NULL;

    if ( g_file_get_contents ( file, &contents, 0, &err ) )
    {
        gchar **lines = g_strsplit ( contents, "\n", 0 );

        for ( n = 0; lines[n] != NULL; n++ )
        {
            if ( !g_strrstr ( lines[n], "=" ) ) continue;

            gchar **key_val = g_strsplit ( lines[n], "=", 0 );

                if ( g_strrstr ( lines[n], "theme" ) )
                    g_object_set ( gtk_settings_get_default (), key_val[0], key_val[1], NULL );

                if ( g_strrstr ( lines[n], "main-win-width" ) )
                    gtvservice.main_win_width = atoi ( key_val[1] );

                if ( g_strrstr ( lines[n], "main-win-height" ) )
                    gtvservice.main_win_height = atoi ( key_val[1] );

                g_debug ( "gtv_read_config:: Set %s -> %s \n", key_val[0], key_val[1]);

            g_strfreev ( key_val );
        }

        g_strfreev ( lines );
        g_free ( contents );
    }
    else
    {
        g_critical ( "gtv_read_config:: %s\n", err->message );
        g_error_free ( err );
	}
	
    if ( gtvservice.main_win_width  < 400 ) gtvservice.main_win_width  = 900;
    if ( gtvservice.main_win_height < 200 ) gtvservice.main_win_height = 400;
}

void gtv_save_config ( gchar *file, GtkWindow *window )
{
    gchar *gtv_conf_t = gtv_get_service_prop ( "gtk-theme-name" );
    gchar *gtv_conf_i = gtv_get_service_prop ( "gtk-icon-theme-name" );

    gint width  = gtk_widget_get_allocated_width  ( GTK_WIDGET ( window ) );
    gint height = gtk_widget_get_allocated_height ( GTK_WIDGET ( window ) );

    GString *gstring = g_string_new ( "# Gtv-Dvb conf \n" );

        g_string_append_printf ( gstring, "gtk-theme-name=%s\n",      gtv_conf_t );
        g_string_append_printf ( gstring, "gtk-icon-theme-name=%s\n", gtv_conf_i );

        g_string_append_printf ( gstring, "main-win-width=%d\n",  width  );
        g_string_append_printf ( gstring, "main-win-height=%d\n", height );

        if ( !g_file_set_contents ( file, gstring->str, -1, NULL ) )
            g_critical ( "gtv_save_config:: Save file %s failed.\n", file );

    g_string_free ( gstring, TRUE );

    g_free ( gtv_conf_i );
    g_free ( gtv_conf_t );
}
