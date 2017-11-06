/*
 * Copyright 2014 - 2017 Stepan Perun
 * This program is free software.
 * License: GNU LESSER GENERAL PUBLIC LICENSE
 * http://www.gnu.org/licenses/lgpl.html
*/

#include <stdlib.h>
#include <fcntl.h>
#include <time.h>

#include <linux/dvb/frontend.h>
#include <sys/ioctl.h>

#define GST_USE_UNSTABLE_API
#include <gst/mpegts/mpegts.h>

#include "gtv-scan.h"


gint DVB_DELSYS = SYS_UNDEFINED;
const gchar *dvb_type_str = "UNDEFINED";

static guint j = 0, c = 0;
guint adapter_ct = 0, frontend_ct = 0, lnb_type = 0;
gchar *pol = "H";
time_t t_start, t_cur;

static GstElement *dvb_scan, *dvbsrc_tune;
static GtkLabel *scan_snr_dvbt, *scan_snr_dvbs, *scan_snr_dvbc;

static void tv_scan_set_all_ch ( gint all_ch, gint c_tv, gint c_ro );
static void tv_scan_stop ( GtkButton *button, gpointer data );


// Mini GST scanner

void dvb_mpegts_initialize ()
{
    gst_mpegts_initialize ();
    g_type_class_ref ( GST_TYPE_MPEGTS_STREAM_TYPE          );
    g_type_class_ref ( GST_TYPE_MPEGTS_DVB_SERVICE_TYPE     );
    g_type_class_ref ( GST_TYPE_MPEGTS_DESCRIPTOR_TYPE      );
    g_type_class_ref ( GST_TYPE_MPEGTS_DVB_DESCRIPTOR_TYPE  );
    g_type_class_ref ( GST_TYPE_MPEGTS_ISO639_AUDIO_TYPE    );
    g_type_class_ref ( GST_TYPE_MPEGTS_RUNNING_STATUS       );
}

const gchar * enum_name ( GType instance_type, gint val )
{
    GEnumValue *env = g_enum_get_value ( G_ENUM_CLASS ( g_type_class_peek ( instance_type ) ), val );

    if ( !env )
        return "UNKNOWN";

    return env->value_nick;
}

#define MAX_RUN_PAT 128

static struct dvb_gst_scan_sdt { guint pmn_pid; gchar *name;             } dvb_gst_scan_sdt_n[MAX_RUN_PAT];
static struct dvb_gst_scan_pat { guint pmn_pid; guint  nmap;             } dvb_gst_scan_pat_n[MAX_RUN_PAT];
static struct dvb_gst_scan_pmt { guint pmn_pid; guint  vpid; guint apid; } dvb_gst_scan_pmt_n[MAX_RUN_PAT];

static gint pat_count = 0, pmt_count = 0, sdt_count = 0;
static gboolean pat_done = FALSE, pmt_done = FALSE, sdt_done = FALSE;

static void start_clear_dvb_gst_scan ()
{
    sdt_done = FALSE;
    pat_done = FALSE;
    pmt_done = FALSE;

    pat_count = 0;
    pmt_count = 0;
    sdt_count = 0;

    gint i;
    for ( i = 0; i < MAX_RUN_PAT; i++ )
    {
        dvb_gst_scan_pmt_n[i].pmn_pid = 0;
        dvb_gst_scan_pmt_n[i].apid = 0;
        dvb_gst_scan_pmt_n[i].vpid = 0;

        dvb_gst_scan_pat_n[i].pmn_pid = 0;
        dvb_gst_scan_pat_n[i].nmap = 0;

        dvb_gst_scan_sdt_n[i].pmn_pid = 0;
        dvb_gst_scan_sdt_n[i].name = NULL;
    }
}

static void dump_pat ( GstMpegtsSection *section )
{
    GPtrArray *pat = gst_mpegts_section_get_pat ( section );
    g_print ( "\nPAT: %d Programs \n", pat->len );

    guint i = 0;
    for ( i = 0; i < pat->len; i++ )
    {
        if ( i >= MAX_RUN_PAT ) break;

        GstMpegtsPatProgram *patp = g_ptr_array_index ( pat, i );

        if ( patp->program_number == 0 ) continue;

        dvb_gst_scan_pat_n[pat_count].pmn_pid = patp->program_number;
        dvb_gst_scan_pat_n[pat_count].nmap    = patp->network_or_program_map_PID;
        pat_count++;

        g_print ( "     Program number: %6d (0x%04x) | network or pg-map pid: 0x%04x \n",
            patp->program_number, patp->program_number, patp->network_or_program_map_PID );
    }

    g_ptr_array_unref ( pat );

    pat_done = TRUE;
    ( i >= MAX_RUN_PAT ) ? g_print ( "MAX %d: PAT scan stop \n", MAX_RUN_PAT ) : g_print ( "PAT Done  \n\n" );
}

static void dump_pmt ( GstMpegtsSection *section )
{
    if ( pmt_count >= MAX_RUN_PAT )
    {
        g_print ( "MAX %d: PMT scan stop \n", MAX_RUN_PAT );
        return;
    }

    guint i = 0, len = 0;
    gboolean first_audio = TRUE;

    const GstMpegtsPMT *pmt = gst_mpegts_section_get_pmt ( section );
    len = pmt->streams->len;
    dvb_gst_scan_pmt_n[pmt_count].pmn_pid = pmt->program_number;

    g_print ( "\nPMT: %d  ( %d ) \n", pmt_count+1, len );

    g_print ( "     Program number     : %d (0x%04x) \n", pmt->program_number, pmt->program_number );
    g_print ( "     Pcr pid            : %d (0x%04x) \n", pmt->pcr_pid, pmt->pcr_pid );
    g_print ( "     %d Streams: \n", len );

    for ( i = 0; i < len; i++ )
    {
        GstMpegtsPMTStream *stream = g_ptr_array_index (pmt->streams, i);

        g_print ( "       pid: %d (0x%04x) , stream_type:0x%02x (%s) \n", stream->pid, stream->pid, stream->stream_type,
            enum_name (GST_TYPE_MPEGTS_STREAM_TYPE, stream->stream_type) );

        const gchar *name_t = enum_name ( GST_TYPE_MPEGTS_STREAM_TYPE, stream->stream_type );

        if ( g_strrstr ( name_t, "video" ) )
            dvb_gst_scan_pmt_n[pmt_count].vpid = stream->pid;

        if ( g_strrstr ( name_t, "audio" ) )
        {
            if ( first_audio ) dvb_gst_scan_pmt_n[pmt_count].apid = stream->pid;

            first_audio = FALSE;
        }
    }
    pmt_count++;

    if ( pat_count > 0 )
    if ( pmt_count == pat_count )
    {
        pmt_done = TRUE;
        g_print ( "\nPMT Done \n\n" );
    }
}

static void dump_sdt ( GstMpegtsSection *section )
{
    if ( sdt_count >= MAX_RUN_PAT )
    {
        g_print ( "MAX %d: SDT scan stop \n", MAX_RUN_PAT );
        return;
    }

    guint i = 0, len = 0;
    gint  z = 0;

    const GstMpegtsSDT *sdt = gst_mpegts_section_get_sdt ( section );

    len = sdt->services->len;
    g_print ( "Services: %d  ( %d ) \n", sdt_count+1, len );

    for ( i = 0; i < len; i++ )
    {
        GstMpegtsSDTService *service = g_ptr_array_index ( sdt->services, i );

        dvb_gst_scan_sdt_n[sdt_count].name = g_strdup_printf ( "PGMN-%d", service->service_id);
        dvb_gst_scan_sdt_n[sdt_count].pmn_pid = service->service_id;

        gboolean get_descr = FALSE;

        if ( pat_done )
        {
            for ( z = 0; z < pat_count; z++ )
                if ( dvb_gst_scan_pat_n[z].pmn_pid == service->service_id )
                    {  get_descr = TRUE; break; }
        }

        if ( !get_descr ) continue;

        dvb_gst_scan_sdt_n[sdt_count].name = g_strdup_printf ( "PGMN-%d", service->service_id );
        g_print ( "     Service id: %d  %d 0x%04x \n", sdt_count+1, service->service_id, service->service_id );

        GPtrArray *descriptors = service->descriptors;
        for ( c = 0; c < descriptors->len; c++ )
        {
            GstMpegtsDescriptor *desc = g_ptr_array_index ( descriptors, c );

            gchar *service_name, *provider_name;
            GstMpegtsDVBServiceType service_type;

            if ( desc->tag == GST_MTS_DESC_DVB_SERVICE )
            if ( gst_mpegts_descriptor_parse_dvb_service ( desc, &service_type, &service_name, &provider_name ) )
            {
                dvb_gst_scan_sdt_n[sdt_count].name = g_strdup ( service_name );

                g_print ( "   Service Descriptor, type:0x%02x (%s) \n",
                    service_type, enum_name (GST_TYPE_MPEGTS_DVB_SERVICE_TYPE, service_type) );
                g_print ( "      Service  (name) : %s \n", service_name  );
                g_print ( "      Provider (name) : %s \n", provider_name );

                g_free ( service_name  );
                g_free ( provider_name );
            }
        }
        sdt_count++;
    }

    if ( pat_count > 0 && pmt_count > 0 )
    if ( sdt_count == pat_count || sdt_count == pmt_count )
    {
        sdt_done = TRUE;
        g_print ( "SDT Done \n\n" );
    }
}

static void parse_gst_mpegts_section ( GstMessage *message )
{
    GstMpegtsSection *section = gst_message_parse_mpegts_section ( message );
    if ( section )
    {
        switch ( GST_MPEGTS_SECTION_TYPE ( section ) )
        {
            case GST_MPEGTS_SECTION_PAT:
                dump_pat (section);
                break;
            case GST_MPEGTS_SECTION_PMT:
                dump_pmt (section);
                break;
            case GST_MPEGTS_SECTION_SDT:
                dump_sdt (section);
                break;

            default:
                break;
        }
        gst_mpegts_section_unref ( section );
    }

    if ( pat_done && pmt_done && sdt_done )
      tv_scan_stop ( NULL, NULL );

    time ( &t_cur );
    if ( ( t_cur - t_start ) >= 10 )
    {
        g_print ( "Warning. Time stop %ld (sec) \n", t_cur - t_start );
        tv_scan_stop ( NULL, NULL );
    }
}

static void scan_msg_all ( GstBus *bus, GstMessage *message )
{
    const GstStructure *structure = gst_message_get_structure ( message );

    if ( structure )
    {
        gint signal, snr;
        gboolean hlook = FALSE;

        if (  gst_structure_get_int ( structure, "signal", &signal )  )
        {
            gst_structure_get_boolean ( structure, "lock", &hlook );
            gst_structure_get_int ( structure, "snr", &snr);

            tv_set_sgn_snr ( dvb_scan, scan_snr_dvbt, (signal * 100) / 0xffff, (snr * 100) / 0xffff, hlook );
            tv_set_sgn_snr ( dvb_scan, scan_snr_dvbs, (signal * 100) / 0xffff, (snr * 100) / 0xffff, hlook );
            tv_set_sgn_snr ( dvb_scan, scan_snr_dvbc, (signal * 100) / 0xffff, (snr * 100) / 0xffff, hlook );

            //g_print ( "signal %d | snr %d\n", signal*100/0xffff, snr*100/0xffff );
        }
    }
    parse_gst_mpegts_section ( message );
}

static void scan_msg_err ( GstBus *bus, GstMessage *msg )
{
    GError *err = NULL;
    gchar *dbg = NULL;

    gst_message_parse_error ( msg, &err, &dbg );
    g_printerr ( "ERROR: %s (%s)\n", err->message, (dbg) ? dbg : "no details" );

    //tv_message_dialog ( err->message, (dbg) ? dbg : " ", GTK_MESSAGE_ERROR );

    g_error_free ( err );
    g_free ( dbg );
}

static void scan_set_tune ()
{
    tv_set_tuning_timeout ( dvbsrc_tune );
}

static void scan_gst_create ()
{
    GstElement *dvbsrc_parse, *dvbsrc_files;

    dvb_scan     = gst_pipeline_new ( "pipeline_scan" );
    dvbsrc_tune  = gst_element_factory_make ( "dvbsrc",   NULL );
    dvbsrc_parse = gst_element_factory_make ( "tsparse",  NULL );
    dvbsrc_files = gst_element_factory_make ( "filesink", NULL );

    if ( !dvb_scan || !dvbsrc_tune || !dvbsrc_parse || !dvbsrc_files )
        g_printerr ("dvb_scan - not be created.\n");

    gst_bin_add_many ( GST_BIN ( dvb_scan ), dvbsrc_tune, dvbsrc_parse, dvbsrc_files, NULL );
    gst_element_link_many ( dvbsrc_tune, dvbsrc_parse, dvbsrc_files, NULL );

    g_object_set ( dvbsrc_files, "location", "/dev/null", NULL);

    GstBus *bus_scan = gst_element_get_bus ( dvb_scan );
    gst_bus_add_signal_watch ( bus_scan );
    gst_object_unref ( bus_scan );

    g_signal_connect ( bus_scan, "message",        G_CALLBACK ( scan_msg_all ), NULL );
    g_signal_connect ( bus_scan, "message::error", G_CALLBACK ( scan_msg_err ), NULL );

    scan_set_tune ();
}

// End Mini GST scanner

GtkNotebook *notebook;
GtkLabel *all_channels;
GtkTreeView *scan_treeview;
static void tv_scan_add_to_treeview ( gchar *name_ch, gchar *data );

const struct labels_scan { gchar *name; } labels_scan_n[] =
{
    { " General " }, { " DVB-T/T2 " }, { " DVB-S/S2 " }, { " DVB-C " }, { " Channels " }
};

const gchar *dvbt[] =
{
    " Frequency  MHz",
    " Bandwidth ",
    " Inversion ",
    " Code Rate HP ",
    " Code Rate LP ",
    " Modulation ",
    " Transmission ",
    " Guard interval ",
    " Hierarchy ",
    " Stream ID "
};

const gchar *dvbs[] =
{
    " DiSEqC ",
    " Frequency  MHz",
    " Symbol rate  kBd",
    " Polarity ",
    " FEC ",
    " Modulation ",
    " Pilot ",
    " Rolloff ",
    " Stream ID ",
    " LNB "
};

const gchar *dvbc[] =
{
    " Frequency  MHz",
    " Symbol rate  kBd",
    " Inversion ",
    " Code Rate HP ",
    " Modulation "
};

// 1 digits; 2 enum descriptors

const gint dvbet[] = { 1, 1, 2, 2, 2, 2, 2, 2, 2, 1 };
const gint dvbes[] = { 2, 1, 1, 2, 2, 2, 2, 2, 1, 2 };
const gint dvbec[] = { 1, 1, 2, 2, 2 };


const struct dvb_descr_all { gint descr_num; const gchar *dvb_v5_name; const gchar *text_vis; } dvb_descr_all_n;

const struct dvb_descr_all dvb_descr_inversion_type_n[] =
{
    { INVERSION_OFF,  "OFF",  "Off"  },
    { INVERSION_ON,   "ON",   "On"   },
    { INVERSION_AUTO, "AUTO", "Auto" }
};
const struct dvb_descr_all dvb_descr_coderate_type_n[] =
{
    { FEC_NONE, "NONE", "None" },
    { FEC_1_2,  "1/2",  "1/2"  },
    { FEC_2_3,  "2/3",  "2/3"  },
    { FEC_3_4,  "3/4",  "3/4"  },
    { FEC_4_5,  "4/5",  "4/5"  },
    { FEC_5_6,  "5/6",  "5/6"  },
    { FEC_6_7,  "6/7",  "6/7"  },
    { FEC_7_8,  "7/8",  "7/8"  },
    { FEC_8_9,  "8/9",  "8/9"  },
    { FEC_AUTO, "AUTO", "Auto" },
    { FEC_3_5,  "3/5",  "3/5"  },
    { FEC_9_10, "9/10", "9/10" },
    { FEC_2_5,  "2/5",  "2/5"  }
};
const struct dvb_descr_all dvb_descr_modulation_type_n[] =
{
    { QPSK,     "QPSK",     "QPSK"     },
    { QAM_16,   "QAM/16",   "QAM 16"   },
    { QAM_32,   "QAM/32",   "QAM 32"   },
    { QAM_64,   "QAM/64",   "QAM 64"   },
    { QAM_128,  "QAM/128",  "QAM 128"  },
    { QAM_256,  "QAM/256",  "QAM 256"  },
    { QAM_AUTO, "QAM/AUTO", "Auto"     },
    { VSB_8,    "VSB/8",    "VSB 8"    },
    { VSB_16,   "VSB/16",   "VSB 16"   },
    { PSK_8,    "PSK/8",    "PSK 8"    },
    { APSK_16,  "APSK/16",  "APSK 16"  },
    { APSK_32,  "APSK/32",  "APSK 32"  },
    { DQPSK,    "DQPSK",    "DQPSK"    },
    { QAM_4_NR, "QAM/4_NR", "QAM 4 NR" }
};
const struct dvb_descr_all dvb_descr_transmode_type_n[] =
{
    { TRANSMISSION_MODE_2K,   "2K",   "2K"   },
    { TRANSMISSION_MODE_8K,   "8K",   "8K"   },
    { TRANSMISSION_MODE_AUTO, "AUTO", "Auto" },
    { TRANSMISSION_MODE_4K,   "4K",   "4K"   },
    { TRANSMISSION_MODE_1K,   "1K",   "1K"   },
    { TRANSMISSION_MODE_16K,  "16K",  "16K"  },
    { TRANSMISSION_MODE_32K,  "32K",  "32K"  },
    { TRANSMISSION_MODE_C1,   "C1",   "C1"   },
    { TRANSMISSION_MODE_C3780,"C3780","C3780"}
};
const struct dvb_descr_all dvb_descr_guard_type_n[] =
{
    { GUARD_INTERVAL_1_32,   "1/32",   "32"     },
    { GUARD_INTERVAL_1_16,   "1/16",   "16"     },
    { GUARD_INTERVAL_1_8,    "1/8",    "8"      },
    { GUARD_INTERVAL_1_4,    "1/4",    "4"      },
    { GUARD_INTERVAL_AUTO,   "AUTO",   "Auto"   },
    { GUARD_INTERVAL_1_128,  "1/128",  "128"    },
    { GUARD_INTERVAL_19_128, "19/128", "19/128" },
    { GUARD_INTERVAL_19_256, "19/256", "19/256" },
    { GUARD_INTERVAL_PN420,  "PN420",  "PN 420" },
    { GUARD_INTERVAL_PN595,  "PN595",  "PN 595" },
    { GUARD_INTERVAL_PN945,  "PN945",  "PN 945" }
};
const struct dvb_descr_all dvb_descr_hierarchy_type_n[] =
{
    { HIERARCHY_NONE, "NONE", "None" },
    { HIERARCHY_1,    "1",    "1"    },
    { HIERARCHY_2,    "2",    "2"    },
    { HIERARCHY_4,    "4",    "4"    },
    { HIERARCHY_AUTO, "AUTO", "Auto" }

};
const struct dvb_descr_all dvb_descr_pilot_type_n[] =
{
    { PILOT_ON,   "ON",   "On"   },
    { PILOT_OFF,  "OFF",  "Off"  },
    { PILOT_AUTO, "AUTO", "Auto" }
};
const struct dvb_descr_all dvb_descr_roll_type_n[] =
{
    { ROLLOFF_35,   "35",   "35"   },
    { ROLLOFF_20,   "20",   "20"   },
    { ROLLOFF_25,   "25",   "25"   },
    { ROLLOFF_AUTO, "AUTO", "Auto" }
};
const struct dvb_descr_all dvb_descr_polarity_type_n[] =
{
    { SEC_VOLTAGE_18,  "HORIZONTAL", "H  18V" },
    { SEC_VOLTAGE_13,  "VERTICAL",   "V  13V" },
    { SEC_VOLTAGE_18,  "LEFT",       "L  18V" },
    { SEC_VOLTAGE_13,  "RIGHT",      "R  13V" },
    { SEC_VOLTAGE_OFF, "OFF",        "Off"    }
};

const struct dvb_descr_all dvb_descr_lnb_type_n[] =
{
    { 0, "UNIVERSAL", "Universal" },
    { 1, "DBS",	      "DBS"       },
    { 2, "EXTENDED",  "Extended"  },
    { 3, "STANDARD",  "Standard"  },
    { 4, "ENHANCED",  "Enhanced"  },
    { 5, "C-BAND",	  "C-Band"    },
    { 6, "C-MULT",	  "C-Mult"    },
    { 7, "DISHPRO",	  "Dishpro"   },
    { 8, "110BS",	  "110 BS"    },
};

struct lnb_types_lhs { gchar	*name; guint low_val; guint high_val; guint switch_val; } lnb_n[] =
{
	{ "UNIVERSAL",  9750000,  10600000, 11700000 },
 	{ "DBS",		11250000, 0, 0               },
 	{ "EXTENDED",   9750000,  10600000, 11700000 },
	{ "STANDARD",	10000000, 0, 0               },
	{ "ENHANCED",	9750000,  0, 0               },
	{ "C-BAND",		5150000,  0, 0               },
	{ "C-MULT",		5150000,  5750000,  0        },
	{ "DISHPRO",	11250000, 14350000, 0        },
	{ "110BS",		10678000, 0, 0               }
};
void tv_set_lnb ( GstElement *element, gint num_lnb )
{
    g_object_set ( element, "lnb-lof1", lnb_n[num_lnb].low_val,    NULL );
    g_object_set ( element, "lnb-lof2", lnb_n[num_lnb].high_val,   NULL );
    g_object_set ( element, "lnb-slof", lnb_n[num_lnb].switch_val, NULL );
}

struct gst_param_dvb_descr_all { gchar *name; const gchar *dvb_v5_name; gchar *gst_param; const struct dvb_descr_all *dvb_descr; guint cdsc; } gst_param_dvb_descr_n[] =
{
    // descr
    { "Inversion",      "INVERSION",         "inversion",        dvb_descr_inversion_type_n,  G_N_ELEMENTS (dvb_descr_inversion_type_n)  },
    { "Code Rate HP",   "CODE_RATE_HP",      "code-rate-hp",     dvb_descr_coderate_type_n,   G_N_ELEMENTS (dvb_descr_coderate_type_n)   },
    { "Code Rate LP",   "CODE_RATE_LP",      "code-rate-lp",     dvb_descr_coderate_type_n,   G_N_ELEMENTS (dvb_descr_coderate_type_n)   },
    { "FEC",            "INNER_FEC",         "code-rate-hp",     dvb_descr_coderate_type_n,   G_N_ELEMENTS (dvb_descr_coderate_type_n)   },
    { "Modulation",     "MODULATION",        "modulation",       dvb_descr_modulation_type_n, G_N_ELEMENTS (dvb_descr_modulation_type_n) },
    { "Transmission",   "TRANSMISSION_MODE", "trans-mode",       dvb_descr_transmode_type_n,  G_N_ELEMENTS (dvb_descr_transmode_type_n)  },
    { "Guard interval", "GUARD_INTERVAL",    "guard",            dvb_descr_guard_type_n,      G_N_ELEMENTS (dvb_descr_guard_type_n)      },
    { "Hierarchy",      "HIERARCHY",         "hierarchy",        dvb_descr_hierarchy_type_n,  G_N_ELEMENTS (dvb_descr_hierarchy_type_n)  },
    { "Pilot",          "PILOT",             "pilot",            dvb_descr_pilot_type_n,      G_N_ELEMENTS (dvb_descr_pilot_type_n)      },
    { "Rolloff",        "ROLLOFF",           "rolloff",          dvb_descr_roll_type_n,       G_N_ELEMENTS (dvb_descr_roll_type_n)       },
    { "Polarity",       "POLARIZATION",      "polarity",         dvb_descr_polarity_type_n,   G_N_ELEMENTS (dvb_descr_polarity_type_n)   },
    { "LNB",            "LNB",               "lnb-type",         dvb_descr_lnb_type_n,        G_N_ELEMENTS (dvb_descr_lnb_type_n)        },

    // digits
    { "Frequency",      "FREQUENCY",         "frequency",        NULL, 0 },
    { "Bandwidth",      "BANDWIDTH_HZ",      "bandwidth-hz",     NULL, 0 },
    { "Symbol rate",    "SYMBOL_RATE",       "symbol-rate",      NULL, 0 },
    { "Stream ID",      "STREAM_ID",         "stream-id",        NULL, 0 },
    { "DiSEqC",         "SAT_NUMBER",        "diseqc-source",    NULL, 0 },
    { "Service Id",     "SERVICE_ID",        "program-number",   NULL, 0 },
    { "Audio Pid",      "AUDIO_PID",         "audio-pid",        NULL, 0 },
    { "Video Pid",      "VIDEO_PID",         "video-pid",        NULL, 0 }
};

static gchar * tv_strip_ch_name ( gchar *name )
{
    guint i = 0;
    for ( i = 0; name[i] != '\0'; i++ )
    {
        if ( g_ascii_isprint ( name[i] ) )
        {
            if ( name[i] == ':' || name[i] == '[' || name[i] == ']' ) name[i] = ' ';
        }
        else
            name[i] = ' ';
    }
    return g_strstrip ( name );
}

static void tv_convert_dvb5 ( const gchar *filename )
{
    guint n = 0, z = 0, x = 0;
    gchar *contents;
    GError *err = NULL;

    GString *gstring = g_string_new ( "# Convert DVB 5 format" );

    if ( g_file_get_contents ( filename, &contents, 0, &err ) )
    {
        gchar **lines = g_strsplit ( contents, "\n", 0 );

        for ( n = 0; lines[n] != NULL; n++ )
        {
            if ( g_str_has_prefix ( lines[n], "[" ) )
            {
                gstring = g_string_append ( gstring, "\n" );
                g_string_append_printf ( gstring, "%s", tv_strip_ch_name ( lines[n] ) );
                g_string_append_printf ( gstring, ":adapter=%d:frontend=%d", adapter_ct, frontend_ct );
            }

            for ( z = 0; z < G_N_ELEMENTS ( gst_param_dvb_descr_n ); z++ )
            {
                if ( g_strrstr ( lines[n], gst_param_dvb_descr_n[z].dvb_v5_name ) )
                {
                    gchar **value_key = g_strsplit ( lines[n], " = ", 0 );

                    if ( gst_param_dvb_descr_n[z].cdsc == 0 )
                    {
                        guint data = atoi ( value_key[1] );
                        if ( g_strrstr ( value_key[0], "SYMBOL_RATE" ) && data > 100000 ) data /= 1000;
                        g_string_append_printf ( gstring, ":%s=%d", gst_param_dvb_descr_n[z].gst_param, data );
                    }
                    else
                    {
                        for ( x = 0; x < gst_param_dvb_descr_n[z].cdsc; x++ )
                        {
                            if ( g_strrstr ( value_key[1], gst_param_dvb_descr_n[z].dvb_descr[x].dvb_v5_name ) )
                            {
                                if ( g_strrstr ( value_key[0], "POLARIZATION" ) )
                                {
                                    g_string_append_printf ( gstring, ":%s=%s",
                                        gst_param_dvb_descr_n[z].gst_param,
                                        gst_param_dvb_descr_n[z].dvb_descr[x].descr_num ? "H" : "V" );
                                }
                                else
                                {
                                    g_string_append_printf ( gstring, ":%s=%d",
                                        gst_param_dvb_descr_n[z].gst_param,
                                        gst_param_dvb_descr_n[z].dvb_descr[x].descr_num );
                                }
                            }
                        }
                    }
                }
            }
        }

        g_strfreev ( lines );
        g_free ( contents );
    }
    else
    {
        g_critical ( "ERROR: %s\n", err->message );
        if ( err ) g_error_free ( err );
        g_string_free ( gstring, FALSE );
        return;
    }

    // g_print ( "All data: %s", gstring->str );

    // tv_treeview_clear ( tv_treeview );

        gchar **lines_all = g_strsplit ( gstring->str, "\n", 0 );

            for ( n = 0; lines_all[n] != NULL; n++ )
			{
				if ( g_strrstr ( lines_all[n], "audio-pid" ) || g_strrstr ( lines_all[n], "video-pid" ) )
					tv_str_split_ch_data ( lines_all[n] );
			}

        g_strfreev ( lines_all );

    g_string_free ( gstring, TRUE );
}


static glong tv_set_label_freq_ext ( GtkLabel *label_set, glong num )
{
    gint numpage = gtk_notebook_get_current_page ( notebook );

    GtkWidget *page = gtk_notebook_get_nth_page ( notebook, numpage );
    GtkLabel *label = (GtkLabel *)gtk_notebook_get_tab_label ( notebook, page );
    const gchar *name_tab = gtk_label_get_text ( label );

    if ( g_strrstr ( name_tab, "DVB-S" ) )
    {
        if ( num < 100000 )
        {
            num *= 1000;
            gtk_label_set_text ( label_set, " Frequency  MHz " );
        }
        else
            gtk_label_set_text ( label_set, " Frequency  KHz " );
    }
    else
    {
        if ( num < 1000 )
        {
            num *= 1000000;
            gtk_label_set_text ( label_set, " Frequency  MHz " );
        }
        else if ( num < 1000000 )
        {
            num *= 1000;
            gtk_label_set_text ( label_set, " Frequency  KHz " );
        }
    }

    g_print ( "numpage = %d | %s\n", numpage, name_tab );

    return num;
}

static void scan_changed_spin_all ( GtkSpinButton *button, GtkLabel *label )
{
    gtk_spin_button_update ( button );

    glong num = gtk_spin_button_get_value  ( button );
    const gchar *name = gtk_label_get_text ( label  );

    if ( g_strrstr ( name, "Frequency" ) ) num = tv_set_label_freq_ext ( label, num );

    guint c = 0;
    for ( c = 0; c < G_N_ELEMENTS ( gst_param_dvb_descr_n ); c++ )
        if ( g_strrstr ( name, gst_param_dvb_descr_n[c].name ) )
        {
            g_object_set ( dvbsrc_tune, gst_param_dvb_descr_n[c].gst_param, num, NULL );

            const gchar *name_set = gtk_label_get_text ( label );
            g_print ( "name = %s | num = %ld | gst_param = %s \n", name_set, num, gst_param_dvb_descr_n[c].gst_param );
        }
}
static void scan_changed_combo_all ( GtkComboBox *combo_box, GtkLabel *label )
{
    guint num = gtk_combo_box_get_active ( combo_box );
    const gchar *name = gtk_label_get_text ( label );

    if ( g_strrstr ( name, "LNB" ) )
    {
        lnb_type = num;
        tv_set_lnb ( dvbsrc_tune, num );
        g_print ( "name %s | set %s: %d \n", name, lnb_n[num].name, num );
        return;
    }
    if ( g_strrstr ( name, "DiSEqC" ) )
    {
        g_object_set ( dvbsrc_tune, "diseqc-source", num-1, NULL );
        g_print ( "name = %s | set = %d | gst_param = diseqc-source \n", name, num-1 );
        return;
    }

    guint c = 0;
    for ( c = 0; c < G_N_ELEMENTS ( gst_param_dvb_descr_n ); c++ )
        if ( g_strrstr ( name, gst_param_dvb_descr_n[c].name ) )
        {
            if ( g_strrstr ( name, "Polarity" ) )
            {
                pol = gst_param_dvb_descr_n[c].dvb_descr[num].descr_num ? "H" : "V";
                g_object_set ( dvbsrc_tune, gst_param_dvb_descr_n[c].gst_param, pol, NULL );
            }
            else
                g_object_set ( dvbsrc_tune, gst_param_dvb_descr_n[c].gst_param,
                    gst_param_dvb_descr_n[c].dvb_descr[num].descr_num, NULL );

            g_print ( "name = %s | num = %d | gst_param = %s | descr_text_vis = %s | descr_num = %d \n",
                name, num, gst_param_dvb_descr_n[c].gst_param,
                gst_param_dvb_descr_n[c].dvb_descr[num].text_vis,
                gst_param_dvb_descr_n[c].dvb_descr[num].descr_num );
        }
}


static void tv_scan_get_tp_data ( GString *gstring )
{
    guint c = 0, d = 0;
    gint  d_data = 0, DVBTYPE = 0;

    g_object_get ( dvbsrc_tune, "delsys", &DVBTYPE, NULL );
    g_print ( "delsys: %d | %d \n", DVBTYPE, SYS_DVBS );
    //g_string_append_printf ( gstring, ":delsys=%d", DVBTYPE );

    if ( DVBTYPE == SYS_UNDEFINED )
    {
        if ( DVB_DELSYS == SYS_UNDEFINED )
        {
            if ( g_strrstr ( dvb_type_str, "DVB-T" ) ) DVBTYPE = SYS_DVBT2;
            if ( g_strrstr ( dvb_type_str, "DVB-S" ) ) DVBTYPE = SYS_DVBS2;
            if ( g_strrstr ( dvb_type_str, "DVB-C" ) ) DVBTYPE = SYS_DVBC_ANNEX_A;
        }
        else
            DVBTYPE = DVB_DELSYS;
    }

    const gchar *dvb_f[] = { "adapter", "frontend" };

    for ( d = 0; d < G_N_ELEMENTS ( dvb_f ); d++ )
    {
        g_object_get ( dvbsrc_tune, dvb_f[d], &d_data, NULL );
        g_string_append_printf ( gstring, ":%s=%d", dvb_f[d], d_data );
    }

    if ( DVBTYPE == SYS_DVBT || DVBTYPE == SYS_DVBT2 || DVBTYPE == SYS_DTMB /* || SYS_DVBT2 */ )
    {
        for ( c = 0; c < G_N_ELEMENTS ( dvbt ); c++ )
        {
            if ( DVBTYPE == SYS_DVBT )
                if ( g_strrstr ( dvbt[c], "Stream ID" ) ) continue;

            if ( DVBTYPE == SYS_DTMB )
                if ( g_strrstr ( dvbt[c], "Code Rate LP" ) || g_strrstr ( dvbt[c], "Hierarchy" ) || g_strrstr ( dvbt[c], "Stream ID" ) ) continue;


            for ( d = 0; d < G_N_ELEMENTS ( gst_param_dvb_descr_n ); d++ )
            {
                if ( g_strrstr ( dvbt[c], gst_param_dvb_descr_n[d].name ) )
                {
                    g_object_get ( dvbsrc_tune, gst_param_dvb_descr_n[d].gst_param, &d_data, NULL );
                    g_string_append_printf ( gstring, ":%s=%d", gst_param_dvb_descr_n[d].gst_param, d_data );

                    break;
                }
            }
        }
    }

    if ( DVBTYPE == SYS_DVBS || DVBTYPE == SYS_TURBO || DVBTYPE == SYS_DVBS2 /* || SYS_ISDBS */ )
    {
        for ( c = 0; c < G_N_ELEMENTS ( dvbs ); c++ )
        {
            if ( DVBTYPE == SYS_TURBO )
                if ( g_strrstr ( dvbs[c], "Pilot" ) || g_strrstr ( dvbs[c], "Rolloff" ) || g_strrstr ( dvbs[c], "Stream ID" ) ) continue;

            if ( DVBTYPE == SYS_DVBS )
                if ( g_strrstr ( dvbs[c], "Modulation" ) || g_strrstr ( dvbs[c], "Pilot" ) || g_strrstr ( dvbs[c], "Rolloff" ) || g_strrstr ( dvbs[c], "Stream ID" ) ) continue;

            for ( d = 0; d < G_N_ELEMENTS ( gst_param_dvb_descr_n ); d++ )
            {
                if ( g_strrstr ( dvbs[c], gst_param_dvb_descr_n[d].name ) )
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

                    g_object_get ( dvbsrc_tune, gst_param_dvb_descr_n[d].gst_param, &d_data, NULL );
                    g_string_append_printf ( gstring, ":%s=%d", gst_param_dvb_descr_n[d].gst_param, d_data );

                    break;
                }
            }
        }
    }

    if ( DVBTYPE == SYS_DVBC_ANNEX_A || DVBTYPE == SYS_DVBC_ANNEX_C || DVBTYPE == SYS_DVBC_ANNEX_B || DVBTYPE == SYS_ATSC )
    {
        for ( c = 0; c < G_N_ELEMENTS ( dvbc ); c++ )
        {
            if ( DVBTYPE == SYS_DVBC_ANNEX_B || DVBTYPE == SYS_ATSC )
                if ( g_strrstr ( dvbc[c], "Inversion" ) || g_strrstr ( dvbc[c], "Symbol rate" ) || g_strrstr ( dvbc[c], "Code Rate HP" ) ) continue;

            for ( d = 0; d < G_N_ELEMENTS ( gst_param_dvb_descr_n ); d++ )
            {
                if ( g_strrstr ( dvbc[c], gst_param_dvb_descr_n[d].name ) )
                {
                    g_object_get ( dvbsrc_tune, gst_param_dvb_descr_n[d].gst_param, &d_data, NULL );
                    g_string_append_printf ( gstring, ":%s=%d", gst_param_dvb_descr_n[d].gst_param, d_data );

                    break;
                }
            }
        }
    }
}

static void tv_scan_set_info_ch ()
{
    gint c = 0, c_tv = 0, c_ro = 0;

    for ( c = 0; c < sdt_count; c++ )
    {
        if ( dvb_gst_scan_pmt_n[c].vpid > 0 ) c_tv++;
        if ( dvb_gst_scan_pmt_n[c].apid > 0 && dvb_gst_scan_pmt_n[c].vpid == 0 ) c_ro++;
    }

    tv_scan_set_all_ch ( sdt_count, c_tv, c_ro );
    g_print ( "\nAll Channels: %d    TV: %d    Radio: %d    Other: %d\n\n", sdt_count, c_tv, c_ro, sdt_count-c_tv-c_ro );
}

static void tv_scan_read_ch_to_treeview ()
{
    GString *gstr_data = g_string_new ( NULL );
    tv_scan_get_tp_data ( gstr_data );
    g_print ( "%s \n", gstr_data->str );

    gint i = 0, c = 0;
    for ( i = 0; i < sdt_count; i++ )
    {
        for ( c = 0; c < pmt_count; c++ )
        {
            if ( dvb_gst_scan_sdt_n[i].pmn_pid == dvb_gst_scan_pmt_n[c].pmn_pid )
                break;
        }

        GString *gstring = g_string_new ( tv_strip_ch_name ( dvb_gst_scan_sdt_n[i].name ) );
        g_string_append_printf ( gstring, ":program-number=%d:video-pid=%d:audio-pid=%d",
            dvb_gst_scan_pmt_n[c].pmn_pid, dvb_gst_scan_pmt_n[c].vpid, dvb_gst_scan_pmt_n[c].apid );

        g_string_append_printf ( gstring, "%s", gstr_data->str );

        if ( /*win_scan &&*/ dvb_gst_scan_pmt_n[c].apid != 0 ) // ignore other
            tv_scan_add_to_treeview ( dvb_gst_scan_sdt_n[i].name, gstring->str );

        g_print ( "%s \n", dvb_gst_scan_sdt_n[i].name );
        //g_print ( "%s \n", gstring->str );

        g_free ( dvb_gst_scan_sdt_n[i].name );
        g_string_free ( gstring, TRUE );
    }

    g_string_free ( gstr_data, TRUE );
    tv_scan_set_info_ch ();
}


// "DVB-T, DVB-S, DVB-C"

static void tv_scan_start ( GtkButton *button, gpointer data )
{
    if ( GST_ELEMENT_CAST ( dvb_scan ) -> current_state == GST_STATE_PLAYING )
        return;

    start_clear_dvb_gst_scan ();

    time ( &t_start );
    gst_element_set_state ( dvb_scan, GST_STATE_PLAYING );

    dvb_type_str = (gchar *)data;

    g_print ( "tv_scan_start: %s \n", dvb_type_str );
}
static void tv_scan_stop ( GtkButton *button, gpointer data )
{
    if ( GST_ELEMENT_CAST ( dvb_scan ) -> current_state == GST_STATE_NULL )
        return;

    gst_element_set_state ( dvb_scan, GST_STATE_NULL );

    gtk_label_set_text ( scan_snr_dvbt, "Signal level 0  &  Signal quality 0" );
    gtk_label_set_text ( scan_snr_dvbs, "Signal level 0  &  Signal quality 0" );
    gtk_label_set_text ( scan_snr_dvbc, "Signal level 0  &  Signal quality 0" );

    tv_scan_read_ch_to_treeview ();

    g_print ( "tv_scan_stop: %s \n", (gchar *)data );
}

static void tv_scan_set_all_ch ( gint all_ch, gint c_tv, gint c_ro )
{
    gchar *text = g_strdup_printf ( " All Channels: %d \n TV: %d -- Radio: %d -- Other: %d\n", all_ch, c_tv, c_ro, all_ch - c_tv - c_ro );
    gtk_label_set_text ( GTK_LABEL ( all_channels ), text );
    g_free ( text );
}

static GtkBox * tv_scan_battons_box ( const gchar *type )
{
    GtkBox *g_box = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_VERTICAL, 0 );

    GtkBox *hb_box = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_HORIZONTAL, 0 );

    GtkLabel *label = (GtkLabel *)gtk_label_new ( "Signal level 0  &  Signal quality 0" );
    if ( g_strrstr ( type, "DVB-T" ) ) scan_snr_dvbt = label;
    if ( g_strrstr ( type, "DVB-S" ) ) scan_snr_dvbs = label;
    if ( g_strrstr ( type, "DVB-C" ) ) scan_snr_dvbc = label;
    gtk_box_pack_start ( g_box, GTK_WIDGET  ( label ), FALSE, FALSE, 10 );

    GtkButton *button_scan = (GtkButton *)gtk_button_new_with_label ( " Scan " );
    g_signal_connect ( button_scan, "clicked", G_CALLBACK ( tv_scan_start ), (gpointer)type );
    gtk_box_pack_start ( hb_box, GTK_WIDGET ( button_scan ), FALSE, FALSE, 0 );

    GtkButton *button_stop = (GtkButton *) gtk_button_new_with_label ( " Stop " );
    g_signal_connect ( button_stop, "clicked", G_CALLBACK ( tv_scan_stop ),  (gpointer)type );
    gtk_box_pack_start ( hb_box, GTK_WIDGET ( button_stop ), FALSE, FALSE, 0 );

    gtk_box_pack_start ( g_box, GTK_WIDGET  ( hb_box ), FALSE, FALSE, 0 );

    return g_box;
}

static GtkBox * tv_scan_dvb_all  ( guint num, const gchar *dvball[], const gint dvbeall[], const gchar *type )
{
    GtkBox *g_box = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_VERTICAL, 0 );
    gtk_widget_set_margin_start ( GTK_WIDGET ( g_box ), 10 );
    gtk_widget_set_margin_end   ( GTK_WIDGET ( g_box ), 10 );

    GtkGrid *grid = (GtkGrid *)gtk_grid_new();
    gtk_grid_set_column_homogeneous ( GTK_GRID ( grid ), TRUE );
    gtk_box_pack_start ( g_box, GTK_WIDGET ( grid ), TRUE, TRUE, 10 );

    GtkLabel *label;
    GtkSpinButton *spinbutton;
    GtkComboBoxText *scan_combo_box;

    guint d = 0, c = 0, z = 0;
    for ( d = 0; d < num; d++ )
    {
        label = (GtkLabel *)gtk_label_new ( dvball[d] );
        gtk_widget_set_halign ( GTK_WIDGET ( label ), GTK_ALIGN_START );
        gtk_grid_attach ( GTK_GRID ( grid ), GTK_WIDGET ( label ), 0, d, 1, 1 );

        if ( dvbeall[d] == 1 )
        {
            spinbutton = (GtkSpinButton *) gtk_spin_button_new_with_range ( 0, 100000000, 1 );
            gtk_grid_attach ( GTK_GRID ( grid ), GTK_WIDGET ( spinbutton ), 1, d, 1, 1 );
            if ( g_strrstr ( dvball[d], "Stream ID" ) )
            {
                gtk_spin_button_set_range ( spinbutton, -1, 255 );
                gtk_spin_button_set_value ( spinbutton, -1 );
            }
            if ( g_strrstr ( dvball[d], "Bandwidth" ) )
            {
                gtk_spin_button_set_range ( spinbutton, 0, 25000000 );
                gtk_spin_button_set_value ( spinbutton, 8000000 );
            }
            g_signal_connect ( spinbutton, "changed", G_CALLBACK ( scan_changed_spin_all ), label );
        }
        else
        {
            scan_combo_box = (GtkComboBoxText *) gtk_combo_box_text_new ();
            gtk_grid_attach ( GTK_GRID ( grid ), GTK_WIDGET ( scan_combo_box ), 1, d, 1, 1 );

            for ( c = 0; c < G_N_ELEMENTS ( gst_param_dvb_descr_n ); c++ )
            {
              if ( g_strrstr ( dvball[d], gst_param_dvb_descr_n[c].name ) )
              {
                for ( z = 0; z < gst_param_dvb_descr_n[c].cdsc; z++ )
                    gtk_combo_box_text_append_text ( scan_combo_box, gst_param_dvb_descr_n[c].dvb_descr[z].text_vis );

                if ( g_strrstr ( gst_param_dvb_descr_n[c].gst_param, "polarity" ) || g_strrstr ( gst_param_dvb_descr_n[c].gst_param, "lnb-type" ) )
                    continue;

                gint d_data = 0;
                g_object_get ( dvbsrc_tune, gst_param_dvb_descr_n[c].gst_param, &d_data, NULL );
                gtk_combo_box_set_active ( GTK_COMBO_BOX ( scan_combo_box ), d_data );
              }
            }

            if ( g_strrstr ( dvball[d], "DiSEqC" ) )
            {
                gtk_combo_box_text_append_text ( scan_combo_box, "None" );
                for ( z = 0; z < 8; z++ )
                {
                    gchar *text = g_strdup_printf  ( "%d  Lnb", z+1 );
                    gtk_combo_box_text_append_text ( scan_combo_box, text );
                    g_free ( text );
                }
            }

            if ( gtk_combo_box_get_active ( GTK_COMBO_BOX ( scan_combo_box ) ) == -1 )
                 gtk_combo_box_set_active ( GTK_COMBO_BOX ( scan_combo_box ), 0 );

            g_signal_connect ( scan_combo_box, "changed", G_CALLBACK ( scan_changed_combo_all ), label );
        }
    }

    gtk_box_pack_start ( g_box, GTK_WIDGET ( tv_scan_battons_box ( type ) ), FALSE, FALSE, 10 );

    return g_box;
}

static GtkBox * tv_scan_pref ();
static GtkBox * tv_scan_channels  ();

static GtkBox * all_box_scan ( guint i )
{
    GtkBox *only_box = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_VERTICAL, 0 );

    if ( i == 0 )  { return tv_scan_pref     (); }
    if ( i == 4 )  { return tv_scan_channels (); }
    if ( i == 1 )  { return tv_scan_dvb_all ( G_N_ELEMENTS ( dvbt ), dvbt, dvbet, "DVB-T" ); }
    if ( i == 2 )  { return tv_scan_dvb_all ( G_N_ELEMENTS ( dvbs ), dvbs, dvbes, "DVB-S" ); }
    if ( i == 3 )  { return tv_scan_dvb_all ( G_N_ELEMENTS ( dvbc ), dvbc, dvbec, "DVB-C" ); }

    return only_box;
}

static void tv_scan_add_to_treeview ( gchar *name_ch, gchar *data )
{
    GtkTreeModel *model = gtk_tree_view_get_model ( GTK_TREE_VIEW ( scan_treeview ) );
    GtkTreeIter iter;

    guint ind = gtk_tree_model_iter_n_children ( gtk_tree_view_get_model ( GTK_TREE_VIEW ( scan_treeview ) ), NULL );

    gtk_list_store_append ( GTK_LIST_STORE ( model ), &iter );
    gtk_list_store_set ( GTK_LIST_STORE ( model ), &iter,
                         COL_NUM,      ind+1,
                         COL_FILES_CH, name_ch,
                         COL_URI_DATA, data,
                         -1 );
}
static void tv_scan_ch_save ( GtkButton *button, GtkTreeView *treeview )
{
    GtkTreeIter iter;
    GtkTreeModel *model = gtk_tree_view_get_model ( GTK_TREE_VIEW ( treeview ) );

    gboolean valid;
    for ( valid = gtk_tree_model_get_iter_first ( model, &iter ); valid;
          valid = gtk_tree_model_iter_next ( model, &iter ) )
    {
        gchar *name, *data;
        gtk_tree_model_get ( model, &iter, 1, &name, -1 );
        gtk_tree_model_get ( model, &iter, 2, &data, -1 );

        tv_add_channels ( name, data );

        g_free ( name );
        g_free ( data );
    }
}
static void tv_scan_ch_clear ( GtkButton *button, GtkTreeView *treeview )
{
    GtkTreeModel *model = gtk_tree_view_get_model ( GTK_TREE_VIEW ( treeview ) );
    gtk_list_store_clear ( GTK_LIST_STORE ( model ) );

    tv_scan_set_all_ch ( 0, 0, 0 );
}

static GtkBox * tv_scan_channels_battons_box ( GtkTreeView *treeview )
{
    GtkBox *g_box = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_VERTICAL, 0 );

    GtkBox *hb_box = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_HORIZONTAL, 0 );

    //GtkButton *button_save = (GtkButton *)gtk_button_new_with_label ( " Save " );
    GtkButton *button_save = (GtkButton *)gtk_button_new_from_icon_name ( "document-save", GTK_ICON_SIZE_SMALL_TOOLBAR );
    g_signal_connect ( button_save, "clicked", G_CALLBACK ( tv_scan_ch_save ), treeview );
    gtk_box_pack_end ( hb_box, GTK_WIDGET ( button_save ), FALSE, FALSE, 0 );

    //GtkButton *button_clear = (GtkButton *) gtk_button_new_with_label ( " Clear " );
    GtkButton *button_clear = (GtkButton *)gtk_button_new_from_icon_name ( "edit-clear", GTK_ICON_SIZE_SMALL_TOOLBAR );
    g_signal_connect ( button_clear, "clicked", G_CALLBACK ( tv_scan_ch_clear ), treeview );
    gtk_box_pack_end ( hb_box, GTK_WIDGET ( button_clear ), FALSE, FALSE, 0 );

    gtk_box_pack_start ( g_box, GTK_WIDGET  ( hb_box ), FALSE, FALSE, 0 );

    return g_box;
}

static GtkBox * tv_scan_channels  ()
{
    GtkBox *g_box  = (GtkBox *)gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_margin_start ( GTK_WIDGET ( g_box ), 10 );
    gtk_widget_set_margin_end   ( GTK_WIDGET ( g_box ), 10 );

    all_channels = (GtkLabel *)gtk_label_new ( " All Channels \n" );
    gtk_widget_set_halign ( GTK_WIDGET ( all_channels ),    GTK_ALIGN_START );

    scan_treeview = (GtkTreeView *)gtk_tree_view_new_with_model ( GTK_TREE_MODEL ( gtk_list_store_new ( 3, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING ) ) );

    gtk_box_pack_start ( g_box, GTK_WIDGET ( tv_scroll_win ( scan_treeview, "Channels", "Data" ) ), TRUE, TRUE, 10 );
    gtk_box_pack_start ( g_box, GTK_WIDGET ( all_channels ),  FALSE, FALSE, 0 );

    gtk_box_pack_start ( g_box, GTK_WIDGET ( tv_scan_channels_battons_box ( scan_treeview ) ), FALSE, FALSE, 10 );

    return g_box;
}

static void tv_get_dvb_name ( GtkLabel *label )
{
    gchar *dvb_name = NULL;

    gint fd;
    gchar *fd_name = g_strdup_printf ( "/dev/dvb/adapter%d/frontend%d", adapter_ct, frontend_ct );

    if ( ( fd = g_open ( fd_name, O_RDONLY ) ) == -1 )
    {
        const gchar *errno_info = g_strerror ( errno );
            g_print ( "ERROR: %s %s\n", fd_name, errno_info );
        dvb_name = g_strdup ( errno_info );
    }
    else
    {
        struct dvb_frontend_info info;

        if ( ( ioctl ( fd, FE_GET_INFO, &info ) ) == -1 )
            perror ( "find_dvb: ioctl FE_GET_INFO failed \n" );
        else
            dvb_name = g_strdup ( info.name );


        struct dtv_property p[] = { { .cmd = DTV_DELIVERY_SYSTEM } };
        struct dtv_properties cmdseq = { .num = 1, .props = p };

        if ( (ioctl(fd, FE_GET_PROPERTY, &cmdseq)) == -1 )
            perror("FE_GET_PROPERTY failed");
        else
            DVB_DELSYS = p[0].u.data;

        g_print ( "DVB Name: %s \nDelivery System: %d \n\n", dvb_name, DVB_DELSYS );

        g_close ( fd, NULL );
    }

    g_free  ( fd_name );

    gtk_label_set_text ( label, dvb_name );

    if ( dvb_name ) g_free  ( dvb_name );
}

static void tv_convert_changed_adapter ( GtkSpinButton *button, GtkLabel *label )
{
    gtk_spin_button_update ( button );
    adapter_ct = gtk_spin_button_get_value ( button );

    tv_get_dvb_name ( label );
}
static void tv_convert_changed_frontend ( GtkSpinButton *button, GtkLabel *label )
{
    gtk_spin_button_update ( button );
    frontend_ct = gtk_spin_button_get_value ( button );

    tv_get_dvb_name ( label );
}

static void tv_convert_openf ( GtkEntry *entry )
{
    gchar *filename = tv_openf ();
        if ( filename ) gtk_entry_set_text ( entry, filename );
    g_free ( filename );
}

static void tv_scan_convert ( GtkButton *button, GtkEntry *entry )
{
    const gchar *filename = gtk_entry_get_text ( entry );

    if ( g_file_test ( filename, G_FILE_TEST_EXISTS ) )
        tv_convert_dvb5 ( filename );
}

static void tv_set_rec_dir ( GtkEntry *entry )
{
    g_free ( rec_dir );
        rec_dir = tv_rec_dir ();
    if ( rec_dir ) gtk_entry_set_text ( entry, rec_dir );
}

static void tv_set_rec_data_venc ( GtkEntry *entry )
{
    g_free ( video_encoder ); video_encoder = g_strdup ( gtk_entry_get_text ( entry ) );
}
static void tv_set_rec_data_aenc ( GtkEntry *entry )
{
    g_free ( audio_encoder ); audio_encoder = g_strdup ( gtk_entry_get_text ( entry ) );
}
static void tv_set_rec_data_mux ( GtkEntry *entry )
{
    g_free ( muxer ); muxer = g_strdup ( gtk_entry_get_text ( entry ) );
}
static void tv_set_rec_data_ext ( GtkEntry *entry )
{
    g_free ( file_ext ); file_ext = g_strdup ( gtk_entry_get_text ( entry ) );
}

static GtkBox * tv_scan_pref ()
{
    GtkBox *g_box  = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_VERTICAL, 0 );
    gtk_widget_set_margin_start ( GTK_WIDGET ( g_box ), 10 );
    gtk_widget_set_margin_end   ( GTK_WIDGET ( g_box ), 10 );

    const struct data_scan_pref { const gchar *label; void (* activate); guint af; const gchar *text; } data_scan_pref_n[] =
    {
        { " ", NULL, 0, NULL }, { " DVB Device ", NULL, 0, NULL }, { " ", NULL, 0, NULL },
        { " Adapter ",  tv_convert_changed_adapter,  adapter_ct,      NULL    },
        { " Frontend ", tv_convert_changed_frontend, frontend_ct,     NULL    },
        { " Channel file ( dvb 5 ) ", tv_convert_openf, 0, "dvb_channel.conf" },
        { " ", NULL, 0, NULL },
        { " Recording folder ", tv_set_rec_dir,       0, rec_dir       },
        { " Audio encoder ",    tv_set_rec_data_aenc, 0, audio_encoder },
        { " Video encoder ",    tv_set_rec_data_venc, 0, video_encoder },
        { " Muxer ",            tv_set_rec_data_mux,  0, muxer         },
        { " File name ext ",    tv_set_rec_data_ext,  0, file_ext      }
    };

    GtkGrid *grid = (GtkGrid *)gtk_grid_new();
    gtk_grid_set_column_homogeneous ( GTK_GRID ( grid ), TRUE );
    gtk_box_pack_start ( g_box, GTK_WIDGET ( grid ), TRUE, TRUE, 10 );

    GtkLabel *label_name;
    GtkEntry *entry, *entry_cv;

    guint d = 0;
    for ( d = 0; d < G_N_ELEMENTS ( data_scan_pref_n ); d++ )
    {
        GtkLabel *label = (GtkLabel *)gtk_label_new ( data_scan_pref_n[d].label );
        gtk_widget_set_halign ( GTK_WIDGET ( label ), ( d == 1 ) ? GTK_ALIGN_CENTER : GTK_ALIGN_START );
        gtk_grid_attach ( GTK_GRID ( grid ), GTK_WIDGET ( label ), 0, d, ( d == 1 ) ? 2 : 1, 1 );

        if ( d == 1 ) label_name = label;

        if ( d == 3 || d == 4 )
        {
            GtkSpinButton *spin = (GtkSpinButton *) gtk_spin_button_new_with_range ( 0, 16, 1 );
            gtk_spin_button_set_value ( spin, data_scan_pref_n[d].af  );
            g_signal_connect ( spin, "changed", G_CALLBACK ( data_scan_pref_n[d].activate ), label_name );
            gtk_grid_attach ( GTK_GRID ( grid ), GTK_WIDGET ( spin ), 1, d, 1, 1 );

            continue;
        }

        if ( data_scan_pref_n[d].activate == NULL ) continue;

        entry = (GtkEntry *)gtk_entry_new ();
        gtk_grid_attach ( GTK_GRID ( grid ), GTK_WIDGET ( entry ), 1, d, 1, 1 );
        gtk_entry_set_text ( entry, data_scan_pref_n[d].text );

        if ( d == 5 || d == 7 )
        {
            gtk_entry_set_icon_from_icon_name ( entry, GTK_ENTRY_ICON_SECONDARY, "document-open" );
            g_signal_connect ( entry, "icon-press", G_CALLBACK ( data_scan_pref_n[d].activate ), NULL );
        }
        else
            g_signal_connect ( entry, "changed", G_CALLBACK ( data_scan_pref_n[d].activate ), NULL );

        if ( d == 5 ) entry_cv = entry;
    }

    tv_get_dvb_name ( label_name );

    GtkButton *button_convert = (GtkButton *)gtk_button_new_with_label ( " Convert " );
    g_signal_connect ( button_convert, "clicked", G_CALLBACK ( tv_scan_convert ), entry_cv );
    gtk_box_pack_start ( g_box, GTK_WIDGET ( button_convert ), FALSE, FALSE, 10 );

    return g_box;
}

static void tv_scan_quit ( GtkWindow *window )
{
    gtk_widget_destroy ( GTK_WIDGET ( window ) );
}
static void tv_scan_close ( GtkButton *button, GtkWindow *window )
{
    tv_scan_quit ( window );
}

void tv_win_scan ()
{
    scan_gst_create ();

    GtkWindow *window =      (GtkWindow *)gtk_window_new ( GTK_WINDOW_TOPLEVEL );
    gtk_window_set_modal     ( window, TRUE );
    gtk_window_set_position  ( window, GTK_WIN_POS_CENTER );
    gtk_window_set_title     ( window, "Scan" );
	g_signal_connect         ( window, "destroy", G_CALLBACK ( tv_scan_quit ), NULL );

	gtk_window_set_type_hint ( GTK_WINDOW (window), GDK_WINDOW_TYPE_HINT_UTILITY );

    GtkBox *m_box = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_VERTICAL,   0 );
    GtkBox *h_box = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_HORIZONTAL, 0 );

    notebook = (GtkNotebook *)gtk_notebook_new ();
    gtk_notebook_set_scrollable ( notebook, TRUE );

    GtkBox *m_box_n[G_N_ELEMENTS ( labels_scan_n )];

    for ( j = 0; j < G_N_ELEMENTS ( labels_scan_n ); j++ )
    {
        m_box_n[j] = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_VERTICAL, 0 );
        gtk_box_pack_start ( m_box_n[j], GTK_WIDGET ( all_box_scan ( j ) ), TRUE, TRUE, 0 );
        gtk_notebook_append_page ( notebook, GTK_WIDGET ( m_box_n[j] ),  gtk_label_new ( labels_scan_n[j].name ) );
    }

    gtk_notebook_set_tab_pos ( notebook, GTK_POS_TOP );
    gtk_box_pack_start ( m_box, GTK_WIDGET (notebook), TRUE, TRUE, 0 );

    GtkButton *button_close = (GtkButton *)gtk_button_new_from_icon_name ( "window-close", GTK_ICON_SIZE_SMALL_TOOLBAR );
    g_signal_connect ( button_close, "clicked", G_CALLBACK ( tv_scan_close ), window );
    gtk_box_pack_end ( h_box, GTK_WIDGET ( button_close ), FALSE, FALSE, 5 );

    gtk_box_pack_start ( m_box, GTK_WIDGET ( h_box ), FALSE, FALSE, 5 );
    gtk_container_set_border_width ( GTK_CONTAINER ( m_box ), 5 );
    gtk_container_add ( GTK_CONTAINER ( window ), GTK_WIDGET ( m_box ) );

    gtk_widget_show_all ( GTK_WIDGET ( window ) );
}

