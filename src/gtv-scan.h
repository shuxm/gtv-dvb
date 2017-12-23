/*
 * Copyright 2014 - 2018 Stepan Perun
 * This program is free software.
 * License: Gnu Lesser General Public License
 * http://www.gnu.org/licenses/lgpl.html
*/


#ifndef GTV_SCAN_H
#define GTV_SCAN_H


#include <gtk/gtk.h>
#include <gst/gst.h>


void gtv_scan_gst_create ();
void gtv_win_scan ();

void gtv_get_dvb_info ( gboolean is_scan, gboolean set_delsys, gboolean get_all_sys, guint adapter, guint frontend );

void gtv_scan_stop ( GtkButton *button, gpointer data );
void gtv_set_lnb  ( GstElement *element, gint num_lnb );


#endif // GTV_SCAN_H
