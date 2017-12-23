/*
 * Copyright 2014 - 2018 Stepan Perun
 * This program is free software.
 * License: Gnu Lesser General Public License
 * http://www.gnu.org/licenses/lgpl.html
*/


#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gst/video/videooverlay.h>
#include <stdlib.h>
#include <stdio.h>

#include "gtv-gst.h"
#include "gtv-dvb.h"
#include "gtv-pref.h"
#include "gtv-scan.h"
#include "gtv-mpegts.h"
#include "gtv-service.h"

//#include "gtv-eqa.h"
//#include "gtv-eqv.h"


struct GtvGstBase
{
	GstElement *dvb_all_n[19];
	GstElement *dvb_rec_all_n[6];
};

struct GtvGstBase gtvgstbase;


static guint a = 0, b = 0, c = 0, j = 0;


static void gtv_gst_set_prop ( GstElement *element, gchar *av_enc_p );


GstElement * gtv_gstelement_src () { return gtvgstbase.dvb_all_n[0];  }
GstElement * gtv_gstelement_mts () { return gtvgstbase.dvb_all_n[1];  }
GstElement * gtv_gstelement_mut () { return gtvgstbase.dvb_all_n[16]; }
GstElement * gtv_gstelement_eqa () { return gtvgstbase.dvb_all_n[17]; }
GstElement * gtv_gstelement_eqv () { return gtvgstbase.dvb_all_n[8];  }

GstElement * gtv_gstelement_rec () { return gtvgstbase.dvb_rec_all_n[5]; }



void gtv_video_window_realize ( GtkDrawingArea *draw )
{
    gulong xid = GDK_WINDOW_XID ( gtk_widget_get_window ( GTK_WIDGET ( draw ) ) );
    gtvgstdvb.video_window_handle = xid;
}

static GstBusSyncReply gtv_bus_sync_handler ( GstBus *bus, GstMessage *message )
{
    if ( !gst_is_video_overlay_prepare_window_handle_message ( message ) )
        return GST_BUS_PASS;

    if ( gtvgstdvb.video_window_handle != 0 )
    {
        GstVideoOverlay *xoverlay = GST_VIDEO_OVERLAY ( GST_MESSAGE_SRC ( message ) );
        gst_video_overlay_set_window_handle ( xoverlay, gtvgstdvb.video_window_handle );

    } else { g_warning ( "Should have obtained video_window_handle by now!" ); }

    gst_message_unref ( message );
    return GST_BUS_DROP;
}

static void gtv_msg_all ( GstBus *bus, GstMessage *msg )
{
    const GstStructure *structure = gst_message_get_structure ( msg );

    if ( structure )
    {
        gint signal, snr;
        gboolean hlook = FALSE;

        if (  gst_structure_get_int ( structure, "signal", &signal )  )
        {
            gst_structure_get_int ( structure, "snr", &snr );
            gst_structure_get_boolean ( structure, "lock", &hlook );

            gtv_set_sgn_snr ( gtvgstdvb.dvbplay, gtvdvb.signal_snr, gtvdvb.bar_sgn, gtvdvb.bar_snr, (signal * 100) / 0xffff, (snr * 100) / 0xffff, hlook );
        }
    }

    gtv_mpegts_parse_pmt_lang ( msg );
}

void gtv_msg_war ( GstBus *bus, GstMessage *msg )
{
    GError *war = NULL;
    gchar *dbg = NULL;

    gst_message_parse_warning ( msg, &war, &dbg );

	//g_warning ( "gtv_msg_war:: %s (%s)\n", war->message, (dbg) ? dbg : "no details" );
	g_debug ( "gtv_msg_war:: %s (%s)\n", war->message, (dbg) ? dbg : "no details" );

    g_error_free ( war );
    g_free ( dbg );
}

static void gtv_msg_err ( GstBus *bus, GstMessage *msg )
{
    GError *err = NULL;
    gchar *dbg  = NULL;
		
    gst_message_parse_error ( msg, &err, &dbg );
    
    g_critical ( "gtv_msg_err:: %s (%s)\n", err->message, (dbg) ? dbg : "no details" );

    if ( gtvdvb.first_msg )
        gtv_message_dialog ( err->message, (dbg) ? dbg : " ", GTK_MESSAGE_ERROR );

    gtvdvb.first_msg = FALSE;

    g_error_free ( err );
    g_free ( dbg );
    
	gtv_stop ();
}

gboolean gtv_gst_create ()
{
    gtv_mpegts_initialize ();

    gtvgstdvb.dvbplay  = gst_pipeline_new ( "pipeline0" );

    if ( !gtvgstdvb.dvbplay )
    {
        g_critical ( "gtv_gst_create:: pipeline - not created. \n" );        
        return FALSE;
    }

    GstBus *bus = gst_element_get_bus ( gtvgstdvb.dvbplay );
    gst_bus_add_signal_watch_full ( bus, G_PRIORITY_DEFAULT );
    gst_bus_set_sync_handler ( bus, (GstBusSyncHandler)gtv_bus_sync_handler, NULL, NULL );
    gst_object_unref (bus);

    g_signal_connect ( bus, "message",          G_CALLBACK ( gtv_msg_all ), NULL );
    g_signal_connect ( bus, "message::error",   G_CALLBACK ( gtv_msg_err ), NULL );
    g_signal_connect ( bus, "message::warning", G_CALLBACK ( gtv_msg_war ), NULL );
    
    gtv_scan_gst_create ();

    return TRUE;
}


static void gtv_gst_pad_link ( GstPad *pad, GstElement *element, const gchar *name, GstElement *element_n )
{
    GstPad *pad_va_sink = gst_element_get_static_pad ( element, "sink" );

    if ( gst_pad_link ( pad, pad_va_sink ) == GST_PAD_LINK_OK )
    	gst_object_unref ( pad_va_sink );
    else
        g_debug ( "gtv_gst_pad_link:: linking demux/decode name: %s & video/audio pad failed \n", name );
}

static void gtv_pad_demux_added_audio ( GstElement *element, GstPad *pad, GstElement *element_audio )
{
    const gchar *name = gst_structure_get_name ( gst_caps_get_structure ( gst_pad_query_caps ( pad, NULL ), 0 ) );

    if ( g_str_has_prefix ( name, "audio" ) )
    {
		if ( ( gtvdvb.audio_ind == 0 && gtvdvb.count_audio == 0 ) || ( gtvdvb.audio_ind > 0 && gtvdvb.count_audio == gtvdvb.audio_ind ) )
			gtv_gst_pad_link ( pad, element_audio, name, element );
		
        gtvdvb.count_audio++;
    }
}
static void gtv_pad_demux_added_video ( GstElement *element, GstPad *pad, GstElement *element_video )
{
    const gchar *name = gst_structure_get_name ( gst_caps_get_structure ( gst_pad_query_caps ( pad, NULL ), 0 ) );

    if ( g_str_has_prefix ( name, "video" ) )
        gtv_gst_pad_link ( pad, element_video, name, element );
}

static void gtv_pad_decode_added ( GstElement *element, GstPad *pad, GstElement *element_va )
{
    const gchar *name = gst_structure_get_name ( gst_caps_get_structure ( gst_pad_query_caps ( pad, NULL ), 0 ) );

    gtv_gst_pad_link ( pad, element_va, name, element );
}

void gtv_gst_tsdemux ()
{
    gtvdvb.count_audio = 0;
    gtvdvb.audio_ind   = 0;

struct dvb_all_list { const gchar *name; } dvb_all_list_n[] =
{
    { "dvbsrc" }, { "tsdemux" },
    { "tee"    }, { "queue2"  }, { "decodebin" }, { "videoconvert" }, { "tee" }, { "queue2"  },/*{ "queue2" },*/ { "videobalance"     }, { "autovideosink" },
    { "tee"    }, { "queue2"  }, { "decodebin" }, { "audioconvert" }, { "tee" }, { "queue2"  },  { "volume" },   { "equalizer-nbands" }, { "autoaudiosink" }
};

    for ( c = 0; c < G_N_ELEMENTS ( gtvgstbase.dvb_all_n ); c++ )
    {
        gtvgstbase.dvb_all_n[c] = gst_element_factory_make ( dvb_all_list_n[c].name, NULL );

    	if ( !gtvgstbase.dvb_all_n[c] )
    	{
            g_critical ( "gtv_gst_tsdemux:: element - %s - not be created.\n", dvb_all_list_n[c].name );
            return;
        }
    }

    if ( gtvdvb.video_enable ) { a = 2; } else { a = 10; }

    gst_bin_add ( GST_BIN ( gtvgstdvb.dvbplay ), gtvgstbase.dvb_all_n[0] );
    gst_bin_add ( GST_BIN ( gtvgstdvb.dvbplay ), gtvgstbase.dvb_all_n[1] );

    for ( c = a; c < G_N_ELEMENTS ( gtvgstbase.dvb_all_n ); c++ )
        gst_bin_add ( GST_BIN ( gtvgstdvb.dvbplay ), gtvgstbase.dvb_all_n[c] );

    gst_element_link_many ( gtvgstbase.dvb_all_n[0], gtvgstbase.dvb_all_n[1], NULL );

    g_signal_connect ( gtvgstbase.dvb_all_n[1], "pad-added", G_CALLBACK ( gtv_pad_demux_added_audio ), gtvgstbase.dvb_all_n[10] );
    g_signal_connect ( gtvgstbase.dvb_all_n[1], "pad-added", G_CALLBACK ( gtv_pad_demux_added_video ), gtvgstbase.dvb_all_n[2] );

    if ( gtvdvb.video_enable )
    {
        gst_element_link_many ( gtvgstbase.dvb_all_n[2], gtvgstbase.dvb_all_n[3], gtvgstbase.dvb_all_n[4], NULL );
        gst_element_link_many ( gtvgstbase.dvb_all_n[5], gtvgstbase.dvb_all_n[6], gtvgstbase.dvb_all_n[7], gtvgstbase.dvb_all_n[8], gtvgstbase.dvb_all_n[9], NULL );

        g_signal_connect ( gtvgstbase.dvb_all_n[4], "pad-added", G_CALLBACK ( gtv_pad_decode_added ), gtvgstbase.dvb_all_n[5] );
    }

    gst_element_link_many ( gtvgstbase.dvb_all_n[10], gtvgstbase.dvb_all_n[11], gtvgstbase.dvb_all_n[12], NULL );
    gst_element_link_many ( gtvgstbase.dvb_all_n[13], gtvgstbase.dvb_all_n[14], gtvgstbase.dvb_all_n[15], gtvgstbase.dvb_all_n[16], gtvgstbase.dvb_all_n[17], gtvgstbase.dvb_all_n[18], NULL );

    g_signal_connect ( gtvgstbase.dvb_all_n[12], "pad-added", G_CALLBACK ( gtv_pad_decode_added ), gtvgstbase.dvb_all_n[13] );
}

void gtv_gst_tsdemux_remove ()
{
    gst_bin_remove ( GST_BIN ( gtvgstdvb.dvbplay ), gtvgstbase.dvb_all_n[0] );
    gst_bin_remove ( GST_BIN ( gtvgstdvb.dvbplay ), gtvgstbase.dvb_all_n[1] );

    for ( c = a; c < G_N_ELEMENTS ( gtvgstbase.dvb_all_n ); c++ )
        gst_bin_remove ( GST_BIN ( gtvgstdvb.dvbplay ), gtvgstbase.dvb_all_n[c] );

    if ( !gtvdvb.rec_status ) gtv_gst_rec_remove ();
}


gboolean gtv_gst_rec_enc ( gchar *name_ch )
{	
	struct dvb_rec_all_list { const gchar *name; } dvb_all_rec_list_n[] =
	{
		{ "queue2"      }, { gtvpref.video_encoder },
		{ "queue2"      }, { gtvpref.audio_encoder },
		{ gtvpref.muxer }, { "filesink" }
	};

    for ( c = 0; c < G_N_ELEMENTS ( gtvgstbase.dvb_rec_all_n ); c++ )
    {
        gtvgstbase.dvb_rec_all_n[c] = gst_element_factory_make ( dvb_all_rec_list_n[c].name, NULL );

    	if ( !gtvgstbase.dvb_rec_all_n[c] )
    	{
            g_critical ( "gtv_gst_rec_enc:: element - %s - not be created.\n", dvb_all_rec_list_n[c].name );
            return FALSE;
    	}
    }


    if ( gtvdvb.video_enable )
        gtv_gst_set_prop ( gtvgstbase.dvb_rec_all_n[1], gtvpref.video_enc_p );

    gtv_gst_set_prop ( gtvgstbase.dvb_rec_all_n[3], gtvpref.audio_enc_p );


    if ( gtvdvb.video_enable ) { b = 0; } else { b = 2; }

  gst_element_set_state ( gtvgstdvb.dvbplay, GST_STATE_PAUSED );

    for ( c = b; c < G_N_ELEMENTS ( gtvgstbase.dvb_rec_all_n ); c++ )
        gst_bin_add ( GST_BIN (gtvgstdvb.dvbplay), gtvgstbase.dvb_rec_all_n[c] );

    if ( gtvdvb.video_enable )
        gst_element_link_many ( gtvgstbase.dvb_all_n[6], gtvgstbase.dvb_rec_all_n[0], gtvgstbase.dvb_rec_all_n[1], gtvgstbase.dvb_rec_all_n[4], NULL );

    gst_element_link_many ( gtvgstbase.dvb_all_n[14], gtvgstbase.dvb_rec_all_n[2], gtvgstbase.dvb_rec_all_n[3], gtvgstbase.dvb_rec_all_n[4], NULL );

    gst_element_link_many ( gtvgstbase.dvb_rec_all_n[4], gtvgstbase.dvb_rec_all_n[5], NULL );

	gchar *date_str = gtv_get_time_date_str ();
    gchar *file_rec = g_strdup_printf ( "%s/%s_%s.%s", gtvpref.rec_dir, name_ch, date_str, gtvpref.file_ext );
    
		g_object_set ( gtvgstbase.dvb_rec_all_n[5], "location", file_rec, NULL );
    
    g_free ( file_rec );
    g_free ( date_str );

    for ( c = b; c < G_N_ELEMENTS ( gtvgstbase.dvb_rec_all_n ); c++ )
        gst_element_set_state ( gtvgstbase.dvb_rec_all_n[c], GST_STATE_PAUSED );

    g_usleep ( 250000 );

  gst_element_set_state ( gtvgstdvb.dvbplay, GST_STATE_PLAYING );

	return TRUE;
}

struct list_types { const gchar *type; const gchar *parser; };

struct list_types list_type_video_n[] =
{
	{ "mpeg",   "mpegvideoparse"  },
	{ "h264",   "h264parse"       },
	{ "h265",   "h265parse"       },
	{ "vc1",    "vc1parse"        }
};
struct list_types list_type_audio_n[] =
{
    { "mpeg", "mpegaudioparse" 	},
    { "ac3",  "ac3parse" 		}, 
    { "aac",  "aacparse" 		}
};

static const gchar * gtv_iterate_elements ( GstElement *it_element, struct list_types list_types_all[], guint num )
{
	GstIterator *it = gst_bin_iterate_recurse ( GST_BIN ( it_element ) );
	GValue item = { 0, };
	gboolean done = FALSE;
	const gchar *ret = NULL;
  
	while ( !done )
	{
		switch ( gst_iterator_next ( it, &item ) ) 
		{
			case GST_ITERATOR_OK:
			{
				g_debug ( "GST_ITERATOR_OK \n" );
				GstElement *element = GST_ELEMENT ( g_value_get_object (&item) );
      
				gchar *object_name = gst_object_get_name ( GST_OBJECT ( element ) );
				
					if ( g_strrstr ( object_name, "parse" ) )
					{
						for ( c = 0; c < num; c++ )
							if ( g_strrstr ( object_name, list_types_all[c].type ) )
								ret = list_types_all[c].parser;
					}

					g_debug ( "Object name: %s \n", object_name );
					
				g_free ( object_name );
				g_value_reset (&item);
			}
				break;
			
			case GST_ITERATOR_RESYNC:
				g_debug ( "GST_ITERATOR_RESYNC \n" );
				gst_iterator_resync (it);
				break;
				
			case GST_ITERATOR_ERROR:
				g_debug ( "GST_ITERATOR_ERROR \n" );
				done = TRUE;
				break;
				
			case GST_ITERATOR_DONE:
				g_debug ( "GST_ITERATOR_DONE \n" );
				done = TRUE;
				break;
		}
	}
	
	g_value_unset ( &item );
	gst_iterator_free ( it );
	
	return ret;
}

gboolean gtv_gst_rec_ts ( gchar *name_ch )
{
	const gchar *video_parser = gtv_iterate_elements ( gtvgstbase.dvb_all_n[4],  list_type_video_n, G_N_ELEMENTS ( list_type_video_n ) );
	const gchar *audio_parser = gtv_iterate_elements ( gtvgstbase.dvb_all_n[12], list_type_audio_n, G_N_ELEMENTS ( list_type_audio_n ) );
	const gchar *audio_encode = "avenc_mp2";
	
	if ( !video_parser && !audio_parser ) return FALSE;
	
	gboolean audio_mpeg = FALSE;
	if ( g_strrstr ( audio_parser, "mpeg" ) ) audio_mpeg = TRUE;
	
	g_debug ( "rec ts:: video parser: %s | audio parser / enc: %s \n", 
				  video_parser, audio_mpeg ? audio_parser : audio_encode );
	
	struct dvb_rec_all_list { const gchar *name; } dvb_all_rec_list_n[] =
	{
		{ "queue2" }, { video_parser },
		{ "queue2" }, { audio_mpeg ? audio_parser : audio_encode },
		{ "mpegtsmux" }, { "filesink" }
	};

    for ( c = 0; c < G_N_ELEMENTS ( gtvgstbase.dvb_rec_all_n ); c++ )
    {
		if ( !dvb_all_rec_list_n[c].name ) continue;
		
        gtvgstbase.dvb_rec_all_n[c] = gst_element_factory_make ( dvb_all_rec_list_n[c].name, NULL );

    	if ( !gtvgstbase.dvb_rec_all_n[c] )
    	{
            g_critical ( "gtv_gst_rec_ts:: element - %s - not be created.\n", dvb_all_rec_list_n[c].name );
            return FALSE;
    	}
    }

    if ( gtvdvb.video_enable ) { b = 0; } else { b = 2; }

  gst_element_set_state ( gtvgstdvb.dvbplay, GST_STATE_PAUSED );

    for ( c = b; c < G_N_ELEMENTS ( gtvgstbase.dvb_rec_all_n ); c++ )
        gst_bin_add ( GST_BIN (gtvgstdvb.dvbplay), gtvgstbase.dvb_rec_all_n[c] );

    if ( gtvdvb.video_enable )
        gst_element_link_many ( gtvgstbase.dvb_all_n[2], gtvgstbase.dvb_rec_all_n[0], gtvgstbase.dvb_rec_all_n[1], gtvgstbase.dvb_rec_all_n[4], NULL );

    gst_element_link_many ( audio_mpeg ? gtvgstbase.dvb_all_n[10] : gtvgstbase.dvb_all_n[14], gtvgstbase.dvb_rec_all_n[2], gtvgstbase.dvb_rec_all_n[3], gtvgstbase.dvb_rec_all_n[4], NULL );

    gst_element_link_many ( gtvgstbase.dvb_rec_all_n[4], gtvgstbase.dvb_rec_all_n[5], NULL );

	gchar *date_str = gtv_get_time_date_str ();
    gchar *file_rec = g_strdup_printf ( "%s/%s_%s.%s", gtvpref.rec_dir, name_ch, date_str, "m2ts" );
	
		g_object_set ( gtvgstbase.dvb_rec_all_n[5], "location", file_rec, NULL );
    
    g_free ( file_rec );
    g_free ( date_str );

    for ( c = b; c < G_N_ELEMENTS ( gtvgstbase.dvb_rec_all_n ); c++ )
        gst_element_set_state ( gtvgstbase.dvb_rec_all_n[c], GST_STATE_PAUSED );

    g_usleep ( 250000 );

  gst_element_set_state ( gtvgstdvb.dvbplay, GST_STATE_PLAYING );
	
	return TRUE;
}

gboolean gtv_gst_rec ( gchar *name_ch )
{
	gboolean ret;
	
	if ( gtvdvb.rec_en_ts )
		ret = gtv_gst_rec_enc ( name_ch );
	else
		ret = gtv_gst_rec_ts  ( name_ch );

	return ret;
}


void gtv_gst_rec_remove ()
{	
    for ( c = b; c < G_N_ELEMENTS ( gtvgstbase.dvb_rec_all_n ); c++ )
        gst_bin_remove ( GST_BIN (gtvgstdvb.dvbplay), gtvgstbase.dvb_rec_all_n[c] );
}


static void gtv_gst_set_prop ( GstElement *element, gchar *av_enc_p )
{
    if ( gtvdvb.rec_en_ts && g_strrstr ( av_enc_p, "=" ) )
    {
		if ( g_str_has_prefix ( av_enc_p, " " ) || g_strrstr ( av_enc_p, " =" ) || g_strrstr ( av_enc_p, "= " ) || g_strrstr ( av_enc_p, "  " ) )
		{
			g_warning ( "gtv_gst_set_prop:: not set - %s \n", av_enc_p );
			return;
		}
		
        gchar **fields = g_strsplit ( av_enc_p, " ", 0 );
        guint numfields = g_strv_length ( fields );

        for ( j = 0; j < numfields; j++ )
        {
            gchar **splits = g_strsplit ( fields[j], "=", 0 );
            guint numsplits = g_strv_length ( splits );

            if ( numsplits != 3 )
            {
                g_warning ( "gtv_gst_set_prop:: not set ( numsplits < 3 ) - %s \n", fields[j] );
                g_strfreev ( splits );
                continue;
            }

            if ( g_strrstr ( splits[0], "int"    ) )
            {
                g_object_set ( element, splits[1], atoi ( splits[2] ), NULL );
            }

            if ( g_strrstr ( splits[0], "double" ) )
            {				
                gfloat cnv = gtv_str_to_double ( splits[2] );
                
                if ( cnv > 0 )
                    g_object_set ( element, splits[1], cnv, NULL );
            }

            if ( g_strrstr ( splits[0], "bool"   ) )
            {
                gchar *text = g_utf8_strdown ( splits[2], -1 );

                if ( g_strrstr ( text, "true" ) )
                    g_object_set ( element, splits[1], TRUE,  NULL );
                else
                    g_object_set ( element, splits[1], FALSE, NULL );

                g_free ( text );
            }

            if ( g_strrstr ( splits[0], "char"   ) )
            {
                g_object_set ( element, splits[1], splits[2], NULL );
            }
            
            g_debug ( "type %s | key = %s | val %s \n", splits[0], splits[1], splits[2] );

            g_strfreev ( splits );
        }

        g_strfreev ( fields );
    }
}
