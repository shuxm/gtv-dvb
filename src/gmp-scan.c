/*
 * Copyright 2014 - 2018 Stepan Perun
 * This program is free software.
 * License: Gnu Lesser General Public License
 * http://www.gnu.org/licenses/lgpl.html
*/


#include <stdlib.h>
#include <fcntl.h>
#include <glib/gstdio.h>
#include <sys/ioctl.h>
#include <glib/gi18n.h>

#include "gmp-descr.h"
#include "gmp-scan.h"
#include "gmp-mpegts.h"
#include "gmp-dvb.h"
#include "gmp-media.h"
#include "gmp-pref.h"


struct GmpScan
{
	GstElement *dvb_scan, *dvbsrc_tune;
	
	GtkNotebook *notebook;
	GtkTreeView *scan_treeview;
	GtkLabel *label_dvb_name, *all_channels, *scan_sgn_snr;
	GtkProgressBar *bar_scan_sgn, *bar_scan_snr;
	
	guint DVB_DELSYS;
	guint adapter_set;
	guint frontend_set;
	gboolean all_info;
};

struct GmpScan gmpscan;


const gchar *undf = N_("Undefined \n"), *pol = "H";
static guint SYS_DVBC = SYS_DVBC_ANNEX_A, lnb_type = 0;


enum page_n 
{
	PAGE_SC,
	PAGE_DT,
	PAGE_DS,
	PAGE_DC,
	PAGE_AT,
	PAGE_DM,
	PAGE_CH,
	PAGE_NUM
};

const struct GmpScanLabel { guint page; const gchar *name; } 
gmp_scan_label_n[] =
{
    { PAGE_SC, N_("Scanner")  }, 
    { PAGE_DT, "DVB-T/T2" }, 
    { PAGE_DS, "DVB-S/S2" }, 
    { PAGE_DC, "DVB-C"    },
    { PAGE_AT, "ATSC"     },
    { PAGE_DM, "DTMB"     }, 
    { PAGE_CH, N_("Channels") }
};

struct DvbDescrGstParam { const gchar *name; const gchar *dvb_v5_name; const gchar *gst_param; 
						  const struct dvb_descr_all *dvb_descr; guint cdsc; } 
gst_param_dvb_descr_n[] =
{
    // descr
    { "Inversion",      "INVERSION",         "inversion",        dvb_descr_inversion_type_n,  G_N_ELEMENTS ( dvb_descr_inversion_type_n  ) },
    { "Code Rate HP",   "CODE_RATE_HP",      "code-rate-hp",     dvb_descr_coderate_type_n,   G_N_ELEMENTS ( dvb_descr_coderate_type_n   ) },
    { "Code Rate LP",   "CODE_RATE_LP",      "code-rate-lp",     dvb_descr_coderate_type_n,   G_N_ELEMENTS ( dvb_descr_coderate_type_n   ) },
    { "Inner Fec",      "INNER_FEC",         "code-rate-hp",     dvb_descr_coderate_type_n,   G_N_ELEMENTS ( dvb_descr_coderate_type_n   ) },
    { "Modulation",     "MODULATION",        "modulation",       dvb_descr_modulation_type_n, G_N_ELEMENTS ( dvb_descr_modulation_type_n ) },
    { "Transmission",   "TRANSMISSION_MODE", "trans-mode",       dvb_descr_transmode_type_n,  G_N_ELEMENTS ( dvb_descr_transmode_type_n  ) },
    { "Guard interval", "GUARD_INTERVAL",    "guard",            dvb_descr_guard_type_n,      G_N_ELEMENTS ( dvb_descr_guard_type_n 	 ) },
    { "Hierarchy",      "HIERARCHY",         "hierarchy",        dvb_descr_hierarchy_type_n,  G_N_ELEMENTS ( dvb_descr_hierarchy_type_n  ) },
    { "Pilot",          "PILOT",             "pilot",            dvb_descr_pilot_type_n,      G_N_ELEMENTS ( dvb_descr_pilot_type_n 	 ) },
    { "Rolloff",        "ROLLOFF",           "rolloff",          dvb_descr_roll_type_n,       G_N_ELEMENTS ( dvb_descr_roll_type_n 		 ) },
    { "Polarity",       "POLARIZATION",      "polarity",         dvb_descr_polarity_type_n,   G_N_ELEMENTS ( dvb_descr_polarity_type_n   ) },
    { "LNB",            "LNB",               "lnb-type",         dvb_descr_lnb_type_n,        G_N_ELEMENTS ( dvb_descr_lnb_type_n 		 ) },
    { "DiSEqC",         "SAT_NUMBER",        "diseqc-source",    dvb_descr_lnb_num_n,         G_N_ELEMENTS ( dvb_descr_lnb_num_n 		 ) },
    { "Interleaving",   "INTERLEAVING",      "interleaving",     dvb_descr_ileaving_type_n,   G_N_ELEMENTS ( dvb_descr_ileaving_type_n 	 ) },

    // digits
    { "Frequency",      "FREQUENCY",         "frequency",        NULL, 0 },
    { "Bandwidth",      "BANDWIDTH_HZ",      "bandwidth-hz",     NULL, 0 },
    { "Symbol rate",    "SYMBOL_RATE",       "symbol-rate",      NULL, 0 },
    { "Stream ID",      "STREAM_ID",         "stream-id",        NULL, 0 },
    { "Service Id",     "SERVICE_ID",        "program-number",   NULL, 0 },
    { "Audio Pid",      "AUDIO_PID",         "audio-pid",        NULL, 0 },
    { "Video Pid",      "VIDEO_PID",         "video-pid",        NULL, 0 }
};


struct lnb_types_lhs { uint descr_num; const char *name;
                       long low_val; long high_val; long switch_val; }
lnb_n[] =
{
    { LNB_UNV, "UNIVERSAL", 9750000,  10600000, 11700000 },
    { LNB_DBS, "DBS",		11250000, 0, 0               },
    { LNB_EXT, "EXTENDED",  9750000,  10600000, 11700000 },
    { LNB_STD, "STANDARD",	10000000, 0, 0               },
    { LNB_EHD, "ENHANCED",	9750000,  0, 0               },
    { LNB_CBD, "C-BAND",	5150000,  0, 0               },
    { LNB_CMT, "C-MULT",	5150000,  5750000,  0        },
    { LNB_DSP, "DISHPRO",	11250000, 14350000, 0        },
    { LNB_BS1, "110BS",		10678000, 0, 0               }
};

void gmp_set_lnb ( GstElement *element, gint num_lnb )
{
    g_object_set ( element, "lnb-lof1", lnb_n[num_lnb].low_val,    NULL );
    g_object_set ( element, "lnb-lof2", lnb_n[num_lnb].high_val,   NULL );
    g_object_set ( element, "lnb-slof", lnb_n[num_lnb].switch_val, NULL );
}


static gchar * gmp_strip_ch_name ( gchar *name )
{
    guint i = 0;
    for ( i = 0; name[i] != '\0'; i++ )
    {
		if ( name[i] == ':' || name[i] == '[' || name[i] == ']' ) name[i] = ' ';
    }
    return g_strstrip ( name );
}

static void gmp_convert_dvb5 ( gchar *filename )
{
    guint n = 0, z = 0, x = 0, count_ch = 0;
    gchar *contents;
    GError *err = NULL;

    GString *gstring = g_string_new ( NULL );

    if ( g_file_get_contents ( filename, &contents, 0, &err ) )
    {
        gchar **lines = g_strsplit ( contents, "\n", 0 );

        for ( n = 0; lines[n] != NULL; n++ )
        {
            if ( g_str_has_prefix ( lines[n], "[" ) )
            {
				g_debug ( "Channel: %s ( %d ) \n", lines[n], count_ch );

				if ( count_ch == 0 )
				{
					gstring = g_string_new ( gmp_strip_ch_name ( lines[n] ) );
	                g_string_append_printf ( gstring, ":adapter=%d:frontend=%d", gmpscan.adapter_set, gmpscan.frontend_set );

					count_ch++;
					continue;					
				}

				//g_debug ( "All data: %s \n", gstring->str );

				if ( g_strrstr ( gstring->str, "audio-pid" ) || g_strrstr ( gstring->str, "video-pid" ) )
					gmp_str_split_ch_data ( gstring->str );
				
				g_string_free ( gstring, TRUE );
				
				gstring = g_string_new ( gmp_strip_ch_name ( lines[n] ) );
                g_string_append_printf ( gstring, ":adapter=%d:frontend=%d", gmpscan.adapter_set, gmpscan.frontend_set );

				count_ch++;
            }

            for ( z = 0; z < G_N_ELEMENTS ( gst_param_dvb_descr_n ); z++ )
            {
                if ( g_strrstr ( lines[n], gst_param_dvb_descr_n[z].dvb_v5_name ) )
                {
                    gchar **value_key = g_strsplit ( lines[n], " = ", 0 );

                    g_string_append_printf ( gstring, ":%s=", gst_param_dvb_descr_n[z].gst_param );

					if ( g_strrstr ( gst_param_dvb_descr_n[z].dvb_v5_name, "SAT_NUMBER" ) )
					{
						g_string_append ( gstring, value_key[1] );
						continue;
					}

                    if ( gst_param_dvb_descr_n[z].cdsc == 0 )
                    {
                        g_string_append ( gstring, value_key[1] );
                    }
                    else
                    {
                        for ( x = 0; x < gst_param_dvb_descr_n[z].cdsc; x++ )
                        {
                            if ( g_strrstr ( value_key[1], gst_param_dvb_descr_n[z].dvb_descr[x].dvb_v5_name ) )
                            {
								g_string_append_printf ( gstring, "%d", gst_param_dvb_descr_n[z].dvb_descr[x].descr_num );
                            }
                        }
                    }
                }
            }
        }

        g_strfreev ( lines );
        g_free ( contents );
                
		if ( g_strrstr ( gstring->str, "audio-pid" ) || g_strrstr ( gstring->str, "video-pid" ) )
			gmp_str_split_ch_data ( gstring->str );
				
		g_string_free ( gstring, TRUE );
    }
    else
    {
        g_critical ( "ERROR: %s\n", err->message );
        if ( err ) g_error_free ( err );
        return;
    }
}


static void gmp_scan_get_tp_data ( GString *gstring )
{
    guint c = 0, d = 0;
    gint  d_data = 0, DVBTYPE = 0;

    g_object_get ( gmpscan.dvbsrc_tune, "delsys", &DVBTYPE, NULL );
    g_debug ( "delsys: %d | %d \n", DVBTYPE, SYS_DVBS );
    //g_string_append_printf ( gstring, ":delsys=%d", DVBTYPE );

    const gchar *dvb_f[] = { "adapter", "frontend" };

    for ( d = 0; d < G_N_ELEMENTS ( dvb_f ); d++ )
    {
        g_object_get ( gmpscan.dvbsrc_tune, dvb_f[d], &d_data, NULL );
        g_string_append_printf ( gstring, ":%s=%d", dvb_f[d], d_data );
    }

    if ( DVBTYPE == SYS_DVBT || DVBTYPE == SYS_DVBT2 )
    {
        for ( c = 0; c < G_N_ELEMENTS ( dvbt_props_n ); c++ )
        {
            if ( DVBTYPE == SYS_DVBT )
                if ( g_strrstr ( dvbt_props_n[c].param, "Stream ID" ) ) continue;

            for ( d = 0; d < G_N_ELEMENTS ( gst_param_dvb_descr_n ); d++ )
            {
                if ( g_strrstr ( dvbt_props_n[c].param, gst_param_dvb_descr_n[d].name ) )
                {
                    g_object_get ( gmpscan.dvbsrc_tune, gst_param_dvb_descr_n[d].gst_param, &d_data, NULL );
                    g_string_append_printf ( gstring, ":%s=%d", gst_param_dvb_descr_n[d].gst_param, d_data );

                    break;
                }
            }
        }
    }
    
    if ( DVBTYPE == SYS_DTMB )
    {
        for ( c = 0; c < G_N_ELEMENTS ( dtmb_props_n ); c++ )
        {
            for ( d = 0; d < G_N_ELEMENTS ( gst_param_dvb_descr_n ); d++ )
            {
                if ( g_strrstr ( dtmb_props_n[c].param, gst_param_dvb_descr_n[d].name ) )
                {
                    g_object_get ( gmpscan.dvbsrc_tune, gst_param_dvb_descr_n[d].gst_param, &d_data, NULL );
                    g_string_append_printf ( gstring, ":%s=%d", gst_param_dvb_descr_n[d].gst_param, d_data );

                    break;
                }
            }
        }
    }

    if ( DVBTYPE == SYS_DVBC_ANNEX_A || DVBTYPE == SYS_DVBC_ANNEX_C )
    {
        for ( c = 0; c < G_N_ELEMENTS ( dvbc_props_n ); c++ )
        {
            for ( d = 0; d < G_N_ELEMENTS ( gst_param_dvb_descr_n ); d++ )
            {
                if ( g_strrstr ( dvbc_props_n[c].param, gst_param_dvb_descr_n[d].name ) )
                {
                    g_object_get ( gmpscan.dvbsrc_tune, gst_param_dvb_descr_n[d].gst_param, &d_data, NULL );
                    g_string_append_printf ( gstring, ":%s=%d", gst_param_dvb_descr_n[d].gst_param, d_data );

                    break;
                }
            }
        }
    }

    if ( DVBTYPE == SYS_ATSC || DVBTYPE == SYS_DVBC_ANNEX_B )
    {
        for ( c = 0; c < G_N_ELEMENTS ( atsc_props_n ); c++ )
        {
            for ( d = 0; d < G_N_ELEMENTS ( gst_param_dvb_descr_n ); d++ )
            {
                if ( g_strrstr ( atsc_props_n[c].param, gst_param_dvb_descr_n[d].name ) )
                {
                    g_object_get ( gmpscan.dvbsrc_tune, gst_param_dvb_descr_n[d].gst_param, &d_data, NULL );
                    g_string_append_printf ( gstring, ":%s=%d", gst_param_dvb_descr_n[d].gst_param, d_data );

                    break;
                }
            }
        }
    }
    
    if ( DVBTYPE == SYS_DVBS || DVBTYPE == SYS_TURBO || DVBTYPE == SYS_DVBS2 )
    {
        for ( c = 0; c < G_N_ELEMENTS ( dvbs_props_n ); c++ )
        {
            if ( DVBTYPE == SYS_TURBO )
                if ( g_strrstr ( dvbs_props_n[c].param, "Pilot" ) || g_strrstr ( dvbs_props_n[c].param, "Rolloff" ) || g_strrstr ( dvbs_props_n[c].param, "Stream ID" ) ) continue;

            if ( DVBTYPE == SYS_DVBS )
                if ( g_strrstr ( dvbs_props_n[c].param, "Modulation" ) || g_strrstr ( dvbs_props_n[c].param, "Pilot" ) || g_strrstr ( dvbs_props_n[c].param, "Rolloff" ) || g_strrstr ( dvbs_props_n[c].param, "Stream ID" ) ) continue;

            for ( d = 0; d < G_N_ELEMENTS ( gst_param_dvb_descr_n ); d++ )
            {
                if ( g_strrstr ( dvbs_props_n[c].param, gst_param_dvb_descr_n[d].name ) )
                {
                    if ( g_strrstr ( "polarity", gst_param_dvb_descr_n[d].gst_param ) )
                    {
                        g_string_append_printf ( gstring, ":polarity=%s", pol );
                        continue;
                    }

                    if ( g_strrstr ( "lnb-type", gst_param_dvb_descr_n[d].gst_param ) )
                    {
                        g_string_append_printf ( gstring, ":%s=%d", "lnb-type", lnb_type );
                        continue;
                    }

                    g_object_get ( gmpscan.dvbsrc_tune, gst_param_dvb_descr_n[d].gst_param, &d_data, NULL );
                    g_string_append_printf ( gstring, ":%s=%d", gst_param_dvb_descr_n[d].gst_param, d_data );

                    break;
                }
            }
        }
    }    
}



static void gmp_scan_set_all_ch ( gint all_ch, gint c_tv, gint c_ro );


static void gmp_scan_msg_all ( GstBus *bus, GstMessage *message )
{
	if ( gmpscan.all_info )
		g_debug ( "gmp_scan_msg_all:: pending %s \n", gst_bus_have_pending ( bus ) ? "TRUE" : "FALSE" );
	
    const GstStructure *structure = gst_message_get_structure ( message );

    if ( structure )
    {
        gint signal, snr;
        gboolean hlook = FALSE;

        if (  gst_structure_get_int ( structure, "signal", &signal )  )
        {
            gst_structure_get_boolean ( structure, "lock", &hlook );
            gst_structure_get_int ( structure, "snr", &snr);

            if ( gmpscan.scan_treeview )
            {
				gmp_set_sgn_snr ( gmpscan.dvb_scan, gmpscan.scan_sgn_snr, gmpscan.bar_scan_sgn, gmpscan.bar_scan_snr,
									  (signal * 100) / 0xffff, (snr * 100) / 0xffff, hlook );
            }
        }
    }

    gmp_mpegts_parse_section ( message );
}

void gmp_scan_msg_war ( GstBus *bus, GstMessage *msg )
{
	if ( gmpscan.all_info )
		g_debug ( "gmp_scan_msg_all:: pending %s \n", gst_bus_have_pending ( bus ) ? "TRUE" : "FALSE" );	
	
    GError *war = NULL;
    gchar *dbg = NULL;

    gst_message_parse_warning ( msg, &war, &dbg );

	g_warning ( "scan_msg_war:: %s (%s)\n", war->message, (dbg) ? dbg : "no details" );

    g_error_free ( war );
    g_free ( dbg );
}

static void gmp_scan_msg_err ( GstBus *bus, GstMessage *msg )
{
	if ( gmpscan.all_info )
		g_debug ( "gmp_scan_msg_all:: pending %s \n", gst_bus_have_pending ( bus ) ? "TRUE" : "FALSE" );	
	
    GError *err = NULL;
    gchar *dbg = NULL;

    gst_message_parse_error ( msg, &err, &dbg );

    g_critical ( "scan_msg_err:: %s (%s)\n", err->message, (dbg) ? dbg : "no details" );
    gmp_message_dialog ( err->message, (dbg) ? dbg : " ", GTK_MESSAGE_ERROR );

    g_error_free ( err );
    g_free ( dbg );

	/* In some cases need to comment out this line. */
    gmp_scan_stop ();
}

static void gmp_scan_set_tune ()
{
    guint64 timeout = 0;
    g_object_get ( gmpscan.dvbsrc_tune, "tuning-timeout", &timeout, NULL );
    g_object_set ( gmpscan.dvbsrc_tune, "tuning-timeout", (guint64)timeout / 5, NULL );
}

void gmp_scan_gst_create ()
{
    GstElement *dvbsrc_parse, *dvbsrc_files;
    gmpscan.all_info = FALSE;

    gmpscan.dvb_scan     = gst_pipeline_new ( "pipeline_scan" );
    gmpscan.dvbsrc_tune  = gst_element_factory_make ( "dvbsrc",   NULL );
    dvbsrc_parse = gst_element_factory_make ( "tsparse",  NULL );
    dvbsrc_files = gst_element_factory_make ( "filesink", NULL );

    if ( !gmpscan.dvb_scan || !gmpscan.dvbsrc_tune || !dvbsrc_parse || !dvbsrc_files )
        g_critical ( "gmp_scan_gst_create:: pipeline_scan - not be created.\n" );

    gst_bin_add_many ( GST_BIN ( gmpscan.dvb_scan ), gmpscan.dvbsrc_tune, dvbsrc_parse, dvbsrc_files, NULL );
    gst_element_link_many ( gmpscan.dvbsrc_tune, dvbsrc_parse, dvbsrc_files, NULL );

    g_object_set ( dvbsrc_files, "location", "/dev/null", NULL);

    GstBus *bus_scan = gst_element_get_bus ( gmpscan.dvb_scan );
    gst_bus_add_signal_watch ( bus_scan );
    gst_object_unref ( bus_scan );

    g_signal_connect ( bus_scan, "message",          G_CALLBACK ( gmp_scan_msg_all ), NULL );
    g_signal_connect ( bus_scan, "message::error",   G_CALLBACK ( gmp_scan_msg_err ), NULL );
    g_signal_connect ( bus_scan, "message::warning", G_CALLBACK ( gmp_scan_msg_war ), NULL );

    gmp_scan_set_tune ();
    
    g_debug ( "gmp_scan_gst_create \n" );
}


static GtkButton * gmp_scan_button ( gchar *icon, guint size, void (* activate)(), gpointer data )
{
	GdkPixbuf *logo = gtk_icon_theme_load_icon ( gtk_icon_theme_get_default (),
              icon, size, GTK_ICON_LOOKUP_USE_BUILTIN, NULL );

  	GtkButton *button = (GtkButton *)gtk_button_new ();
  	GtkImage *image   = (GtkImage  *)gtk_image_new_from_pixbuf ( logo );
 
	gtk_button_set_image ( button, GTK_WIDGET ( image ) );

  	g_signal_connect ( button, "clicked", G_CALLBACK (activate), data );

	g_debug ( "gmp_scan_button \n" );

	return button;
}

static void gmp_scan_create_columns ( GtkTreeView *tree_view, GtkTreeViewColumn *column, GtkCellRenderer *renderer, const gchar *name, gint column_id, gboolean col_vis )
{
    column = gtk_tree_view_column_new_with_attributes ( name, renderer, "text", column_id, NULL );
    gtk_tree_view_append_column ( tree_view, column );
    gtk_tree_view_column_set_visible ( column, col_vis );
}
static void gmp_scan_add_columns ( GtkTreeView *tree_view, struct trw_columns sw_col_n[], guint num )
{
    GtkTreeViewColumn *column_n[num];
    GtkCellRenderer *renderer = gtk_cell_renderer_text_new ();

	guint c = 0;
    for ( c = 0; c < num; c++ )
        gmp_scan_create_columns ( tree_view, column_n[c], renderer, sw_col_n[c].title, c, sw_col_n[c].visible );
}
static GtkScrolledWindow * gmp_scan_treeview ( GtkTreeView *tree_view, struct trw_columns sw_col_n[], guint num )
{
    GtkScrolledWindow *scroll_win = (GtkScrolledWindow *)gtk_scrolled_window_new ( NULL, NULL );
    gtk_scrolled_window_set_policy ( scroll_win, GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC );
    gtk_widget_set_size_request ( GTK_WIDGET ( scroll_win ), 200, -1 );

    gtk_tree_view_set_search_column ( GTK_TREE_VIEW ( tree_view ), COL_TITLE );
    gmp_scan_add_columns ( tree_view, sw_col_n, num );

    gtk_container_add ( GTK_CONTAINER ( scroll_win ), GTK_WIDGET ( tree_view ) );

    return scroll_win;
}

static void gmp_scan_channels_save ()
{
    GtkTreeIter iter;
    GtkTreeModel *model = gtk_tree_view_get_model ( GTK_TREE_VIEW ( gmpscan.scan_treeview ) );

    gboolean valid;
    for ( valid = gtk_tree_model_get_iter_first ( model, &iter ); valid;
          valid = gtk_tree_model_iter_next ( model, &iter ) )
    {
        gchar *data;
        gtk_tree_model_get ( model, &iter, COL_DATA,  &data, -1 );
            gmp_str_split_ch_data ( data );
        g_free ( data );
    }	
	
    g_debug ( "gmp_scan_channels_save \n ");
}
static void gmp_scan_channels_clear ()
{  
	gmp_scan_set_all_ch ( 0, 0, 0 );

    GtkTreeModel *model = gtk_tree_view_get_model ( GTK_TREE_VIEW ( gmpscan.scan_treeview ) );
	gtk_list_store_clear ( GTK_LIST_STORE ( model ) );
	
    g_debug ( "gmp_scan_channels_clear \n ");
}

static GtkBox * gmp_scan_channels  ()
{
	g_debug ( "gmp_scan_channels \n" );
	
    GtkBox *g_box  = (GtkBox *)gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_margin_start ( GTK_WIDGET ( g_box ), 10 );
    gtk_widget_set_margin_end   ( GTK_WIDGET ( g_box ), 10 );

    gmpscan.all_channels = (GtkLabel *)gtk_label_new ( _("All Channels") );
    gtk_widget_set_halign ( GTK_WIDGET ( gmpscan.all_channels ),    GTK_ALIGN_START );

    gmpscan.scan_treeview = (GtkTreeView *)gtk_tree_view_new_with_model ( GTK_TREE_MODEL ( gtk_list_store_new ( 3, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING ) ) );

	struct trw_columns trw_cols_scan_n[] =
	{
		{ "Num",      TRUE  },
		{ _("Channels"), TRUE  },
		{ "Data",     FALSE }
	};

    gtk_box_pack_start ( g_box, GTK_WIDGET ( gmp_scan_treeview ( gmpscan.scan_treeview, trw_cols_scan_n, G_N_ELEMENTS ( trw_cols_scan_n ) ) ), TRUE, TRUE, 10 );
    gtk_box_pack_start ( g_box, GTK_WIDGET ( gmpscan.all_channels ),  FALSE, FALSE, 0 );

	GtkBox *h_box = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_HORIZONTAL, 0 );
    
    gtk_box_pack_end ( h_box, GTK_WIDGET ( gmp_scan_button ( "gmp-save",  16, gmp_scan_channels_save, NULL ) ),  TRUE, TRUE, 5 );
    gtk_box_pack_end ( h_box, GTK_WIDGET ( gmp_scan_button ( "gmp-clear", 16, gmp_scan_channels_clear, NULL ) ), TRUE, TRUE, 5 );

	gtk_box_pack_start ( g_box, GTK_WIDGET ( h_box ), FALSE, FALSE, 10 );

    return g_box;
}



static void gmp_set_scan_label ( const gchar *dvb_name, const gchar *dvb_type )
{
    gchar *text = g_strdup_printf ( "%s\n%s", dvb_name, dvb_type );
			
		gtk_label_set_text ( gmpscan.label_dvb_name, text );

    g_free  ( text );
    
    g_debug ( "gmp_set_scan_label \n" );
}

const gchar * gmp_get_str_dvb_type ( guint delsys )
{
	g_debug ( "gmp_get_str_dvb_type \n" );
	
	const gchar *dvb_type = NULL;
	
    guint i = 0;
    for ( i = 0; i < G_N_ELEMENTS ( dvb_descr_delsys_type_n ); i++ )
    {
		if ( dvb_descr_delsys_type_n[i].descr_num == delsys )
			dvb_type = dvb_descr_delsys_type_n[i].text_vis;
	}
	
	return dvb_type;
}

void gmp_get_dvb_info ( gboolean set_label, guint adapter, guint frontend )
{
	g_print ( "\ngmp_get_dvb_info:: adapter %d frontend %d \n", adapter, frontend );
	
    const gchar *dvb_name = _(undf), *dvb_type = "Undefined";
    
    gint flags = O_RDWR;
    guint dtv_api_ver = 0, dtv_del_sys = SYS_UNDEFINED;
    gmpscan.DVB_DELSYS = SYS_UNDEFINED;

    gint fd;
    gchar *fd_name = g_strdup_printf ( "/dev/dvb/adapter%d/frontend%d", adapter, frontend );
    
    if ( ( fd = g_open ( fd_name, flags ) ) == -1 )
    {
		if ( ( fd = g_open ( fd_name, O_RDONLY ) ) == -1 )
		{
			g_critical ( "gmp_get_dvb_info: %s %s\n", fd_name, g_strerror ( errno ) );
        
			g_free  ( fd_name );
			return;
		}
    }
    
	struct dvb_frontend_info info;

    if ( ( ioctl ( fd, FE_GET_INFO, &info ) ) == -1 )
    {
		perror ( "ioctl FE_GET_INFO " );
		
		g_close ( fd, NULL );
		g_free  ( fd_name );
        return;
	}
	else
		dvb_name = info.name;

	struct dtv_property dvb_prop[2];
	struct dtv_properties cmdseq;
	
	dvb_prop[0].cmd = DTV_DELIVERY_SYSTEM;
	dvb_prop[1].cmd = DTV_API_VERSION;
	
	cmdseq.num = 2;
	cmdseq.props = dvb_prop;

    if ( ( ioctl ( fd, FE_GET_PROPERTY, &cmdseq ) ) == -1 )
    {
		perror ( "ioctl FE_GET_PROPERTY " );
		
		dtv_api_ver = 0x300;
		gboolean legacy = FALSE;
		
		switch ( info.type )
		{
			case FE_QPSK:
				legacy = TRUE;
				dtv_del_sys = SYS_DVBS; 
				break;
				
			case FE_OFDM:
				legacy = TRUE;
				dtv_del_sys = SYS_DVBT;
				break;
					
			case FE_QAM:
				legacy = TRUE;
				dtv_del_sys = SYS_DVBC; 
				break;
				
			case FE_ATSC:
				legacy = TRUE;
				dtv_del_sys = SYS_ATSC; 
				break;

			default:
				break;
		}
		
		if ( legacy )
			g_print ( "DVBv3 DVB TYPE Ok\n" );
		else
			g_critical ( "DVBv3 DVB TYPE Failed\n" );
		
		if ( !legacy )
		{
			g_close ( fd, NULL );
			g_free  ( fd_name );
			return;
		}
	}
	else
	{
		g_print ( "\nDVBv5 DELIVERY_SYSTEM Ok\n" );
		
		dtv_del_sys = dvb_prop[0].u.data;
		dtv_api_ver = dvb_prop[1].u.data;
	}
	
    g_close ( fd, NULL );
    g_free  ( fd_name );
    
    gmpscan.DVB_DELSYS = dtv_del_sys;
    
    const gchar *dvbtype = gmp_get_str_dvb_type ( dtv_del_sys );
    
    if ( dvbtype ) dvb_type = dvbtype;
    
    if ( set_label ) gmp_set_scan_label ( dvb_name, dvb_type );

	g_print ( "DVB info: %s\n%s  ( %d )\nAPI Version:  %d.%d\n\n", dvb_name, dvb_type, 
				gmpscan.DVB_DELSYS, dtv_api_ver / 256, dtv_api_ver % 256 );
}



static void gmp_scan_set_adapter ( GtkSpinButton *button )
{
    gtk_spin_button_update ( button );
    gmpscan.adapter_set = gtk_spin_button_get_value_as_int ( button );

	gtk_label_set_text ( gmpscan.label_dvb_name, _(undf) );
    gmp_get_dvb_info ( TRUE, gmpscan.adapter_set, gmpscan.frontend_set );
    
    g_debug ( "gmp_scan_set_adapter \n" );
}
static void gmp_scan_set_frontend ( GtkSpinButton *button )
{
    gtk_spin_button_update ( button );
    gmpscan.frontend_set = gtk_spin_button_get_value_as_int ( button );

	gtk_label_set_text ( gmpscan.label_dvb_name, _(undf) );
    gmp_get_dvb_info ( TRUE, gmpscan.adapter_set, gmpscan.frontend_set );
    
    g_debug ( "gmp_scan_set_frontend \n" );
}

static GtkBox * gmp_scan_device ()
{
	g_debug ( "gmp_scan_device \n" );
	
	gmpscan.adapter_set  = 0; 
	gmpscan.frontend_set = 0;
	
    GtkBox *g_box  = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_VERTICAL, 0 );
    gtk_widget_set_margin_start ( GTK_WIDGET ( g_box ), 10 );
    gtk_widget_set_margin_end   ( GTK_WIDGET ( g_box ), 10 );

    GtkGrid *grid = (GtkGrid *)gtk_grid_new();
    gtk_grid_set_column_homogeneous ( GTK_GRID ( grid ), TRUE );
    gtk_box_pack_start ( g_box, GTK_WIDGET ( grid ), TRUE, TRUE, 10 );

    struct data_a { GtkWidget *label; const gchar *ltext; GtkWidget *widget; gchar *etext; guint edat; void (* activate); }
    data_a_n[] =
    {
        { gtk_label_new ( "" ), "DVB Device", NULL, NULL, 0, NULL },
        { gtk_label_new ( "" ), NULL,         NULL, NULL, 0, NULL },

        { gtk_label_new ( "" ), "Adapter",  gtk_spin_button_new_with_range ( 0, 16, 1 ), NULL, gmpscan.adapter_set,  gmp_scan_set_adapter  },
        { gtk_label_new ( "" ), "Frontend", gtk_spin_button_new_with_range ( 0, 16, 1 ), NULL, gmpscan.frontend_set, gmp_scan_set_frontend }
    };

    guint d = 0;
    for ( d = 0; d < G_N_ELEMENTS ( data_a_n ); d++ )
    {
        gtk_label_set_label ( GTK_LABEL ( data_a_n[d].label ), data_a_n[d].ltext );
        gtk_widget_set_halign ( GTK_WIDGET ( data_a_n[d].label ), ( d == 0 ) ? GTK_ALIGN_CENTER : GTK_ALIGN_START );
        gtk_grid_attach ( GTK_GRID ( grid ), GTK_WIDGET ( data_a_n[d].label ), 0, d, ( d == 0 ) ? 2 : 1, 1 );

        if ( d == 0 )
        {
            gmpscan.label_dvb_name = GTK_LABEL ( data_a_n[d].label );
            gtk_label_set_text ( gmpscan.label_dvb_name, _(undf) );
            gmp_get_dvb_info ( TRUE, gmpscan.adapter_set, gmpscan.frontend_set );
            continue;
        }

        if ( !data_a_n[d].widget ) continue;

		gtk_spin_button_set_value ( GTK_SPIN_BUTTON ( data_a_n[d].widget ), data_a_n[d].edat );
        g_signal_connect ( data_a_n[d].widget, "changed", G_CALLBACK ( data_a_n[d].activate ), NULL );
        gtk_grid_attach ( GTK_GRID ( grid ), GTK_WIDGET ( data_a_n[d].widget ), 1, d, 1, 1 );
    }

    return g_box;
}

static void gmp_convert ( GtkButton *button )
{
	g_debug ( "gmp_convert:: file %s \n ", gmp_drag_cvr );
	
	if ( gmp_drag_cvr && g_str_has_suffix ( gmp_drag_cvr, ".conf" ) )
		gmp_convert_dvb5 ( gmp_drag_cvr );
	else
		g_print ( "gmp_convert:: no convert %s \n ", gmp_drag_cvr );
	
	gtk_widget_destroy ( GTK_WIDGET ( gtk_widget_get_parent ( gtk_widget_get_parent ( gtk_widget_get_parent (GTK_WIDGET (button)) ) ) ) );
}

static void  gmp_convert_win_set_file ( GtkEntry *entry )
{
	g_debug ( "gmp_convert_win_set_file \n" );
	
	gchar *file_name = gmp_pref_open_file ( g_get_home_dir () );

	if ( file_name == NULL ) return;
	
	if ( gmp_drag_cvr ) g_free ( gmp_drag_cvr );

    gmp_drag_cvr = file_name;

	gtk_entry_set_text ( entry, file_name );	
}

static void gmp_convert_quit ( GtkWindow *window )
{
    gtk_widget_destroy ( GTK_WIDGET ( window ) );
    
    g_debug ( "gmp_convert_quit \n" );
}

static void gmp_convert_close ( GtkButton *button, GtkWindow *window )
{
    gmp_convert_quit ( window );
    
    g_debug ( "gmp_convert_close:: widget name %s \n", gtk_widget_get_name ( GTK_WIDGET ( button ) ) );
}

void gmp_convert_win ( gboolean set_file )
{	
    GtkWindow *window =      (GtkWindow *)gtk_window_new ( GTK_WINDOW_TOPLEVEL );
    gtk_window_set_transient_for ( window, gmp_base_win_ret () );
    gtk_window_set_modal     ( window, TRUE );
    gtk_window_set_position  ( window, GTK_WIN_POS_CENTER_ON_PARENT );
    gtk_window_set_title     ( window, _("Convert") );

	gtk_window_set_type_hint ( GTK_WINDOW (window), GDK_WINDOW_TYPE_HINT_UTILITY );

    GtkBox *m_box = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_VERTICAL,   0 );
    GtkBox *h_box = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_HORIZONTAL, 0 );

    GtkBox *m_box_n = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_VERTICAL, 0 );
	gtk_box_pack_start ( m_box_n, GTK_WIDGET ( gmp_scan_device () ), TRUE, TRUE, 0 );

    gtk_box_pack_start ( m_box, GTK_WIDGET ( m_box_n ), TRUE, TRUE, 0 );


    GtkLabel *label = (GtkLabel *)gtk_label_new ( _("Format  DVBv5") );
    gtk_box_pack_start ( m_box_n, GTK_WIDGET ( label ), FALSE, FALSE, 10 );


	GdkPixbuf *logo = gtk_icon_theme_load_icon ( gtk_icon_theme_get_default (),
              "gmp-convert", 48, GTK_ICON_LOOKUP_USE_BUILTIN, NULL );

  	GtkImage *image   = (GtkImage  *)gtk_image_new_from_pixbuf ( logo );
    gtk_box_pack_start ( m_box_n, GTK_WIDGET ( image ), TRUE, TRUE, 0 );

	if ( set_file )
	{
		gchar *name = g_path_get_basename ( gmp_drag_cvr );
			label = (GtkLabel *)gtk_label_new ( name );
		g_free ( name );
		
		gtk_box_pack_start ( m_box_n, GTK_WIDGET ( label ), FALSE, FALSE, 10 );
    }
    else
    {
		GtkEntry *entry = (GtkEntry *)gtk_entry_new ();
		gtk_entry_set_text ( entry, "dvb_channel.conf" );

    gtk_widget_set_margin_start ( GTK_WIDGET ( entry ), 10 );
    gtk_widget_set_margin_end   ( GTK_WIDGET ( entry ), 10 );

        g_object_set ( entry, "editable", FALSE, NULL );
        gtk_entry_set_icon_from_icon_name ( entry, GTK_ENTRY_ICON_SECONDARY, "folder" );
        g_signal_connect ( entry, "icon-press", G_CALLBACK ( gmp_convert_win_set_file ), NULL );
        
        gtk_box_pack_start ( m_box_n, GTK_WIDGET ( entry ), FALSE, FALSE, 10 );	
    }
    

    GtkButton *button_convert = (GtkButton *)gtk_button_new_from_icon_name ( "gmp-ok", GTK_ICON_SIZE_SMALL_TOOLBAR );
    g_signal_connect ( button_convert, "clicked", G_CALLBACK ( gmp_convert ), NULL );
    gtk_box_pack_end ( h_box, GTK_WIDGET ( button_convert ), TRUE, TRUE, 5 );

    GtkButton *button_close = (GtkButton *)gtk_button_new_from_icon_name ( "gmp-exit", GTK_ICON_SIZE_SMALL_TOOLBAR );
    g_signal_connect ( button_close, "clicked", G_CALLBACK ( gmp_convert_close ), window );
    gtk_box_pack_end ( h_box, GTK_WIDGET ( button_close ), TRUE, TRUE, 5 );

    gtk_box_pack_start ( m_box, GTK_WIDGET ( h_box ), FALSE, FALSE, 5 );
    gtk_container_set_border_width ( GTK_CONTAINER ( m_box ), 5 );
    gtk_container_add ( GTK_CONTAINER ( window ), GTK_WIDGET ( m_box ) );

    gtk_widget_show_all ( GTK_WIDGET ( window ) );
    
    gtk_widget_set_opacity ( GTK_WIDGET ( window ), opacity_win );
    
    g_debug ( "gmp_convert_win:: \n" );
}


static void gmp_scan_add_to_treeview ( gchar *name, gchar *data )
{
    GtkTreeModel *model = gtk_tree_view_get_model ( GTK_TREE_VIEW ( gmpscan.scan_treeview ) );
    GtkTreeIter iter;

    guint ind = gtk_tree_model_iter_n_children ( gtk_tree_view_get_model ( GTK_TREE_VIEW ( gmpscan.scan_treeview ) ), NULL );

    gtk_list_store_append ( GTK_LIST_STORE ( model ), &iter );
    gtk_list_store_set ( GTK_LIST_STORE ( model ), &iter,
                         COL_NUM,   ind+1,
                         COL_TITLE, name,
                         COL_DATA,  data,
                         -1 );
}

static void gmp_scan_set_all_ch ( gint all_ch, gint c_tv, gint c_ro )
{
    gchar *text = g_strdup_printf ( " %s: %d \n %s: %d -- %s: %d -- %s: %d",
                   _("All Channels"), all_ch, _("TV"), c_tv, _("Radio"), c_ro, _("Other"), all_ch - c_tv - c_ro );

    gtk_label_set_text ( GTK_LABEL ( gmpscan.all_channels ), text );
    g_free ( text );
}

static void gmp_scan_set_info_ch ()
{
    guint c = 0, c_tv = 0, c_ro = 0;

    for ( c = 0; c < gtvmpegts.pmt_count; c++ )
    {
        if ( dvb_gst_pmt_n[c].vpid > 0 ) c_tv++;
        if ( dvb_gst_pmt_n[c].apid > 0 && dvb_gst_pmt_n[c].vpid == 0 ) c_ro++;
    }

    gmp_scan_set_all_ch ( gtvmpegts.pmt_count, c_tv, c_ro );
    
    g_print ( "\nAll Channels: %d    TV: %d    Radio: %d    Other: %d\n\n", 
		gtvmpegts.pmt_count, c_tv, c_ro, gtvmpegts.pmt_count-c_tv-c_ro );
}

static void gmp_scan_read_ch_to_treeview ()
{
	if ( gtvmpegts.pmt_count == 0 )
		return;
	
    GString *gstr_data = g_string_new ( NULL );
    
    gmp_scan_get_tp_data ( gstr_data );
    
    g_debug ( "%s \n", gstr_data->str );
    g_print ( "\n" );

    guint i = 0, c = 0, sdt_vct_count = gtvmpegts.pat_count;
    
    if ( gtvmpegts.sdt_count ) sdt_vct_count = gtvmpegts.sdt_count;
    if ( gtvmpegts.vct_count ) sdt_vct_count = gtvmpegts.vct_count;
    
    for ( i = 0; i < gtvmpegts.pmt_count; i++ )
    {
		gchar *ch_name = NULL;

		for ( c = 0; c < sdt_vct_count; c++ )
		{
			if ( dvb_gst_pmt_n[i].pmn_pid == dvb_gst_sdt_n[c].pmn_pid )
				{ ch_name = dvb_gst_sdt_n[c].name; break; }
			
			if ( dvb_gst_pmt_n[i].pmn_pid == dvb_gst_vct_n[c].pmn_pid )
				{ ch_name = dvb_gst_sdt_n[c].name; break; }
		}

        if ( ch_name )
			ch_name = gmp_strip_ch_name ( ch_name );
		else
			ch_name = g_strdup_printf ( "Program-%d", dvb_gst_pmt_n[i].pmn_pid );

        GString *gstring = g_string_new ( ch_name );
                
        g_string_append_printf ( gstring, ":program-number=%d:video-pid=%d:audio-pid=%d",
            dvb_gst_pmt_n[i].pmn_pid, dvb_gst_pmt_n[i].vpid, dvb_gst_pmt_n[i].apid );

        g_string_append_printf ( gstring, "%s", gstr_data->str );

        if ( gmpscan.scan_treeview && dvb_gst_pmt_n[i].apid != 0 ) // ignore other
            gmp_scan_add_to_treeview ( ch_name, gstring->str );

		g_print ( "%s \n", ch_name );
		
        g_debug ( "%s \n", gstring->str );
        
        g_free ( ch_name );
        g_string_free ( gstring, TRUE );
    }

    g_string_free ( gstr_data, TRUE );

    if ( gmpscan.scan_treeview ) 
		gmp_scan_set_info_ch ();
}


static void gmp_scan_start ()
{
	if ( GST_ELEMENT_CAST ( gmpscan.dvb_scan )->current_state == GST_STATE_PLAYING )
        return;
       
    gmp_mpegts_clear_scan ();
    time ( &gtvmpegts.t_start );
    
	gst_element_set_state ( gmpscan.dvb_scan, GST_STATE_PLAYING );
	
	g_debug ( "gmp_scan_start:: \n" );
}

void gmp_scan_stop ()
{
	if ( GST_ELEMENT_CAST ( gmpscan.dvb_scan )->current_state == GST_STATE_NULL )
        return;
        
	gst_element_set_state ( gmpscan.dvb_scan, GST_STATE_NULL );
	
	gmp_scan_read_ch_to_treeview ();

	gmp_set_sgn_snr ( gmpscan.dvb_scan, gmpscan.scan_sgn_snr, gmpscan.bar_scan_sgn, gmpscan.bar_scan_snr, 0, 0, FALSE );
	gtk_label_set_text ( gmpscan.scan_sgn_snr, "Level  &  Quality" );
		
	g_debug ( "gmp_scan_stop:: \n" );
}


static glong gmp_set_label_freq_ext ( GtkLabel *label_set, glong num )
{
    gint numpage = gtk_notebook_get_current_page ( gmpscan.notebook );

    GtkWidget *page = gtk_notebook_get_nth_page ( gmpscan.notebook, numpage );
    GtkLabel *label = (GtkLabel *)gtk_notebook_get_tab_label ( gmpscan.notebook, page );
    const gchar *name_tab = gtk_label_get_text ( label );

    if ( g_strrstr ( name_tab, "DVB-S" ) )
    {
        if ( num < 30000 )
        {
            num *= 1000;
            gtk_label_set_text ( label_set, "Frequency  MHz" );
        }
        else
            gtk_label_set_text ( label_set, "Frequency  KHz" );
    }
    else
    {
        if ( num < 1000 )
        {
            num *= 1000000;
            gtk_label_set_text ( label_set, "Frequency  MHz" );
        }
        else if ( num < 1000000 )
        {
            num *= 1000;
            gtk_label_set_text ( label_set, "Frequency  KHz" );
        }
    }

    g_debug ( "num = %ld | numpage = %d | %s\n", num, numpage, name_tab );

    return num;
}

static void gmp_scan_changed_spin_all ( GtkSpinButton *button, GtkLabel *label )
{
    gtk_spin_button_update ( button );

    glong num = gtk_spin_button_get_value  ( button );
    const gchar *name = gtk_label_get_text ( label  );

    if ( g_strrstr ( name, "Frequency" ) ) num = gmp_set_label_freq_ext ( label, num );

    guint c = 0;
    for ( c = 0; c < G_N_ELEMENTS ( gst_param_dvb_descr_n ); c++ )
        if ( g_strrstr ( name, gst_param_dvb_descr_n[c].name ) )
        {
            g_object_set ( gmpscan.dvbsrc_tune, gst_param_dvb_descr_n[c].gst_param, num, NULL );

            g_debug ( "name = %s | num = %ld | gst_param = %s \n", gtk_label_get_text ( label ),
                      num, gst_param_dvb_descr_n[c].gst_param );
	}
}
static void gmp_scan_changed_combo_all ( GtkComboBox *combo_box, GtkLabel *label )
{
    guint num = gtk_combo_box_get_active ( combo_box );
    const gchar *name = gtk_label_get_text ( label );

    if ( g_strrstr ( name, "LNB" ) )
    {
        lnb_type = num;
        gmp_set_lnb ( gmpscan.dvbsrc_tune, num );
        g_debug ( "name %s | set %s: %d \n", name, lnb_n[num].name, num );
        return;
    }
    if ( g_strrstr ( name, "DiSEqC" ) )
    {
        g_object_set ( gmpscan.dvbsrc_tune, "diseqc-source", num-1, NULL );
        g_debug ( "name = %s | set = %d | gst_param = diseqc-source \n", name, num-1 );
        return;
    }

    guint c = 0;
    for ( c = 0; c < G_N_ELEMENTS ( gst_param_dvb_descr_n ); c++ )
        if ( g_strrstr ( name, gst_param_dvb_descr_n[c].name ) )
        {
            if ( g_strrstr ( name, "Polarity" ) )
            {
                pol = gst_param_dvb_descr_n[c].dvb_descr[num].descr_num ? "H" : "V";
                g_object_set ( gmpscan.dvbsrc_tune, gst_param_dvb_descr_n[c].gst_param, pol, NULL );
            }
            else
                g_object_set ( gmpscan.dvbsrc_tune, gst_param_dvb_descr_n[c].gst_param,
                    gst_param_dvb_descr_n[c].dvb_descr[num].descr_num, NULL );

            g_debug ( "name = %s | num = %d | gst_param = %s | descr_text_vis = %s | descr_num = %d \n",
					  name, num, gst_param_dvb_descr_n[c].gst_param,
					  gst_param_dvb_descr_n[c].dvb_descr[num].text_vis,
					  gst_param_dvb_descr_n[c].dvb_descr[num].descr_num );
        }
}

static GtkBox * gmp_scan_dvb_all  ( guint num, const struct DvbTypes *dvball, const gchar *type )
{
	g_debug ( "gmp_scan_dvb_all:: %s \n", type );
	
    GtkBox *g_box = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_VERTICAL, 0 );
    gtk_widget_set_margin_start ( GTK_WIDGET ( g_box ), 10 );
    gtk_widget_set_margin_end   ( GTK_WIDGET ( g_box ), 10 );

    GtkGrid *grid = (GtkGrid *)gtk_grid_new();
    gtk_grid_set_column_homogeneous ( GTK_GRID ( grid ), TRUE );
    gtk_box_pack_start ( g_box, GTK_WIDGET ( grid ), TRUE, TRUE, 10 );

    GtkLabel *label;
    GtkSpinButton *spinbutton;
    GtkComboBoxText *scan_combo_box;

	gint d_data = 0;
    guint d = 0, c = 0, z = 0;
    for ( d = 0; d < num; d++ )
    {
        label = (GtkLabel *)gtk_label_new ( dvball[d].param );
        gtk_widget_set_halign ( GTK_WIDGET ( label ), GTK_ALIGN_START );
        gtk_grid_attach ( GTK_GRID ( grid ), GTK_WIDGET ( label ), 0, d, 1, 1 );

        if ( !dvball[d].descr )
        {
            spinbutton = (GtkSpinButton *) gtk_spin_button_new_with_range ( 0, 30000000, 1 ); // kHz ( 0 - 30 GHz )
            gtk_grid_attach ( GTK_GRID ( grid ), GTK_WIDGET ( spinbutton ), 1, d, 1, 1 );
            if ( g_strrstr ( dvball[d].param, "Stream ID" ) )
            {
                gtk_spin_button_set_range ( spinbutton, -1, 255 );
                gtk_spin_button_set_value ( spinbutton, -1 );
                g_object_set ( gmpscan.dvbsrc_tune, "stream-id", -1, NULL );
            }
            if ( g_strrstr ( dvball[d].param, "Bandwidth" ) )
            {
                gtk_spin_button_set_range ( spinbutton, 0, 25000000 );
                gtk_spin_button_set_value ( spinbutton, 8000000 );
            }
            g_signal_connect ( spinbutton, "changed", G_CALLBACK ( gmp_scan_changed_spin_all ), label );
        }
        else
        {
            scan_combo_box = (GtkComboBoxText *) gtk_combo_box_text_new ();
            gtk_grid_attach ( GTK_GRID ( grid ), GTK_WIDGET ( scan_combo_box ), 1, d, 1, 1 );

            for ( c = 0; c < G_N_ELEMENTS ( gst_param_dvb_descr_n ); c++ )
            {
              if ( g_strrstr ( dvball[d].param, gst_param_dvb_descr_n[c].name ) )
              {
                for ( z = 0; z < gst_param_dvb_descr_n[c].cdsc; z++ )
                    gtk_combo_box_text_append_text ( scan_combo_box, gst_param_dvb_descr_n[c].dvb_descr[z].text_vis );

                if ( g_strrstr ( gst_param_dvb_descr_n[c].gst_param, "polarity" ) || g_strrstr ( gst_param_dvb_descr_n[c].gst_param, "lnb-type" ) )
                    continue;
                    
                g_object_get ( gmpscan.dvbsrc_tune, gst_param_dvb_descr_n[c].gst_param, &d_data, NULL );
                gtk_combo_box_set_active ( GTK_COMBO_BOX ( scan_combo_box ), d_data );
              }
            }

            if ( g_strrstr ( dvball[d].param, "DiSEqC" ) )
            {                
                g_object_get ( gmpscan.dvbsrc_tune, "diseqc-source", &d_data, NULL );
                gtk_combo_box_set_active ( GTK_COMBO_BOX ( scan_combo_box ), d_data+1 );
            }

            if ( g_strrstr ( dvball[d].param, "LNB" ) )
				gtk_combo_box_set_active ( GTK_COMBO_BOX ( scan_combo_box ), lnb_type );

            if ( g_strrstr ( dvball[d].param, "Polarity" ) )
            {
				gchar *s_data = NULL;
				g_object_get ( gmpscan.dvbsrc_tune, "polarity", &s_data, NULL );
				gtk_combo_box_set_active ( GTK_COMBO_BOX ( scan_combo_box ), ( s_data[0] == 'H' ) ? 0 : 1 );
				g_free ( s_data );
			}

            if ( gtk_combo_box_get_active ( GTK_COMBO_BOX ( scan_combo_box ) ) == -1 )
                 gtk_combo_box_set_active ( GTK_COMBO_BOX ( scan_combo_box ), 0 );
            g_signal_connect ( scan_combo_box, "changed", G_CALLBACK ( gmp_scan_changed_combo_all ), label );
        }
    }

    return g_box;
}

static GtkBox * gmp_scan_create_sgn_snr ()
{
	g_debug ( "gmp_scan_create_sgn_snr:: \n" );
	
    GtkBox *tbar_dvb = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_VERTICAL, 0 );
    gtk_widget_set_margin_start ( GTK_WIDGET ( tbar_dvb ), 5 );
    gtk_widget_set_margin_end   ( GTK_WIDGET ( tbar_dvb ), 5 );

    gmpscan.scan_sgn_snr = (GtkLabel *)gtk_label_new ( "Level  &  Quality" );
    gtk_box_pack_start ( tbar_dvb, GTK_WIDGET ( gmpscan.scan_sgn_snr  ), FALSE, FALSE, 5 );

    gmpscan.bar_scan_sgn = (GtkProgressBar *)gtk_progress_bar_new ();
    gmpscan.bar_scan_snr = (GtkProgressBar *)gtk_progress_bar_new ();
    
    gtk_box_pack_start ( tbar_dvb, GTK_WIDGET ( gmpscan.bar_scan_sgn  ), FALSE, FALSE, 0 );
    gtk_box_pack_start ( tbar_dvb, GTK_WIDGET ( gmpscan.bar_scan_snr  ), FALSE, FALSE, 3 );

    return tbar_dvb;
}

static GtkBox * gmp_scan_all_box ( guint i )
{
	g_debug ( "gmp_scan_all_box:: \n" );
	
    GtkBox *only_box = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_VERTICAL, 0 );

    if ( i == PAGE_SC )  { return gmp_scan_device   (); }
    if ( i == PAGE_CH )  { return gmp_scan_channels (); }
    if ( i == PAGE_DT )  { return gmp_scan_dvb_all  ( G_N_ELEMENTS ( dvbt_props_n ), dvbt_props_n, "DVB-T" ); }
    if ( i == PAGE_DS )  { return gmp_scan_dvb_all  ( G_N_ELEMENTS ( dvbs_props_n ), dvbs_props_n, "DVB-S" ); }
    if ( i == PAGE_DC )  { return gmp_scan_dvb_all  ( G_N_ELEMENTS ( dvbc_props_n ), dvbc_props_n, "DVB-C" ); }
    if ( i == PAGE_AT )  { return gmp_scan_dvb_all  ( G_N_ELEMENTS ( atsc_props_n ), atsc_props_n, "ATSC"  ); }
    if ( i == PAGE_DM )  { return gmp_scan_dvb_all  ( G_N_ELEMENTS ( dtmb_props_n ), dtmb_props_n, "DTMB"  ); }

    return only_box;
}


static void gmp_scan_quit ( GtkWindow *window )
{
    gtk_widget_destroy ( GTK_WIDGET ( window ) );
    
    g_debug ( "gmp_scan_quit:: \n" );
}

static void gmp_scan_close ( GtkButton *button, GtkWindow *window )
{
    gmp_scan_quit ( window );
    
    g_debug ( "gmp_scan_close:: widget name %s", gtk_widget_get_name ( GTK_WIDGET ( button ) ) );
}

void gmp_scan_win ()
{	
    GtkWindow *window =      (GtkWindow *)gtk_window_new ( GTK_WINDOW_TOPLEVEL );
    gtk_window_set_transient_for ( window, gmp_base_win_ret () );
	gtk_window_set_title     ( window, _("Scanner") );
    gtk_window_set_modal     ( window, TRUE );
    gtk_window_set_position  ( window, GTK_WIN_POS_CENTER_ON_PARENT );
	g_signal_connect         ( window, "destroy", G_CALLBACK ( gmp_scan_quit ), NULL );

    GtkBox *m_box = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_VERTICAL,   0 );
    GtkBox *h_box = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_HORIZONTAL, 0 );

    gmpscan.notebook = (GtkNotebook *)gtk_notebook_new ();
    gtk_notebook_set_scrollable ( gmpscan.notebook, TRUE );

    GtkBox *m_box_n[PAGE_NUM];

	guint j = 0;
    for ( j = 0; j < PAGE_NUM; j++ )
    {
        m_box_n[j] = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_VERTICAL, 0 );
        gtk_box_pack_start ( m_box_n[j], GTK_WIDGET ( gmp_scan_all_box ( j ) ), TRUE, TRUE, 0 );
        gtk_notebook_append_page ( gmpscan.notebook, GTK_WIDGET ( m_box_n[j] ),  gtk_label_new ( _(gmp_scan_label_n[j].name) ) );
    
		if ( j == PAGE_SC )
		{
			gtk_box_pack_start ( m_box_n[PAGE_SC], GTK_WIDGET ( gmp_scan_create_sgn_snr () ), FALSE, FALSE, 0 );
		
			GtkBox *hb_box = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_HORIZONTAL, 0 );
			
			GtkButton *button_start = gmp_scan_button ( "gmp-media-start", 16, gmp_scan_start, NULL );
			GtkButton *button_stop  = gmp_scan_button ( "gmp-media-stop",  16, gmp_scan_stop,  NULL );
			
			gtk_box_pack_start ( hb_box, GTK_WIDGET ( button_start ), TRUE, TRUE, 5 );
			gtk_box_pack_start ( hb_box, GTK_WIDGET ( button_stop ), TRUE, TRUE, 5 );

			gtk_box_pack_start ( m_box_n[PAGE_SC], GTK_WIDGET ( hb_box ), FALSE, FALSE, 5 );
		}
	}
	
    gtk_notebook_set_tab_pos ( gmpscan.notebook, GTK_POS_TOP );
    gtk_box_pack_start ( m_box, GTK_WIDGET (gmpscan.notebook), TRUE, TRUE, 0 );

	gtk_box_pack_end ( h_box, GTK_WIDGET ( gmp_scan_button ( "gmp-exit",  16, gmp_scan_close,  window ) ), TRUE, TRUE, 5 );

    gtk_box_pack_start ( m_box, GTK_WIDGET ( h_box ), FALSE, FALSE, 5 );
    gtk_container_set_border_width ( GTK_CONTAINER ( m_box ), 5 );
    gtk_container_add ( GTK_CONTAINER ( window ), GTK_WIDGET ( m_box ) );

    gtk_widget_show_all ( GTK_WIDGET ( window ) );
    
    gtk_widget_set_opacity ( GTK_WIDGET ( window ), opacity_win );
    
    g_debug ( "gmp_scan_win:: \n" );
}
