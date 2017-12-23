/*
 * Copyright 2014 - 2018 Stepan Perun
 * This program is free software.
 * License: Gnu Lesser General Public License
 * http://www.gnu.org/licenses/lgpl.html
*/


#ifndef GTV_TREEVIEW_H
#define GTV_TREEVIEW_H


#include <gtk/gtk.h>


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


GtkScrolledWindow * gtv_treeview ( GtkTreeView *tree_view, struct trw_columns trw_col_n[], guint num );

void gtv_add_channels ( GtkTreeView *tree_view, gchar *name_ch, gchar *data );

void gtv_treeview_up_down ( GtkTreeView *tree_view, gboolean up_dw );
void gtv_treeview_remove  ( GtkWindow *main_window, GtkTreeView *tree_view );
void gtv_treeview_clear   ( GtkWindow *main_window, GtkTreeView *tree_view );


#endif /* GTV_TREEVIEW_H */
