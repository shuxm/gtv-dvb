/*
 * Copyright 2014 - 2018 Stepan Perun
 * This program is free software.
 * License: Gnu Lesser General Public License
 * http://www.gnu.org/licenses/lgpl.html
*/


#include <glib/gi18n.h>

#include "gtv-dvb.h"
#include "gtv-toolbar.h"


static guint j = 0;


const struct GtvAllMedia { const gchar *label; const gchar *name_icon; void (* activate)(); const gchar *accel_key; } 
gtv_all_media_n[] =
{
    // Toolbar media
    { N_("Stop"),           "media-playback-stop",     gtv_stop,  "<control>x" },
    { N_("Record"),         "media-record",            gtv_rec,   "<control>r" },
    { N_("EQ-Audio"),       "preferences-desktop",     gtv_audio, "<control>a" },
    { N_("EQ-Video"),       "preferences-desktop",     gtv_video, "<control>v" },
    { N_("Channels"),       "applications-multimedia", gtv_plist, "<control>l" },
    { N_("Scanner"),        "display",                 gtv_scan,  "<control>u" },

    // Toolbar sw
    { N_("Up"),             "up",                      gtv_goup,  "<control>z" },
    { N_("Down"),           "down",                    gtv_down,  "<control>z" },
    { N_("Remove"),         "remove",                  gtv_remv,  "<control>z" },
    { N_("Clear"),          "edit-clear",              gtv_clear, "<control>z" },

    // Menu
    { N_("Mute"),           "audio-volume-muted",      gtv_mute,  "<control>m" },
    { N_("Mini"),           "view-restore",            gtv_mini,  "<control>h" },
    { N_("Full-screen"),    "view-fullscreen",         gtv_flscr, "<control>f" },
    { N_("Preferences"),    "preferences-desktop",     gtv_pref,  "<control>p" },
    { N_("About"),          "help-about",              gtv_about, "<shift>a"   },
    { N_("Quit"),           "system-shutdown",         gtv_quit,  "<control>q" }
};


void gtv_create_gaction_entry ( GtkApplication *app, GSimpleActionGroup *group )
{
    GActionEntry entries[ G_N_ELEMENTS ( gtv_all_media_n ) ];

    for ( j = 0; j < G_N_ELEMENTS ( gtv_all_media_n ); j++ )
    {
        entries[j].name           = gtv_all_media_n[j].label;
        entries[j].activate       = gtv_all_media_n[j].activate;
        entries[j].parameter_type = NULL; // g_variant_new_string ( gtv_all_media_n[j].accel_key );
        entries[j].state          = NULL;

        gchar *text = g_strconcat ( "app.", gtv_all_media_n[j].label, NULL );
        const gchar *accelf[] = { gtv_all_media_n[j].accel_key, NULL };
            gtk_application_set_accels_for_action ( app, text, accelf );
        g_free ( text );
    }

    g_action_map_add_action_entries ( G_ACTION_MAP ( app ),   entries, G_N_ELEMENTS ( entries ), NULL );
    g_action_map_add_action_entries ( G_ACTION_MAP ( group ), entries, G_N_ELEMENTS ( entries ), NULL );
}


GtkToolItem * gtv_get_item_toolbar ( GtkToolbar *toolbar, gint i )
{
    return gtk_toolbar_get_nth_item ( GTK_TOOLBAR ( toolbar ), i );
}

static GtkToolbar * gtv_create_toolbar_all ( guint start, guint stop )
{
    GtkToolbar *toolbar_create = (GtkToolbar *)gtk_toolbar_new ();

    GtkToolItem *item;
    gtk_widget_set_valign ( GTK_WIDGET ( toolbar_create ), GTK_ALIGN_CENTER );

    for ( j = start; j < stop; j++ )
    {
        item = gtk_tool_button_new ( NULL, _(gtv_all_media_n[j].label) );
        gtk_tool_button_set_icon_name ( GTK_TOOL_BUTTON ( item ), gtv_all_media_n[j].name_icon );
        g_signal_connect ( item, "clicked", G_CALLBACK ( gtv_all_media_n[j].activate ), NULL );

        gtk_toolbar_insert ( toolbar_create, item, -1 );
    }

    gtk_toolbar_set_icon_size ( toolbar_create, GTK_ICON_SIZE_MENU );

    return toolbar_create;
}

GtkToolbar * gtv_create_toolbar ( guint start_num, guint end_num )
{
    return gtv_create_toolbar_all ( start_num, end_num );
}

static GMenu * gtv_create_gmenu ()
{
    GMenu *menu = g_menu_new ();
    GMenuItem *mitem;

/*
    Stop 0 | Record 1 | EQ-Audio 2 | EQ-Video 3 | Channels 4 | Scanner 5 | Up 6 | Down 7
    Remove 8 | Clear 9 | Mute 10 | Mini 11 |  Full-screen12 | Preferences 13 | About 14 | Quit 15
*/

    gint dat_n[] = { 0, 1, 2, 3, 4, 5, 10, 11, 12, 13, 14, 15 };

    for ( j = 0; j < G_N_ELEMENTS ( dat_n ); j++ )
    {
        gchar *text = g_strconcat ( "menu.", gtv_all_media_n[ dat_n[j] ].label, NULL );
            mitem = g_menu_item_new ( _(gtv_all_media_n[ dat_n[j] ].label), text );
        g_free ( text );

        g_menu_item_set_icon ( mitem, G_ICON ( gtk_icon_theme_load_icon ( gtk_icon_theme_get_default (),
                               gtv_all_media_n[ dat_n[j] ].name_icon, 16, GTK_ICON_LOOKUP_NO_SVG, NULL ) ) );

        g_menu_item_set_attribute_value ( mitem, "accel", g_variant_new_string ( gtv_all_media_n[ dat_n[j] ].accel_key ) );
        g_menu_append_item ( menu, mitem );
    }

    return menu;
}

GtkMenu * gtv_create_menu ( GSimpleActionGroup *group )
{
    GtkMenu *gtv_menu = (GtkMenu *)gtk_menu_new_from_model ( G_MENU_MODEL ( gtv_create_gmenu () ) );
    gtk_widget_insert_action_group ( GTK_WIDGET ( gtv_menu ), "menu", G_ACTION_GROUP ( group ) );

    return gtv_menu;
}
