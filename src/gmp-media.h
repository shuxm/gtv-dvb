/*
 * Copyright 2014 - 2018 Stepan Perun
 * This program is free software.
 * License: Gnu Lesser General Public License
 * http://www.gnu.org/licenses/lgpl.html
*/


#ifndef GMP_MEDIA_H
#define GMP_MEDIA_H


#include <gtk/gtk.h>
#include <gst/gst.h>


#define MAX_AUDIO 32

enum COLS 
{ 
	COL_NUM, 
	COL_TITLE, 
	COL_DATA, 
	NUM_COLS
};

struct trw_columns 
{
	const gchar *title;
	gboolean visible; 
};

void gmp_media_win ( GtkBox *box, GdkPixbuf *logo, gboolean set_tv_pl, struct trw_columns sw_col_n[], guint num );

void gmp_set_sgn_snr ( GstElement *element, GtkLabel *label, GtkProgressBar *barsgn, GtkProgressBar *barsnr, gdouble sgl, gdouble snr, gboolean hlook );
void gmp_str_split_ch_data ( gchar *data );

void gmp_media_set_tv   ();
void gmp_media_set_mp   ();
void gmp_media_stop_all ();
gboolean gmp_checked_filter ( gchar *file_name );


#endif // GMP_MEDIA_H
