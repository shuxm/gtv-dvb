/*
 * Copyright 2014 - 2017 Stepan Perun
 * This program is free software.
 * License: GNU LESSER GENERAL PUBLIC LICENSE
 * http://www.gnu.org/licenses/lgpl.html
*/

#ifndef GTV_H
#define GTV_H

#include <gtk/gtk.h>
#include <gst/gst.h>
#include <glib/gstdio.h>


gchar *rec_dir, *audio_encoder, *video_encoder, *muxer, *file_ext;

GtkTreeView *tv_treeview;
enum { COL_NUM, COL_FILES_CH, COL_URI_DATA, NUM_COLS };

void tv_set_tuning_timeout ( GstElement *element );

GtkScrolledWindow * tv_scroll_win ( GtkTreeView *tree_view, gchar *title, gchar *data );
void tv_add_channels ( const gchar *name_ch, gchar *data );
void tv_str_split_ch_data ( gchar *data );
void tv_set_sgn_snr ( GstElement *element, GtkLabel *label, gdouble sgb, gdouble srb, gboolean hlook );

gchar * tv_openf ();
gchar * tv_rec_dir ();


#endif // GTV_H
