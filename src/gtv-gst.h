/*
 * Copyright 2014 - 2018 Stepan Perun
 * This program is free software.
 * License: Gnu Lesser General Public License
 * http://www.gnu.org/licenses/lgpl.html
*/


#ifndef GTV_CREATE_GST_H
#define GTV_CREATE_GST_H


#include <gtk/gtk.h>
#include <gst/gst.h>


struct GtvGstDvb
{
	guintptr video_window_handle;
	GstElement *dvbplay;
};

struct GtvGstDvb gtvgstdvb;


void gtv_video_window_realize ( GtkDrawingArea *draw );

gboolean gtv_gst_create ();

void gtv_gst_tsdemux ();
void gtv_gst_tsdemux_remove ();
gboolean gtv_gst_rec ( gchar *name_ch );
void gtv_gst_rec_remove ();

GstElement * gtv_gstelement_src ();
GstElement * gtv_gstelement_mts ();
GstElement * gtv_gstelement_mut ();
GstElement * gtv_gstelement_eqa ();
GstElement * gtv_gstelement_eqv ();

GstElement * gtv_gstelement_rec ();


#endif // GTV_CREATE_GST_H
