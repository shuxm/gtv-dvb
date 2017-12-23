/*
 * Copyright 2014 - 2018 Stepan Perun
 * This program is free software.
 * License: Gnu Lesser General Public License
 * http://www.gnu.org/licenses/lgpl.html
*/


#ifndef GTV_SERVICE_H
#define GTV_SERVICE_H


#include <gtk/gtk.h>
#include <gst/gst.h>


struct GtvService
{
	guint main_win_width;
	guint main_win_height;
};

struct GtvService gtvservice;

gchar * gtv_open_dir  ( const gchar *path );
gchar * gtv_open_file ( const gchar *path );
gchar * gtv_get_time_date_str ();
gdouble gtv_str_to_double ( gchar *text );

void gtv_file_to_treeview ( gchar *filename );
void gtv_treeview_to_file ( GtkTreeView *tree_view, gchar *filename );

void gtv_read_config ( gchar *file );
void gtv_save_config ( gchar *file, GtkWindow *window );

void gtv_win_about ( GtkWindow *window );
void gtv_message_dialog ( const gchar *f_error, const gchar *file_or_info, GtkMessageType mesg_type );
void gtv_set_sgn_snr ( GstElement *element, GtkLabel *label, GtkProgressBar *barsgn, GtkProgressBar *barsnr, gdouble sgl, gdouble snr, gboolean hlook );

//GtkImage    * gtv_greate_icon_widget ( const gchar *icon_name, gint isz );
GtkToolItem * gtv_image_tool_button ( GtkBox *h_box, const gchar *icon_name, gint isz, gboolean start_end );
GtkButton   * gtv_image_button ( GtkBox *box, const gchar *icon_name, gboolean start_end );
GtkScale    * gtv_scale ( GtkBox *h_box, gdouble opacity );


#endif // GTV_SERVICE_H
