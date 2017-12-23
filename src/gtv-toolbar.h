/*
 * Copyright 2014 - 2018 Stepan Perun
 * This program is free software.
 * License: Gnu Lesser General Public License
 * http://www.gnu.org/licenses/lgpl.html
*/


#ifndef GTV_TOOLBAR_H
#define GTV_TOOLBAR_H


#include <gtk/gtk.h>


void gtv_create_gaction_entry ( GtkApplication *app, GSimpleActionGroup *group );

GtkToolItem * gtv_get_item_toolbar ( GtkToolbar *toolbar, gint i );
GtkToolbar  * gtv_create_toolbar ( guint start_num, guint end_num );

GtkMenu * gtv_create_menu ( GSimpleActionGroup *group );


#endif /* GTV_TOOLBAR_H */
