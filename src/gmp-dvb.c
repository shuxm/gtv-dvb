/*
 * Copyright 2014 - 2018 Stepan Perun
 * This program is free software.
 * License: Gnu Lesser General Public License
 * http://www.gnu.org/licenses/lgpl.html
*/


#include "gmp-dvb.h"
#include "gmp-media.h"
#include "gmp-pref.h"

#include <locale.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>


struct GmpBase
{
	GtkWindow *main_window;
	GdkPixbuf *tv_logo, *mp_logo;
	GtkBox *mn_vbox, *bs_vbox, *tv_vbox, *mp_vbox;
};

struct GmpBase gmpbase;



static void gmp_about_win ( GtkWindow *window, GdkPixbuf *logo )
{
	g_debug ( "gmp_about_win \n" );

    GtkAboutDialog *dialog = (GtkAboutDialog *)gtk_about_dialog_new ();
    gtk_window_set_transient_for ( (GtkWindow *)dialog, window );

    const gchar *authors[] = { "Stepan Perun", NULL };

    const gchar *license = "GNU Lesser General Public License \nwww.gnu.org/licenses/lgpl.html";

    gtk_about_dialog_set_program_name ( dialog, " Gtv-Dvb " );
    gtk_about_dialog_set_version ( dialog, "2.0" );
    gtk_about_dialog_set_license ( dialog, license );
    gtk_about_dialog_set_authors ( dialog, authors );
    gtk_about_dialog_set_website ( dialog,   "https://github.com/vl-nix/gtv-dvb" );
    gtk_about_dialog_set_copyright ( dialog, "Copyright 2014 - 2018 Gtv-Dvb" );
    gtk_about_dialog_set_comments  ( dialog, "Media Player & Digital TV \nDVB-T2/S2/C, ATSC, DTMB" );

    if ( logo != NULL )
        gtk_about_dialog_set_logo ( dialog, logo );
    else
        gtk_about_dialog_set_logo_icon_name ( dialog, "applications-multimedia" );

    gtk_dialog_run ( GTK_DIALOG (dialog) );

    gtk_widget_destroy ( GTK_WIDGET (dialog) );
}

static void gmp_base_set_about ()
{
	gmp_about_win ( gmpbase.main_window, gmpbase.tv_logo );

	g_debug ( "gmp_set_about \n" );
}

static void gmp_base_set_pref ()
{
	gmp_pref_win ( gmpbase.main_window );

	g_debug ( "gmp_base_set_pref \n" );
}

void gmp_base_update_win ()
{
	gtk_widget_queue_draw ( GTK_WIDGET ( gmpbase.main_window ) );

	g_debug ( "gmp_base_update_win \n" );
}

GtkWindow * gmp_base_win_ret ()
{
	g_debug ( "gmp_base_win_ret \n" );
	
	return gmpbase.main_window;
}

gboolean gmp_base_flscr ()
{
	g_debug ( "gmp_base_flscr \n" );

    GdkWindowState state = gdk_window_get_state ( gtk_widget_get_window ( GTK_WIDGET ( gmpbase.main_window ) ) );

    if ( state & GDK_WINDOW_STATE_FULLSCREEN )
        { gtk_window_unfullscreen ( gmpbase.main_window ); return FALSE; }
    else
        { gtk_window_fullscreen   ( gmpbase.main_window ); return TRUE; }

	return TRUE;
}

void gmp_base_set_window ()
{
	gtk_window_set_title ( gmpbase.main_window, "Digital TV & Media Player");

	gtk_widget_hide ( GTK_WIDGET ( gmpbase.mp_vbox ) );
	gtk_widget_hide ( GTK_WIDGET ( gmpbase.tv_vbox ) );
	gtk_widget_show ( GTK_WIDGET ( gmpbase.bs_vbox ) );

	g_debug ( "gmp_base_set_window \n" );
}

static void gmp_base_set_tv ()
{
	gtk_window_set_icon  ( gmpbase.main_window, gmpbase.tv_logo );
	gtk_window_set_title ( gmpbase.main_window, "Digital TV");

	gtk_widget_hide ( GTK_WIDGET ( gmpbase.bs_vbox ) );
	gtk_widget_hide ( GTK_WIDGET ( gmpbase.mp_vbox ) );
	gtk_widget_show ( GTK_WIDGET ( gmpbase.tv_vbox ) );

	gmp_media_set_tv ();

	g_debug ( "gmp_base_set_tv \n" );
}

static void gmp_base_set_mp ()
{
	gtk_window_set_icon  ( gmpbase.main_window, gmpbase.mp_logo );
	gtk_window_set_title ( gmpbase.main_window, "Media Player");

	gtk_widget_hide ( GTK_WIDGET ( gmpbase.bs_vbox ) );
	gtk_widget_hide ( GTK_WIDGET ( gmpbase.tv_vbox ) );
	gtk_widget_show ( GTK_WIDGET ( gmpbase.mp_vbox ) );

	gmp_media_set_mp ();

	g_debug ( "gmp_base_set_mp \n" );
}

static void gmp_base_button ( GtkBox *box, gchar *icon, guint size, void (* activate)() )
{
	GdkPixbuf *logo = gtk_icon_theme_load_icon ( gtk_icon_theme_get_default (),
              icon, size, GTK_ICON_LOOKUP_USE_BUILTIN, NULL );

  	GtkButton *button = (GtkButton *)gtk_button_new ();
  	GtkImage *image   = (GtkImage  *)gtk_image_new_from_pixbuf ( logo );
 
	gtk_button_set_image ( button, GTK_WIDGET ( image ) );

  	g_signal_connect ( button, "clicked", G_CALLBACK (activate), NULL );

  	gtk_box_pack_start ( box, GTK_WIDGET ( button ), TRUE, TRUE, 5 );

	g_debug ( "gmp_base_button \n" );
}

static void gmp_base_init ()
{
	gmp_rec_dir = g_strdup ( g_get_home_dir () );

    gchar *dir_conf = g_strdup_printf ( "%s/gtv", g_get_user_config_dir () );

    if ( !g_file_test ( dir_conf, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_DIR ) )
    {
        g_mkdir ( dir_conf, 0777 );
        g_print ( "Creating %s directory. \n", dir_conf );
    }

    g_free ( dir_conf );


	opacity_cnt = 0.85;
	opacity_eq  = 0.85;
	opacity_win = 1.0;
	resize_icon = 48;
	
    gmp_dvb_conf = g_strconcat ( g_get_user_config_dir (), "/gtv/gtv.conf",         NULL );
	ch_conf      = g_strconcat ( g_get_user_config_dir (), "/gtv/gtv-channel.conf", NULL );

    if ( g_file_test ( gmp_dvb_conf, G_FILE_TEST_EXISTS ) )
		gmp_pref_read_config ( gmp_dvb_conf );


	gtk_icon_theme_add_resource_path ( gtk_icon_theme_get_default (), "/gmp-dvb/res/icons" );

	gmpbase.tv_logo = gtk_icon_theme_load_icon ( gtk_icon_theme_get_default (),
              "gmp-tv", 256, GTK_ICON_LOOKUP_USE_BUILTIN, NULL );

	gmpbase.mp_logo = gtk_icon_theme_load_icon ( gtk_icon_theme_get_default (),
              "gmp-mp", 256, GTK_ICON_LOOKUP_USE_BUILTIN, NULL );

	g_debug ( "gmp_base_init \n" );
}

static void gmp_base_quit ( /*GtkWindow *window*/ )
{
	gmp_media_stop_all ();

	gmp_pref_save_config ( gmp_dvb_conf );

	gtk_widget_destroy ( GTK_WIDGET ( gmpbase.main_window ) );

	g_debug ( "gmp_base_quit \n" );
}

static void gmp_base_win ( GtkApplication *app )
{
	gmp_base_init ();

	gmpbase.main_window = (GtkWindow *)gtk_application_window_new (app);
  	gtk_window_set_title ( gmpbase.main_window, "Digital TV & Media Player");
  	gtk_window_set_default_size ( gmpbase.main_window, 800, 400 );
	g_signal_connect ( gmpbase.main_window, "destroy", G_CALLBACK ( gmp_base_quit ), NULL );

	gtk_window_set_icon ( gmpbase.main_window, gmpbase.tv_logo );


  	gmpbase.mn_vbox = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_VERTICAL, 0 );
	gmpbase.bs_vbox = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_VERTICAL, 0 );
  	gmpbase.tv_vbox = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_VERTICAL, 0 );
	gmpbase.mp_vbox = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_VERTICAL, 0 );

	gtk_container_set_border_width ( GTK_CONTAINER ( gmpbase.bs_vbox ), 25 );


  	GtkBox *bt_hbox = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_HORIZONTAL, 0 );
	
	gmp_base_button ( bt_hbox, "gmp-mp", 256, *(gmp_base_set_mp) );
	gmp_base_button ( bt_hbox, "gmp-tv", 256, *(gmp_base_set_tv) );

  	gtk_box_pack_start ( gmpbase.bs_vbox, GTK_WIDGET ( bt_hbox ), TRUE,  FALSE,  5 );


  	GtkBox *bc_hbox = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_HORIZONTAL, 0 );

	gmp_base_button ( bc_hbox, "gmp-pref",     48, *(gmp_base_set_pref)  );
	gmp_base_button ( bc_hbox, "gmp-about",    48, *(gmp_base_set_about) );
	gmp_base_button ( bc_hbox, "gmp-shutdown", 48, *(gmp_base_quit)      );

	gtk_box_pack_start ( gmpbase.bs_vbox, GTK_WIDGET ( bc_hbox ), TRUE,  FALSE,  5 );


	struct trw_columns trw_cols_tv_n[] =
	{
		{ "Num", TRUE  }, { _("Channels"), TRUE  }, { "Data", FALSE }
	};

	gmp_media_win ( gmpbase.tv_vbox, gmpbase.tv_logo, TRUE, trw_cols_tv_n, G_N_ELEMENTS ( trw_cols_tv_n ) );


	struct trw_columns trw_cols_pl_n[] =
	{
		{ "Num", TRUE  }, { _("Files"), TRUE  }, { "Full Uri", FALSE }
	};

	gmp_media_win ( gmpbase.mp_vbox, gmpbase.mp_logo, FALSE, trw_cols_pl_n, G_N_ELEMENTS ( trw_cols_pl_n ) );


	gtk_box_pack_start ( gmpbase.mn_vbox, GTK_WIDGET ( gmpbase.bs_vbox ), TRUE,  TRUE,  0 );	
	gtk_box_pack_start ( gmpbase.mn_vbox, GTK_WIDGET ( gmpbase.tv_vbox ), TRUE,  TRUE,  0 );
	gtk_box_pack_start ( gmpbase.mn_vbox, GTK_WIDGET ( gmpbase.mp_vbox ), TRUE,  TRUE,  0 );


  	gtk_container_add ( GTK_CONTAINER ( gmpbase.main_window ), GTK_WIDGET (gmpbase. mn_vbox ) );
  	gtk_widget_show_all ( GTK_WIDGET ( gmpbase.main_window ) );

	gtk_widget_hide ( GTK_WIDGET ( gmpbase.tv_vbox ) );
	gtk_widget_hide ( GTK_WIDGET ( gmpbase.mp_vbox ) );

	gtk_window_resize ( gmpbase.main_window, 800, 400 );
	
	if ( gmp_arg_one ) gmp_base_set_mp ();
	
	gtk_widget_set_opacity ( GTK_WIDGET ( gmpbase.main_window ), opacity_win );

	g_debug ( "gmp_base_win \n" );
}

static void gmp_base_get_arg ( int argc, char **argv )
{
	//guint n = 0;
	//for ( n = 0; n < argc; n++ )
		//g_print ( "argv %s \n", argv[n] );
	
	gmp_arg_one = NULL;
	
	if ( argc == 1 ) return;
	
	if ( g_file_test ( argv[1], G_FILE_TEST_EXISTS ) )
	{
		if ( gmp_checked_filter ( argv[1] ) 
		  || g_file_test ( argv[1], G_FILE_TEST_IS_DIR )
		  || g_str_has_suffix ( argv[1], ".m3u" ) )
			gmp_arg_one = g_strdup ( argv[1] );
		
		g_debug ( "gmp_arg_one: %s \n", gmp_arg_one );
	}
	else
	{
        GFile *file = g_file_new_for_uri ( argv[1] );
        gchar *file_name = g_file_get_path ( file );

		if ( g_file_test ( file_name, G_FILE_TEST_EXISTS ) )
		{
			if ( gmp_checked_filter ( file_name ) 
		      || g_file_test ( file_name, G_FILE_TEST_IS_DIR )
		      || g_str_has_suffix ( file_name, ".m3u" ) )
				gmp_arg_one = g_strdup ( file_name );
        }
           
		g_debug ( "gmp_arg_one uri: %s \n", gmp_arg_one );

        g_free ( file_name );
        g_object_unref ( file );
	}	
}

static void gmp_set_locale ()
{
    setlocale ( LC_ALL, "" );
    bindtextdomain ( "gtv-dvb", "/usr/share/locale/" );
    textdomain ( "gtv-dvb" );
}

int main ( int argc, char **argv )
{
	gst_init ( NULL, NULL );
	
	gmp_set_locale ();

	gmp_base_get_arg ( argc, argv );

    GtkApplication *app = gtk_application_new ( NULL, G_APPLICATION_FLAGS_NONE );
    g_signal_connect ( app, "activate", G_CALLBACK ( gmp_base_win ),  NULL );

    int status = g_application_run ( G_APPLICATION ( app ), 0, NULL );
    g_object_unref ( app );

    return status;
}
