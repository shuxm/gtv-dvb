/*
 * Copyright 2014 - 2018 Stepan Perun
 * This program is free software.
 * License: Gnu Lesser General Public License
 * http://www.gnu.org/licenses/lgpl.html
*/


#include <glib/gi18n.h>

#include "gtv-treeview.h"
#include "gtv-dvb.h"


static void gtv_treeview_reread_mini ( GtkTreeView *tree_view )
{
    GtkTreeIter iter;
    GtkTreeModel *model = gtk_tree_view_get_model ( GTK_TREE_VIEW ( tree_view ) );

    gint row_count = 1;
    gboolean valid;
    for ( valid = gtk_tree_model_get_iter_first ( model, &iter ); valid;
          valid = gtk_tree_model_iter_next ( model, &iter ) )
    {
        gtk_list_store_set ( GTK_LIST_STORE ( model ), &iter, COL_NUM, row_count++, -1 );
    }
}

void gtv_treeview_up_down ( GtkTreeView *tree_view, gboolean up_dw )
{
    GtkTreeIter iter;
    GtkTreeModel *model = gtk_tree_view_get_model ( GTK_TREE_VIEW ( tree_view ) );
    gint ind = gtk_tree_model_iter_n_children ( model, NULL );

    if ( ind > 1 )
    if ( gtk_tree_selection_get_selected ( gtk_tree_view_get_selection ( tree_view ), NULL, &iter ) )
    {
        GtkTreeIter *iter_c = gtk_tree_iter_copy ( &iter );

            if ( up_dw )
                if ( gtk_tree_model_iter_previous ( model, &iter ) )
                    gtk_list_store_move_before ( GTK_LIST_STORE ( model ), iter_c, &iter );

            if ( !up_dw )
                if ( gtk_tree_model_iter_next ( model, &iter ) )
                    gtk_list_store_move_after ( GTK_LIST_STORE ( model ), iter_c, &iter );

        gtk_tree_iter_free ( iter_c );
        gtv_treeview_reread_mini ( tree_view );
    }
}

static void gtv_dialog_remove_clear ( GtkWindow *window, GtkTreeView *tree_view, GtkTreeIter iter, const gchar *info_text, gboolean remove_clear )
{
    GtkDialogFlags flags = GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT;

    GtkDialog *dialog = (GtkDialog *)gtk_dialog_new_with_buttons ( _("Message"),
                        window,  flags,
                        "gtk-cancel", GTK_RESPONSE_REJECT,
                        "gtk-ok",     GTK_RESPONSE_ACCEPT,
                        NULL );

    GtkBox *content = (GtkBox *)gtk_dialog_get_content_area ( dialog );

    gchar *text = g_strdup_printf ( "\n %s \n", _(info_text) );
        GtkLabel *label = (GtkLabel *)gtk_label_new ( text );
    g_free ( text );

    gtk_container_add ( GTK_CONTAINER ( content ), GTK_WIDGET ( label ) );
    gtk_widget_show_all ( GTK_WIDGET ( content ) );

    if ( gtk_dialog_run ( GTK_DIALOG ( dialog ) ) == GTK_RESPONSE_ACCEPT )
    {
        GtkTreeModel *model = gtk_tree_view_get_model ( GTK_TREE_VIEW ( tree_view ) );

        if ( remove_clear )
            gtk_list_store_remove ( GTK_LIST_STORE ( model ), &iter );
        else
            gtk_list_store_clear ( GTK_LIST_STORE ( model ) );
    }

    gtk_widget_destroy ( GTK_WIDGET ( dialog ) );
}

void gtv_treeview_remove ( GtkWindow *main_window, GtkTreeView *tree_view )
{
    GtkTreeIter iter;

    if ( gtk_tree_selection_get_selected ( gtk_tree_view_get_selection ( tree_view ), NULL, &iter ) )
    {
        gtv_dialog_remove_clear  ( main_window, tree_view, iter, _("Remove?"), TRUE );
        gtv_treeview_reread_mini ( tree_view );
    }
}

void gtv_treeview_clear ( GtkWindow *main_window, GtkTreeView *tree_view )
{
    GtkTreeIter iter;
    GtkTreeModel *model = gtk_tree_view_get_model ( GTK_TREE_VIEW ( tree_view ) );

    guint ind = gtk_tree_model_iter_n_children ( model, NULL );
    if ( ind == 0 ) return;

    gtv_dialog_remove_clear ( main_window, tree_view, iter, _("Clear all??"), FALSE );
}

void gtv_add_channels ( GtkTreeView *tree_view, gchar *name_ch, gchar *data )
{
    GtkTreeIter iter;
    GtkTreeModel *model = gtk_tree_view_get_model ( GTK_TREE_VIEW ( tree_view ) );
    guint ind = gtk_tree_model_iter_n_children ( model, NULL );

    gtk_list_store_append ( GTK_LIST_STORE ( model ), &iter);
    gtk_list_store_set    ( GTK_LIST_STORE ( model ), &iter,
                            COL_NUM, ind+1,
                            COL_TITLE, name_ch,
                            COL_DATA, data,
                            -1 );
}

static void gtv_tree_view_row_activated ( GtkTreeView *tree_view, GtkTreePath *path/*, GtkTreeViewColumn *column*/ )
{
    GtkTreeIter iter;
    GtkTreeModel *model = gtk_tree_view_get_model ( GTK_TREE_VIEW ( tree_view ) );

    if ( gtk_tree_model_get_iter ( model, &iter, path ) )
    {
        gchar *file_ch = NULL;
            gtk_tree_model_get ( model, &iter, COL_DATA, &file_ch, -1 );
            gtv_stop_play ( file_ch );
        g_free ( file_ch );
    }
}

static void gtv_create_columns ( GtkTreeView *tree_view, GtkTreeViewColumn *column, GtkCellRenderer *renderer, const gchar *name, gint column_id, gboolean col_vis )
{
    column = gtk_tree_view_column_new_with_attributes ( _(name), renderer, "text", column_id, NULL );
    gtk_tree_view_append_column ( tree_view, column );
    gtk_tree_view_column_set_visible ( column, col_vis );
}
static void gtv_add_columns ( GtkTreeView *tree_view, struct trw_columns sw_col_n[], guint num )
{
    GtkTreeViewColumn *column_n[num];
    GtkCellRenderer *renderer = gtk_cell_renderer_text_new ();

	guint c = 0;
    for ( c = 0; c < num; c++ )
        gtv_create_columns ( tree_view, column_n[c], renderer, sw_col_n[c].title, c, sw_col_n[c].visible );
}

GtkScrolledWindow * gtv_treeview ( GtkTreeView *tree_view, struct trw_columns sw_col_n[], guint num )
{
    GtkScrolledWindow *scroll_win = (GtkScrolledWindow *)gtk_scrolled_window_new ( NULL, NULL );
    gtk_scrolled_window_set_policy ( scroll_win, GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC );
    gtk_widget_set_size_request ( GTK_WIDGET ( scroll_win ), 200, -1 );

    gtk_tree_view_set_search_column ( GTK_TREE_VIEW ( tree_view ), COL_TITLE );
    gtv_add_columns ( tree_view, sw_col_n, num );

    g_signal_connect ( tree_view, "row-activated", G_CALLBACK ( gtv_tree_view_row_activated ), NULL );
    gtk_container_add ( GTK_CONTAINER ( scroll_win ), GTK_WIDGET ( tree_view ) );

    return scroll_win;
}
