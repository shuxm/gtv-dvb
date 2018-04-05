/*
 * Copyright 2014 - 2018 Stepan Perun
 * This program is free software.
 * License: Gnu Lesser General Public License
 * http://www.gnu.org/licenses/lgpl.html
*/


#include "gmp-media.h"
#include "gmp-dvb.h"
#include "gmp-mpegts.h"
#include "gmp-scan.h"
#include "gmp-pref.h"
#include "gmp-eqa.h"
#include "gmp-eqv.h"

#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gst/video/videooverlay.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>


struct GmpMedia
{
	guintptr win_handle_tv, win_handle_pl;
	gboolean media_tv_pl, video_enable, rec_status, rec_ses, rec_trig_vis, 
		     show_slider_menu, firstfile, all_info, state_subtitle;

	GstElement *playbin_pl, *videobln, *playequal;
	GstElement *dvbsrc_tv, *dvb_all_n[19], *dvb_rec_all_n[6];
	GstPad *pad_a_sink[MAX_AUDIO], *pad_a_src[MAX_AUDIO], *blockpad;
	guint count_audio_track, set_audio_track;

	GtkLabel *signal_snr;
	GtkProgressBar *bar_sgn, *bar_snr;	

	GtkScale *slider_menu, *slider_base,*slider_vol;
	GtkLabel *lab_pos, *lab_dur,*lab_pos_m, *lab_dur_m;
	GtkBox *sliders, *sliders_m;

	GtkTreeView *treeview_tv, *treeview_pl;
	GtkTreePath *gmp_index_path;

	gchar *ch_name;
	gulong slider_update_signal_id, slider_update_signal_id_m;
	guint s_time_d;
	gdouble volume_tv, volume_pl, slider_range_max;
};

struct GmpMedia gmpmedia;


static void gmp_treeview_to_file ( GtkTreeView *tree_view, gchar *filename );
static void gmp_media_next_pl ();


// ***** Gst create Digital TV & Media Player *****

static GstBusSyncReply gmp_media_bus_sync_handler ( GstBus *bus, GstMessage *message )
{
	if ( gmpmedia.all_info )
		g_debug ( "gmp_media_bus_sync_handler:: pending %s \n", gst_bus_have_pending ( bus ) ? "TRUE" : "FALSE" );

    if ( !gst_is_video_overlay_prepare_window_handle_message ( message ) )
        return GST_BUS_PASS;

	guintptr handle = 0;

	if ( gmpmedia.media_tv_pl )
		handle = gmpmedia.win_handle_tv;
	else
		handle = gmpmedia.win_handle_pl;

    if ( handle != 0 )
    {
        GstVideoOverlay *xoverlay = GST_VIDEO_OVERLAY ( GST_MESSAGE_SRC ( message ) );
        gst_video_overlay_set_window_handle ( xoverlay, handle );

    } else { g_warning ( "Should have obtained video_window_handle by now!" ); }

    gst_message_unref ( message );
    return GST_BUS_DROP;
}

static void gmp_msg_err ( GstBus *bus, GstMessage *msg )
{
    GError *err = NULL;
    gchar *dbg  = NULL;
		
    gst_message_parse_error ( msg, &err, &dbg );
    
    g_critical ( "gmp_msg_err:: %s (%s)\n", err->message, (dbg) ? dbg : "no details" );
    
    gmp_message_dialog ( "ERROR:", err->message, GTK_MESSAGE_ERROR );

    g_error_free ( err );
    g_free ( dbg );
    
	gmp_media_stop_all ();

	g_debug ( "gmp_msg_err:: pending %s \n", gst_bus_have_pending ( bus ) ? "TRUE" : "FALSE" );
}


// ***** Gst create Digital TV ***** 

static void gmp_msg_all ( GstBus *bus, GstMessage *msg )
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

            gmp_set_sgn_snr ( gmpmedia.dvbsrc_tv, gmpmedia.signal_snr, gmpmedia.bar_sgn, gmpmedia.bar_snr, (signal * 100) / 0xffff, (snr * 100) / 0xffff, hlook );
        }
    }
    
	if ( gmpmedia.all_info )
		g_debug ( "gmp_msg_all:: pending %s \n", gst_bus_have_pending ( bus ) ? "TRUE" : "FALSE" );
}

void gmp_msg_war ( GstBus *bus, GstMessage *msg )
{
    GError *war = NULL;
    gchar *dbg = NULL;

    gst_message_parse_warning ( msg, &war, &dbg );

	g_debug ( "gmp_msg_war:: %s (%s)\n", war->message, (dbg) ? dbg : "no details" );

    g_error_free ( war );
    g_free ( dbg );

	g_debug ( "gmp_msg_war:: pending %s \n", gst_bus_have_pending ( bus ) ? "TRUE" : "FALSE" );
}

static gboolean gmp_media_gst_create_tv ()
{
	gmp_mpegts_initialize ();
	
    gmpmedia.dvbsrc_tv  = gst_pipeline_new ( "pipeline0" );

    if ( !gmpmedia.dvbsrc_tv )
    {
        g_critical ( "gmp_media_gst_create_tv:: pipeline - not created. \n" );        
        return FALSE;
    }

    GstBus *bus = gst_element_get_bus ( gmpmedia.dvbsrc_tv );
    gst_bus_add_signal_watch_full ( bus, G_PRIORITY_DEFAULT );
    gst_bus_set_sync_handler ( bus, (GstBusSyncHandler)gmp_media_bus_sync_handler, NULL, NULL );
    gst_object_unref (bus);

    g_signal_connect ( bus, "message",          G_CALLBACK ( gmp_msg_all ), NULL );
    g_signal_connect ( bus, "message::error",   G_CALLBACK ( gmp_msg_err ), NULL );
    g_signal_connect ( bus, "message::warning", G_CALLBACK ( gmp_msg_war ), NULL );
    
    gmp_scan_gst_create ();

    return TRUE;
}


// ***** Gst create Media Player *****

static void gmp_msg_cll ( /*GstBus *bus, GstMessage *msg*/ )
{
	if ( GST_ELEMENT_CAST ( gmpmedia.playbin_pl )->current_state == GST_STATE_PLAYING )
    {
        gst_element_set_state ( gmpmedia.playbin_pl, GST_STATE_PAUSED  );
        gst_element_set_state ( gmpmedia.playbin_pl, GST_STATE_PLAYING );
    }
}

static void gmp_msg_eos ( /*GstBus *bus, GstMessage *msg*/ )
{
	gmp_media_next_pl ();
}

static void gmp_msg_buf ( GstBus *bus, GstMessage *msg )
{
    gint percent;
    gst_message_parse_buffering ( msg, &percent );

    if ( percent == 100 )
    {
        if ( GST_ELEMENT_CAST ( gmpmedia.playbin_pl )->current_state == GST_STATE_PAUSED )
            gst_element_set_state ( gmpmedia.playbin_pl, GST_STATE_PLAYING );
    }
    else
    {
        if ( GST_ELEMENT_CAST ( gmpmedia.playbin_pl )->current_state == GST_STATE_PLAYING )
            gst_element_set_state ( gmpmedia.playbin_pl, GST_STATE_PAUSED );
	
		g_print ( "buffering: %d %s \n", percent, "%" );
	}

	if ( gmpmedia.all_info )
		g_debug ( "gmp_msg_buf:: pending %s \n", gst_bus_have_pending ( bus ) ? "TRUE" : "FALSE" );
}

static gboolean gmp_media_gst_create_pl ()
{
    gmpmedia.playbin_pl  = gst_element_factory_make ( "playbin", "playbin" );

    GstElement *bin_audio, *bin_video, *vsink, *asink;

    vsink     = gst_element_factory_make ( "autovideosink",     NULL );
    gmpmedia.videobln  = gst_element_factory_make ( "videobalance",      NULL );

    asink     = gst_element_factory_make ( "autoaudiosink",     NULL );
    gmpmedia.playequal = gst_element_factory_make ( "equalizer-nbands",  NULL );


    if ( !gmpmedia.playbin_pl || !vsink || !gmpmedia.videobln || !asink || !gmpmedia.playequal )
	{
        g_printerr ( "gmp_media_gst_create_pl - not all elements could be created.\n" );
		return FALSE;	
	}

    bin_audio = gst_bin_new ( "audio_sink_bin" );
    gst_bin_add_many (GST_BIN ( bin_audio), gmpmedia.playequal, asink, NULL );
    gst_element_link_many ( gmpmedia.playequal, asink, NULL );

    GstPad *pad = gst_element_get_static_pad ( gmpmedia.playequal, "sink" );
    gst_element_add_pad ( bin_audio, gst_ghost_pad_new ( "sink", pad ) );
    gst_object_unref ( pad );


    bin_video = gst_bin_new ( "video_sink_bin" );
    gst_bin_add_many ( GST_BIN (bin_video), gmpmedia.videobln, vsink, NULL );
    gst_element_link_many ( gmpmedia.videobln, vsink, NULL );

    GstPad *padv = gst_element_get_static_pad ( gmpmedia.videobln, "sink" );
    gst_element_add_pad ( bin_video, gst_ghost_pad_new ( "sink", padv ) );
    gst_object_unref ( padv );

    g_object_set ( gmpmedia.playbin_pl, "video-sink", bin_video, NULL );
    g_object_set ( gmpmedia.playbin_pl, "audio-sink", bin_audio, NULL );

    g_object_set ( gmpmedia.playbin_pl, "volume", gmpmedia.volume_pl, NULL );


    GstBus *bus = gst_element_get_bus ( gmpmedia.playbin_pl );
    gst_bus_add_signal_watch_full ( bus, G_PRIORITY_DEFAULT );
    gst_bus_set_sync_handler ( bus, (GstBusSyncHandler)gmp_media_bus_sync_handler, NULL, NULL );
    gst_object_unref ( bus );

	g_signal_connect ( bus, "message::eos",           G_CALLBACK ( gmp_msg_eos ),  NULL );
    g_signal_connect ( bus, "message::error",         G_CALLBACK ( gmp_msg_err ),  NULL );
    g_signal_connect ( bus, "message::clock-lost",    G_CALLBACK ( gmp_msg_cll ),  NULL );
    g_signal_connect ( bus, "message::buffering",     G_CALLBACK ( gmp_msg_buf ),  NULL );

    return TRUE;
}

// ***** Gst create Digital TV & Media Player *****


// ***** Digital TV *****

static gboolean gmp_set_audio_track ( GstPad *pad, GstElement *element, gint set_track_audio, const gchar *name, GstElement *element_n )
{
    gboolean audio_changed = TRUE;

    if ( pad )
    {
        gmpmedia.pad_a_sink[gmpmedia.count_audio_track] = gst_element_get_static_pad ( element, "sink" );
        gmpmedia.pad_a_src [gmpmedia.count_audio_track] = pad;

        if ( gst_pad_link ( pad, gmpmedia.pad_a_sink[gmpmedia.count_audio_track] ) == GST_PAD_LINK_OK )
            gst_object_unref ( gmpmedia.pad_a_sink[gmpmedia.count_audio_track] );
        else
        {
            gchar *object_name = gst_object_get_name ( GST_OBJECT ( element_n ) );
                g_debug ( "Linking demux name: %s & audio pad failed - %s \n", object_name, name );
            g_free ( object_name );
        }

        gmpmedia.count_audio_track++;
    }
    else
    {
        if ( !gst_pad_unlink ( gmpmedia.pad_a_src[gmpmedia.set_audio_track], gmpmedia.pad_a_sink[gmpmedia.set_audio_track] ) )
            audio_changed = FALSE;

        if ( gst_pad_link ( gmpmedia.pad_a_src[set_track_audio], gmpmedia.pad_a_sink[set_track_audio] ) != GST_PAD_LINK_OK )
            audio_changed = FALSE;
    }

    return audio_changed;
}

static void tv_changed_audio_track ( gint changed_track_audio )
{
    gmp_set_audio_track ( NULL, NULL, changed_track_audio, NULL, NULL );
    gmpmedia.set_audio_track = changed_track_audio;
}


static void gmp_gst_pad_link ( GstPad *pad, GstElement *element, const gchar *name, GstElement *element_n )
{
    GstPad *pad_va_sink = gst_element_get_static_pad ( element, "sink" );

    if ( gst_pad_link ( pad, pad_va_sink ) == GST_PAD_LINK_OK )
    	gst_object_unref ( pad_va_sink );
    else
	{
		gchar *name_en = gst_object_get_name ( GST_OBJECT ( element_n ) );	
	        g_debug ( "gmp_gst_pad_link:: linking demux/decode name %s video/audio pad failed ( name_en %s ) \n", name, name_en );
		g_free ( name_en );
	}
}

static void gmp_pad_demux_added_audio ( GstElement *element, GstPad *pad, GstElement *element_audio )
{	
    const gchar *name = gst_structure_get_name ( gst_caps_get_structure ( gst_pad_query_caps ( pad, NULL ), 0 ) );
       
    if ( g_str_has_prefix ( name, "audio" ) )
    	//gmp_gst_pad_link ( pad, element_audio, name, element );
    	gmp_set_audio_track ( pad, element_audio, gmpmedia.count_audio_track, name, element );
}
static void gmp_pad_demux_added_video ( GstElement *element, GstPad *pad, GstElement *element_video )
{
    const gchar *name = gst_structure_get_name ( gst_caps_get_structure ( gst_pad_query_caps ( pad, NULL ), 0 ) );

    if ( g_str_has_prefix ( name, "video" ) )
        gmp_gst_pad_link ( pad, element_video, name, element );
}

static void gmp_pad_decode_added ( GstElement *element, GstPad *pad, GstElement *element_va )
{	
    const gchar *name = gst_structure_get_name ( gst_caps_get_structure ( gst_pad_query_caps ( pad, NULL ), 0 ) );

    gmp_gst_pad_link ( pad, element_va, name, element );
}


void gmp_gst_tsdemux ()
{
    gmpmedia.count_audio_track = 0;
    gmpmedia.set_audio_track   = 0;	

struct dvb_all_list { const gchar *name; } dvb_all_list_n[] =
{
    { "dvbsrc" }, { "tsdemux" },
    { "tee"    }, { "queue2"  }, { "decodebin" }, { "videoconvert" }, { "tee" }, { "queue2"  },/*{ "queue2" },*/ { "videobalance"     }, { "autovideosink" },
    { "tee"    }, { "queue2"  }, { "decodebin" }, { "audioconvert" }, { "tee" }, { "queue2"  },  { "volume" },   { "equalizer-nbands" }, { "autoaudiosink" }
};
	guint c = 0;
    for ( c = 0; c < G_N_ELEMENTS ( gmpmedia.dvb_all_n ); c++ )
    {
        gmpmedia.dvb_all_n[c] = gst_element_factory_make ( dvb_all_list_n[c].name, NULL );
        
        if ( !gmpmedia.dvb_all_n[c] ) 
			g_critical ( "dvb_all_list:: element (factory make)  - %s not created. \n", dvb_all_list_n[c].name );
        
        if ( !gmpmedia.video_enable && ( c == 2 || c == 3 || c == 4 || c == 5 || c == 6 || c == 7 || c == 8 || c == 9 ) ) continue;
        
        gst_bin_add ( GST_BIN ( gmpmedia.dvbsrc_tv ), gmpmedia.dvb_all_n[c] );
    	
    	if (  c == 1 || c == 3 || c == 4 || c == 6 || c == 7 || c == 8 || c == 9
           || c == 11 || c == 12 || c == 14 || c == 15 || c == 16 || c == 17 || c == 18 )
           gst_element_link ( gmpmedia.dvb_all_n[c-1], gmpmedia.dvb_all_n[c] );
    }

    g_signal_connect ( gmpmedia.dvb_all_n[1], "pad-added", G_CALLBACK ( gmp_pad_demux_added_audio ), gmpmedia.dvb_all_n[10] );
    g_signal_connect ( gmpmedia.dvb_all_n[1], "pad-added", G_CALLBACK ( gmp_pad_demux_added_video ), gmpmedia.dvb_all_n[2] );

    if ( gmpmedia.video_enable )    
        g_signal_connect ( gmpmedia.dvb_all_n[4], "pad-added", G_CALLBACK ( gmp_pad_decode_added ), gmpmedia.dvb_all_n[5] );

    g_signal_connect ( gmpmedia.dvb_all_n[12], "pad-added", G_CALLBACK ( gmp_pad_decode_added ), gmpmedia.dvb_all_n[13] );
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

static const gchar * gmp_iterate_elements ( GstElement *it_element, struct list_types list_types_all[], guint num )
{
	GstIterator *it = gst_bin_iterate_recurse ( GST_BIN ( it_element ) );
	GValue item = { 0, };
	gboolean done = FALSE;
	const gchar *ret = NULL;
	guint c = 0;
  
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

void gmp_gst_rec_ts ()
{
	const gchar *video_parser = gmp_iterate_elements ( gmpmedia.dvb_all_n[4],  list_type_video_n, G_N_ELEMENTS ( list_type_video_n ) );
	const gchar *audio_parser = gmp_iterate_elements ( gmpmedia.dvb_all_n[12], list_type_audio_n, G_N_ELEMENTS ( list_type_audio_n ) );
	const gchar *audio_encode = "avenc_mp2";
	
	if ( !video_parser && !audio_parser ) return;
	
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
	
	gst_element_set_state ( gmpmedia.dvbsrc_tv, GST_STATE_PAUSED );

	guint c = 0;
    for ( c = 0; c < G_N_ELEMENTS ( gmpmedia.dvb_rec_all_n ); c++ )
    {
		if ( !dvb_all_rec_list_n[c].name ) continue;
		
        gmpmedia.dvb_rec_all_n[c] = gst_element_factory_make ( dvb_all_rec_list_n[c].name, NULL );

    	if ( !gmpmedia.dvb_rec_all_n[c] ) 
			g_critical ( "dvb_all_list:: element (factory make)  - %s not created. \n", dvb_all_rec_list_n[c].name );
    	
    	if ( !gmpmedia.video_enable && ( c == 0 || c == 1 ) ) continue;
    	
    	gst_bin_add ( GST_BIN ( gmpmedia.dvbsrc_tv ), gmpmedia.dvb_rec_all_n[c] );
    	
    	if ( c == 1 || c == 3 || c == 5 )
			gst_element_link ( gmpmedia.dvb_rec_all_n[c-1], gmpmedia.dvb_rec_all_n[c] );
    }
    
    gmpmedia.blockpad = gst_element_get_static_pad ( gmpmedia.dvb_rec_all_n[4], "src" );

    if ( gmpmedia.video_enable )
    {
        gst_element_link ( gmpmedia.dvb_all_n[2],     gmpmedia.dvb_rec_all_n[0] );
        gst_element_link ( gmpmedia.dvb_rec_all_n[1], gmpmedia.dvb_rec_all_n[4] );
	}

    gst_element_link ( audio_mpeg ? gmpmedia.dvb_all_n[10] : gmpmedia.dvb_all_n[14], gmpmedia.dvb_rec_all_n[2] );
    gst_element_link ( gmpmedia.dvb_rec_all_n[3], gmpmedia.dvb_rec_all_n[4] );

	gchar *date_str = gmp_pref_get_time_date_str ();
    gchar *file_rec = g_strdup_printf ( "%s/%s_%s.%s", gmp_rec_dir, gmpmedia.ch_name, date_str, "m2ts" );
	
		g_object_set ( gmpmedia.dvb_rec_all_n[5], "location", file_rec, NULL );
    
    g_free ( file_rec );
    g_free ( date_str );

    for ( c = 0; c < G_N_ELEMENTS ( gmpmedia.dvb_rec_all_n ) - 1; c++ )
		if ( !gmpmedia.video_enable && ( c == 0 || c == 1 ) ) continue;
			gst_element_set_state ( gmpmedia.dvb_rec_all_n[c], GST_STATE_PAUSED );

	gst_element_set_state ( gmpmedia.dvbsrc_tv, GST_STATE_PLAYING );
}

void gmp_gst_tsdemux_remove ()
{
	g_debug ( "gmp_gst_tsdemux_remove \n" );
	
	GstIterator *it = gst_bin_iterate_elements ( GST_BIN ( gmpmedia.dvbsrc_tv ) );
	GValue item = { 0, };
	gboolean done = FALSE;
  
	while ( !done )
	{
		switch ( gst_iterator_next ( it, &item ) ) 
		{
			case GST_ITERATOR_OK:
			{
				g_debug ( "GST_ITERATOR_OK \n" );
				GstElement *element = GST_ELEMENT ( g_value_get_object (&item) );
      
				gchar *object_name = gst_object_get_name ( GST_OBJECT ( element ) );
				
					g_debug ( "Object remove: %s \n", object_name );
					
					gst_bin_remove ( GST_BIN ( gmpmedia.dvbsrc_tv ), element );
					
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

	g_debug ( "End remove \n\n" );
}

static void gmp_checked_video ( gchar *data )
{
	if ( !g_strrstr ( data, "video-pid" ) || g_strrstr ( data, "video-pid=0" ) )
		 gmpmedia.video_enable = FALSE;
	else
		 gmpmedia.video_enable = TRUE;
}

static void gmp_set_tuning_timeout ( GstElement *element )
{
    guint64 timeout = 0;
    g_object_get ( element, "tuning-timeout", &timeout, NULL );
    g_object_set ( element, "tuning-timeout", (guint64)timeout / 5, NULL );
}

static void gmp_check_a_f ( GstElement *element )
{
	guint adapter = 0, frontend = 0;
	g_object_get ( element, "adapter",  &adapter,  NULL );
    g_object_get ( element, "frontend", &frontend, NULL );
    
	gmp_get_dvb_info ( FALSE, adapter, frontend );
}

static void gmp_data_split_set_dvb ( gchar *data )
{
	g_debug ( "gmp_data_split_set_dvb:: data %s \n", data );
	
    GstElement *element = gmpmedia.dvb_all_n[0];
    gmp_set_tuning_timeout ( element );

    gchar **fields = g_strsplit ( data, ":", 0 );
    guint numfields = g_strv_length ( fields );

	if ( gmpmedia.ch_name ) g_free ( gmpmedia.ch_name );
    gmpmedia.ch_name = g_strdup ( fields[0] );

	guint j = 0;
    for ( j = 1; j < numfields; j++ )
    {
        if ( g_strrstr ( fields[j], "delsys" ) || g_strrstr ( fields[j], "audio-pid" ) || g_strrstr ( fields[j], "video-pid" ) ) continue;

        if ( !g_strrstr ( fields[j], "=" ) ) continue;

        gchar **splits = g_strsplit ( fields[j], "=", 0 );
        
			if ( g_strrstr ( splits[0], "polarity" ) )
            {
				if ( splits[1][0] == 'v' || splits[1][0] == 'V' || splits[1][0] == '0' )
					g_object_set ( element, "polarity", "V", NULL );
                else
					g_object_set ( element, "polarity", "H", NULL );
				
				continue;
			}
			
			long dat = atol ( splits[1] );

            if ( g_strrstr ( splits[0], "program-number" ) )
            {
                g_object_set ( gmpmedia.dvb_all_n[1], "program-number", dat, NULL );
            }
            else if ( g_strrstr ( splits[0], "symbol-rate" ) )
				g_object_set ( element, "symbol-rate", ( dat > 100000) ? dat/1000 : dat, NULL );
            else if ( g_strrstr ( splits[0], "lnb-type" ) )
                gmp_set_lnb ( element, dat );
            else
                g_object_set ( element, splits[0], dat, NULL );

        g_strfreev (splits);
    }

    g_strfreev (fields);
    
    gmp_check_a_f ( element );
}

static GstPadProbeReturn gmp_blockpad_probe_event ( GstPad * pad, GstPadProbeInfo * info, gpointer user_data )
{
	if ( gmpmedia.all_info )
		g_print ( "gmp_blockpad_probe_event:: user_data %s \n", (gchar *)user_data );
	
	if ( GST_EVENT_TYPE (GST_PAD_PROBE_INFO_DATA (info)) != GST_EVENT_EOS )
		return GST_PAD_PROBE_PASS;

	gst_pad_remove_probe ( pad, GST_PAD_PROBE_INFO_ID (info) );
  
	gst_element_set_state ( gmpmedia.dvb_rec_all_n[5], GST_STATE_NULL );
  
  
		if ( gmpmedia.rec_ses )
		{
			gchar *date_str = gmp_pref_get_time_date_str ();
			gchar *file_rec = g_strdup_printf ( "%s/%s_%s.%s", gmp_rec_dir, gmpmedia.ch_name, date_str, "m2ts" );
			
			g_object_set ( gmpmedia.dvb_rec_all_n[5], "location", file_rec, NULL );
							
			g_free ( file_rec );
			g_free ( date_str );
		}
		else
			g_object_set ( gmpmedia.dvb_rec_all_n[5], "location", "/dev/null", NULL );
  
		gst_element_link ( gmpmedia.dvb_rec_all_n[4], gmpmedia.dvb_rec_all_n[5] );
  
	gst_element_set_state ( gmpmedia.dvb_rec_all_n[5], GST_STATE_PLAYING );

	return GST_PAD_PROBE_DROP;
}

static GstPadProbeReturn gmp_blockpad_probe ( GstPad * pad, GstPadProbeInfo * info, gpointer user_data )
{
	GstPad *sinkpad;

	gst_pad_remove_probe ( pad, GST_PAD_PROBE_INFO_ID (info) );

	sinkpad = gst_element_get_static_pad ( gmpmedia.dvb_rec_all_n[5], "sink" );
  
	gst_pad_add_probe ( sinkpad, GST_PAD_PROBE_TYPE_BLOCK | GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, 
						gmp_blockpad_probe_event, user_data, NULL );

	gst_pad_send_event ( sinkpad, gst_event_new_eos () );

	gst_object_unref ( sinkpad );

  return GST_PAD_PROBE_OK;
}

static void gmp_media_dvb_record ()
{
	g_debug ( "gmp_media_dvb_record \n" );
	
	if ( GST_ELEMENT_CAST ( gmpmedia.dvbsrc_tv )->current_state != GST_STATE_PLAYING ) return;

    if ( !gmpmedia.rec_status )
    {
        gmp_gst_rec_ts ();
        gmpmedia.rec_status = !gmpmedia.rec_status;
        gmpmedia.rec_ses = TRUE;
    }
    else
    {
		gmpmedia.rec_ses = !gmpmedia.rec_ses;
		
		gst_pad_add_probe ( gmpmedia.blockpad, GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM,
							gmp_blockpad_probe, "blockpad", NULL );
    }
}

static void gmp_media_dvb_stop ()
{
	g_debug ( "gmp_media_dvb_stop \n" );
	
	if ( GST_ELEMENT_CAST ( gmpmedia.dvbsrc_tv )->current_state != GST_STATE_NULL )
	{
		gst_element_set_state ( gmpmedia.dvbsrc_tv, GST_STATE_NULL );
		gmp_gst_tsdemux_remove ();
		gmpmedia.rec_status = FALSE;
		gmpmedia.rec_ses = FALSE;
		
		gmp_set_sgn_snr ( gmpmedia.dvbsrc_tv, gmpmedia.signal_snr, gmpmedia.bar_sgn, gmpmedia.bar_snr, 0, 0, FALSE );
		gtk_label_set_text ( gmpmedia.signal_snr, "Level  &  Quality" );

		gmp_base_update_win ();
	}
}

static gboolean gmp_update_data_win_all ()
{
	gmp_base_update_win ();
	
	return FALSE;
}

static void gmp_media_channel_play ( gchar *data )
{
	g_debug ( "gmp_media_channel_play \n" );
	
	gmp_media_dvb_stop ();

	if ( GST_ELEMENT_CAST ( gmpmedia.dvbsrc_tv )->current_state != GST_STATE_PLAYING )
	{
        gmp_checked_video ( data );
        gmp_gst_tsdemux ();

		g_object_set ( gmpmedia.dvb_all_n[16], "volume", gmpmedia.volume_pl, NULL );
		gmp_data_split_set_dvb ( data );

        gst_element_set_state ( gmpmedia.dvbsrc_tv, GST_STATE_PLAYING );
        
        g_timeout_add ( 250, (GSourceFunc)gmp_update_data_win_all, NULL );
	}
}

// ***** Digital TV *****


// ***** Media Player *****

static void gmp_volume_changed_pl_tv ( GtkScaleButton *button, gdouble value )
{
	if ( GST_ELEMENT_CAST ( gmpmedia.playbin_pl )->current_state == GST_STATE_PLAYING )
		gtk_range_set_value ( GTK_RANGE ( gmpmedia.slider_vol ), value );

    if ( GST_ELEMENT_CAST ( gmpmedia.dvbsrc_tv )->current_state == GST_STATE_PLAYING )
        g_object_set ( gmpmedia.dvb_all_n[16], "volume", value, NULL );

	if ( gmpmedia.media_tv_pl )
		gmpmedia.volume_tv = value;
	else
		gmpmedia.volume_pl = value;

	g_debug ( "gmp_volume_changed_pl_tv:: widget name %s | value %f \n", gtk_widget_get_name ( GTK_WIDGET ( button ) ), value );
}

static void gmp_slider_changed_vol ( GtkRange *range )
{
	gdouble value = gtk_range_get_value ( GTK_RANGE (range) );

    if ( GST_ELEMENT_CAST ( gmpmedia.playbin_pl )->current_state == GST_STATE_PLAYING )
        g_object_set ( gmpmedia.playbin_pl, "volume", value, NULL );

	gmpmedia.volume_pl = value;

	g_debug ( "gmp_slider_changed_vol:: widget name %s | value %f \n", gtk_widget_get_name ( GTK_WIDGET ( range ) ), value );
}

static gboolean gmp_volume_mute_get ( GstElement *volume_get )
{
	if ( gmpmedia.media_tv_pl && GST_ELEMENT_CAST ( gmpmedia.dvbsrc_tv )->current_state == GST_STATE_NULL ) return FALSE;
	
	if ( !gmpmedia.media_tv_pl && GST_ELEMENT_CAST ( gmpmedia.playbin_pl )->current_state == GST_STATE_NULL ) return FALSE;

    gboolean mute = FALSE;

    g_object_get ( volume_get, "mute", &mute, NULL );

	return mute;
}
static void gmp_volume_mute ( GstElement *volume_set, GtkWidget *widget )
{
    gboolean mute = FALSE;

    g_object_get ( volume_set, "mute", &mute, NULL );
    g_object_set ( volume_set, "mute", !mute, NULL );

	gtk_widget_set_sensitive ( GTK_WIDGET ( widget ), mute );
    if ( !gmpmedia.media_tv_pl ) gtk_widget_set_sensitive ( GTK_WIDGET ( gmpmedia.slider_vol ), mute );

	g_debug ( "gmp_volume_mute:: mute %s | widget name %s\n", mute ? "TRUE" : "FALSE", 
		       gtk_widget_get_name ( GTK_WIDGET ( widget ) ) );
}
static void gmp_media_pl_tv_mute ( GtkButton *button, GtkWidget *widget )
{
	if ( GST_ELEMENT_CAST ( gmpmedia.dvbsrc_tv )->current_state == GST_STATE_PLAYING )
		gmp_volume_mute ( gmpmedia.dvb_all_n[16], widget );

    if ( GST_ELEMENT_CAST ( gmpmedia.playbin_pl )->current_state == GST_STATE_PLAYING )
        gmp_volume_mute ( gmpmedia.playbin_pl, widget );

	g_debug ( "gmp_media_pl_tv_mute:: widget name %s \n", gtk_widget_get_name ( GTK_WIDGET ( button ) ) );
}


static void gmp_slider_m_update ( gdouble data )
{
	if ( !gmpmedia.show_slider_menu ) return;

    g_signal_handler_block ( gmpmedia.slider_menu, gmpmedia.slider_update_signal_id_m );
		gtk_range_set_value ( GTK_RANGE ( gmpmedia.slider_menu ), data );
    g_signal_handler_unblock ( gmpmedia.slider_menu, gmpmedia.slider_update_signal_id_m );
}
static void gmp_slider_update ( gdouble data )
{
    g_signal_handler_block ( gmpmedia.slider_base, gmpmedia.slider_update_signal_id );
        gtk_range_set_value ( GTK_RANGE ( gmpmedia.slider_base ), data );
    g_signal_handler_unblock ( gmpmedia.slider_base, gmpmedia.slider_update_signal_id );

	gmp_slider_m_update ( data );

	g_debug ( "gmp_slider_update:: value %f \n", data );
}

static void gmp_slider_m_changed ( GtkRange *range )
{
    gtk_range_set_value ( GTK_RANGE ( gmpmedia.slider_base ), gtk_range_get_value ( GTK_RANGE (range) ) );

	g_debug ( "gmp_slider_m_changed:: widget name %s \n", gtk_widget_get_name ( GTK_WIDGET ( range ) ) );
}
static void gmp_slider_changed ( GtkRange *range )
{
    gdouble value = gtk_range_get_value ( GTK_RANGE (range) );
    gst_element_seek_simple ( gmpmedia.playbin_pl, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT, (gint64)( value * GST_SECOND ) );

	gmp_slider_m_update ( value );

	g_debug ( "gmp_slider_changed:: widget name %s \n", gtk_widget_get_name ( GTK_WIDGET ( range ) ) );
}

static void gmp_label_set_text ( GtkLabel *label, gint64 pos_dur )
{
    gchar *str   = g_strdup_printf ( "%" GST_TIME_FORMAT, GST_TIME_ARGS ( pos_dur ) );
    gchar *str_l = g_strndup ( str, strlen ( str ) - 8 );

    gtk_label_set_text ( label, str_l );

    g_free ( str_l  );
    g_free ( str );
}
static void gmp_pos_dur_text ( gint64 pos, gint64 dur )
{
    gmp_label_set_text ( gmpmedia.lab_pos, pos );
    gmp_label_set_text ( gmpmedia.lab_dur, dur );

	if ( gmpmedia.show_slider_menu )
	{
    	gmp_label_set_text ( gmpmedia.lab_pos_m, pos );
    	gmp_label_set_text ( gmpmedia.lab_dur_m, dur );
	}
}

static void gmp_set_sensitive ( gboolean sensitive_trc, gboolean sensitive_vol )
{
	g_debug ( "gmp_set_sensitive \n" );
	
	gtk_widget_set_sensitive ( GTK_WIDGET ( gmpmedia.slider_base ), sensitive_trc );

	if ( gmpmedia.show_slider_menu )
		gtk_widget_set_sensitive ( GTK_WIDGET ( gmpmedia.sliders_m ), sensitive_trc );

	gtk_widget_set_sensitive ( GTK_WIDGET ( gmpmedia.slider_vol ), sensitive_vol );
}

static gboolean gmp_refresh ()
{
	// g_debug ( "gmp_refresh \n" );

    GstFormat fmt = GST_FORMAT_TIME;
    gint64 duration, current = -1;

    if ( gst_element_query_duration ( gmpmedia.playbin_pl, fmt, &duration ) 
		 && gst_element_query_position ( gmpmedia.playbin_pl, fmt, &current ) )
    {
        if ( duration == -1 )
        {
            gmp_pos_dur_text ( current, duration );
        }
        else
        {
            if ( current / GST_SECOND < duration / GST_SECOND )
            {
				gmpmedia.slider_range_max = (gdouble)duration / GST_SECOND;
				
                gtk_range_set_range ( GTK_RANGE ( gmpmedia.slider_base ), 0, gmpmedia.slider_range_max );

				if ( gmpmedia.show_slider_menu )
	                gtk_range_set_range ( GTK_RANGE ( gmpmedia.slider_menu ), 0, gmpmedia.slider_range_max );

                gmp_slider_update ( (gdouble)current / GST_SECOND );
                gmp_pos_dur_text ( current, duration );
            
            } else { gmp_media_next_pl (); }
        }
    }

    return TRUE;
}

static void gmp_media_playback_start ()
{
	if ( !gmpmedia.firstfile ) return;

	if ( GST_ELEMENT_CAST ( gmpmedia.playbin_pl )->current_state != GST_STATE_PLAYING )
	{
		gst_element_set_state ( gmpmedia.playbin_pl, GST_STATE_PLAYING );
	
		gmpmedia.s_time_d = g_timeout_add ( 100, (GSourceFunc)gmp_refresh, NULL );

		gmp_set_sensitive ( TRUE, !gmp_volume_mute_get ( gmpmedia.playbin_pl ) );
		
		g_timeout_add ( 250, (GSourceFunc)gmp_update_data_win_all, NULL );
	}
	
	g_debug ( "gmp_media_playback_start \n" );
}
static void gmp_media_playback_pause ()
{
	g_debug ( "gmp_media_playback_pause \n" );
	
	if ( GST_ELEMENT_CAST ( gmpmedia.playbin_pl )->current_state == GST_STATE_PLAYING )
	{
		gst_element_set_state ( gmpmedia.playbin_pl, GST_STATE_PAUSED );
		g_source_remove ( gmpmedia.s_time_d );
	}
}
static void gmp_media_playback_stop ()
{
	g_debug ( "gmp_media_playback_stop \n" );
	
	if ( GST_ELEMENT_CAST ( gmpmedia.playbin_pl )->current_state != GST_STATE_NULL )
	{
		if ( GST_ELEMENT_CAST ( gmpmedia.playbin_pl )->current_state != GST_STATE_PAUSED )
			g_source_remove ( gmpmedia.s_time_d );

		gst_element_set_state ( gmpmedia.playbin_pl, GST_STATE_NULL );

		gmp_set_sensitive ( FALSE, FALSE );
        gmp_slider_update ( 0 );
        gmp_pos_dur_text  ( 0, 0 );

		gmp_base_update_win ();
	}
}

static void gmp_media_stop_set_play ( gchar *name_file )
{
	g_debug ( "gmp_media_stop_set_play \n" );
	
    gmp_media_playback_stop ();

    if ( g_strrstr ( name_file, "://" ) )
        g_object_set ( gmpmedia.playbin_pl, "uri", name_file, NULL );
    else
    {
        gchar *uri = gst_filename_to_uri ( name_file, NULL );
            g_object_set ( gmpmedia.playbin_pl, "uri", uri, NULL );
        g_free ( uri );
    }

	gmpmedia.firstfile = TRUE;

    gmp_media_playback_start ();
}

static void gmp_media_next_pl ()
{
	g_debug ( "gmp_media_next_pl \n" );
	
    GtkTreeModel *model = gtk_tree_view_get_model ( GTK_TREE_VIEW ( gmpmedia.treeview_pl ) );
    guint ind = gtk_tree_model_iter_n_children ( model, NULL );

    if ( !gmpmedia.gmp_index_path || ind < 2 )
    {
		gmp_media_playback_stop (); 
		return;
	}

    GtkTreeIter iter;

    if ( gtk_tree_model_get_iter ( model, &iter, gmpmedia.gmp_index_path ) )
    {
        if ( gtk_tree_model_iter_next ( model, &iter ) )
        {
            gchar *file_ch = NULL;

                gtk_tree_model_get ( model, &iter, COL_DATA, &file_ch, -1 );
                gmpmedia.gmp_index_path = gtk_tree_model_get_path ( model, &iter );
                gtk_tree_selection_select_iter ( gtk_tree_view_get_selection ( gmpmedia.treeview_pl ), &iter );
                gmp_media_stop_set_play ( file_ch );

            g_free ( file_ch );
        }
		else
			gmp_media_playback_stop ();
    }
    else
		gmp_media_playback_stop ();
}

// ***** Media Player *****

// ***** Panel *****

void gmp_media_stop_all ()
{
	g_debug ( "gmp_media_stop_all \n" );
	
	gmp_media_dvb_stop ();
	gmp_media_playback_stop ();
}

static void gmp_media_menu_quit ( GtkWidget *window )
{
    gtk_widget_destroy ( GTK_WIDGET ( window ) );
	gmpmedia.show_slider_menu = FALSE;
}
static void gmp_media_menu_close ( GtkButton *button, GtkWidget *window )
{
    gmp_media_menu_quit ( GTK_WIDGET ( window ) );

	g_debug ( "gmp_media_menu_close:: widget name %s \n", gtk_widget_get_name ( GTK_WIDGET ( button ) ) );
}
static void gmp_media_base_window ( GtkButton *button, GtkWidget *window )
{
	gmp_media_stop_all ();
	
	if ( gmpmedia.media_tv_pl )
		gmp_treeview_to_file ( gmpmedia.treeview_tv, (gchar *)ch_conf );

	gmp_media_menu_quit ( GTK_WIDGET ( window ) );
    gmp_base_set_window ();

	g_debug ( "gmp_media_base_window:: widget name %s \n", gtk_widget_get_name ( GTK_WIDGET ( button ) ) );
}

static void gmp_media_scan ( GtkButton *button, GtkWidget *window )
{			
	gmp_media_menu_quit ( GTK_WIDGET ( window ) );

	gmp_scan_win ();

	g_debug ( "gmp_media_scan:: widget name %s \n", gtk_widget_get_name ( GTK_WIDGET ( button ) ) );
}

static void gmp_media_convert ( GtkButton *button, GtkWidget *window )
{			
	gmp_media_menu_quit ( GTK_WIDGET ( window ) );

	gmp_convert_win ( FALSE );

	g_debug ( "gmp_media_convert:: widget name %s \n", gtk_widget_get_name ( GTK_WIDGET ( button ) ) );
}

static void gmp_media_clicked_audio_tv ( GtkButton *button )
{
	if ( GST_ELEMENT_CAST ( gmpmedia.dvbsrc_tv )->current_state != GST_STATE_PLAYING ) return;
	
	g_debug ( "gmp_media_clicked_audio_tv \n" );
		
	if ( gmpmedia.set_audio_track > 0 && gmpmedia.count_audio_track-1 == gmpmedia.set_audio_track )
		tv_changed_audio_track ( 0 );
	else
		tv_changed_audio_track ( gmpmedia.set_audio_track + 1 );

	gchar *text = g_strdup_printf ( "%d / %d", gmpmedia.set_audio_track+1, gmpmedia.count_audio_track );

	gtk_button_set_label ( button, text );
	
	g_free  ( text );
}

static GtkButton * gmp_media_audio_button_tv ()
{
	gchar *text = g_strdup_printf ( "%d / %d", gmpmedia.set_audio_track+1, gmpmedia.count_audio_track );
	
	GtkButton *button = (GtkButton *)gtk_button_new_with_label ( text );
	g_signal_connect ( button, "clicked", G_CALLBACK ( gmp_media_clicked_audio_tv ), NULL );
	
	g_free  ( text );

	return button;
}

static void gmp_media_clicked_audio_pl ( GtkButton *button )
{
	if ( GST_ELEMENT_CAST ( gmpmedia.playbin_pl )->current_state != GST_STATE_PLAYING ) return;
	
	g_debug ( "gmp_media_clicked_audio_pl \n" );
	
	gint cur_audio, num_audio;
	g_object_get ( gmpmedia.playbin_pl, "current-audio", &cur_audio, NULL );
	g_object_get ( gmpmedia.playbin_pl, "n-audio",       &num_audio, NULL );	

	if ( cur_audio > 0 && cur_audio+1 == num_audio )
		g_object_set ( gmpmedia.playbin_pl, "current-audio", 0, NULL );
	else
		g_object_set ( gmpmedia.playbin_pl, "current-audio", cur_audio+1, NULL );
		
	g_object_get ( gmpmedia.playbin_pl, "current-audio", &cur_audio,  NULL );

	gchar *text = g_strdup_printf ( "%d / %d", cur_audio+1, num_audio );

	gtk_button_set_label ( button, text );
	
	g_free  ( text );
}

static GtkButton * gmp_media_audio_button_pl ()
{
	gint cur_audio, num_audio;
	g_object_get ( gmpmedia.playbin_pl, "current-audio", &cur_audio, NULL );
	g_object_get ( gmpmedia.playbin_pl, "n-audio",       &num_audio, NULL );
	
	gchar *text = g_strdup_printf ( "%d / %d", cur_audio+1, num_audio );

	GtkButton *button = (GtkButton *)gtk_button_new_with_label ( text );
	g_signal_connect ( button, "clicked", G_CALLBACK ( gmp_media_clicked_audio_pl ), NULL );
	
	g_free  ( text );

	return button;
}

static void gmp_media_clicked_text_pl ( GtkButton *button )
{
	if ( GST_ELEMENT_CAST ( gmpmedia.playbin_pl )->current_state != GST_STATE_PLAYING ) return;
	
	g_debug ( "gmp_media_clicked_text_pl \n" );
	
	gint cur_text, num_text;
	g_object_get ( gmpmedia.playbin_pl, "current-text", &cur_text, NULL );
	g_object_get ( gmpmedia.playbin_pl, "n-text",       &num_text, NULL );

	if ( cur_text > 0 && cur_text+1 == num_text )
		g_object_set ( gmpmedia.playbin_pl, "current-text", 0, NULL );
	else
		g_object_set ( gmpmedia.playbin_pl, "current-text", cur_text+1, NULL );
		
	g_object_get ( gmpmedia.playbin_pl, "current-text", &cur_text,  NULL );

	gchar *text = g_strdup_printf ( "%d / %d", cur_text+1, num_text );

	gtk_button_set_label ( button, text );
	
	g_free  ( text );
}

static GtkButton * gmp_media_text_button_pl ()
{
	gint cur_text, num_text;
	g_object_get ( gmpmedia.playbin_pl, "current-text", &cur_text, NULL );
	g_object_get ( gmpmedia.playbin_pl, "n-text",       &num_text, NULL );		
	
	gchar *text = g_strdup_printf ( "%d / %d", cur_text+1, num_text );

	GtkButton *button = (GtkButton *)gtk_button_new_with_label ( text );
	g_signal_connect ( button, "clicked", G_CALLBACK ( gmp_media_clicked_text_pl ), NULL );
	
	g_free  ( text );

	return button;
}

static void gmp_on_off_subtitle ( GtkButton *button )
{
	if ( GST_ELEMENT_CAST ( gmpmedia.playbin_pl )->current_state != GST_STATE_PLAYING ) return;
	
	g_debug ( "gmp_on_off_subtitle \n" );
	
	gmpmedia.state_subtitle = !gmpmedia.state_subtitle;
	
	GdkPixbuf *logo = gtk_icon_theme_load_icon ( gtk_icon_theme_get_default (),
              ( gmpmedia.state_subtitle ) ? "gmp-set" : "gmp-unset", 16, GTK_ICON_LOOKUP_USE_BUILTIN, NULL );

  	GtkImage *image   = (GtkImage  *)gtk_image_new_from_pixbuf ( logo ); 
	gtk_button_set_image ( button, GTK_WIDGET ( image ) );	
	
    g_object_set ( gmpmedia.playbin_pl, "flags", ( gmpmedia.state_subtitle ) ? 1559 : 1555, NULL );
}

static void gmp_media_menu_ega ( GtkButton *button, GtkWidget *window )
{
	if ( !gmpmedia.media_tv_pl && GST_ELEMENT_CAST ( gmpmedia.playbin_pl )->current_state != GST_STATE_PLAYING ) return;
	
	if ( gmpmedia.media_tv_pl  && GST_ELEMENT_CAST ( gmpmedia.dvbsrc_tv  )->current_state != GST_STATE_PLAYING ) return;
	
	gmp_media_menu_quit ( GTK_WIDGET ( window ) );

	gmp_eqa_win ( gmpmedia.media_tv_pl ? gmpmedia.dvb_all_n[17] : gmpmedia.playequal );

	g_debug ( "gmp_media_menu_ega:: widget name %s \n", gtk_widget_get_name ( GTK_WIDGET ( button ) ) );
}
static void gmp_media_menu_egv ( GtkButton *button, GtkWidget *window )
{
	if ( !gmpmedia.media_tv_pl && GST_ELEMENT_CAST ( gmpmedia.playbin_pl )->current_state != GST_STATE_PLAYING ) return;
	
	if ( gmpmedia.media_tv_pl  && GST_ELEMENT_CAST ( gmpmedia.dvbsrc_tv  )->current_state != GST_STATE_PLAYING ) return;
	
	gmp_media_menu_quit ( GTK_WIDGET ( window ) );

	gmp_eqv_win ( gmpmedia.media_tv_pl ? gmpmedia.dvb_all_n[8] : gmpmedia.videobln );

	g_debug ( "gmp_media_menu_egv:: widget name %s \n", gtk_widget_get_name ( GTK_WIDGET ( button ) ) );
}


static void gmp_media_scroll_win ( GtkButton *button, GtkWidget *vbox )
{
	if ( gtk_widget_get_visible ( GTK_WIDGET ( vbox ) ) )
	{
		gtk_widget_hide ( GTK_WIDGET ( vbox ) );
	}	
	else
	{
		gtk_widget_show ( GTK_WIDGET ( vbox ) );
	}

	g_debug ( "gmp_media_scroll_win:: widget name %s \n", gtk_widget_get_name ( GTK_WIDGET ( button ) ) );
}

static void gmp_media_slider_win ( /*GtkButton *button, GtkWidget *window*/ )
{
	if ( gtk_widget_get_visible ( GTK_WIDGET ( gmpmedia.sliders ) ) )
	{
		gtk_widget_hide ( GTK_WIDGET ( gmpmedia.sliders ) );
		gtk_widget_show ( GTK_WIDGET ( gmpmedia.sliders_m ) );
		gmpmedia.show_slider_menu = TRUE;	
	}	
	else
	{
		gtk_widget_hide ( GTK_WIDGET ( gmpmedia.sliders_m ) );
		gtk_widget_show ( GTK_WIDGET ( gmpmedia.sliders ) );
		gmpmedia.show_slider_menu = FALSE;	
	}

	g_debug ( "gmp_media_slider_win:: visible %s \n", gtk_widget_get_visible ( GTK_WIDGET ( gmpmedia.sliders ) ) ? "TRUE" : "FALSE" );
}

static void gmp_media_buttons ( GtkBox *box, gchar *icon, guint size, void (* activate)(), GtkWidget *widget )
{
	GdkPixbuf *logo = gtk_icon_theme_load_icon ( gtk_icon_theme_get_default (),
              icon, size, GTK_ICON_LOOKUP_USE_BUILTIN, NULL );

  	GtkButton *button = (GtkButton *)gtk_button_new ();
  	GtkImage *image   = (GtkImage  *)gtk_image_new_from_pixbuf ( logo );
 
	gtk_button_set_image ( button, GTK_WIDGET ( image ) );

  	g_signal_connect ( button, "clicked", G_CALLBACK (activate), widget );

  	gtk_box_pack_start ( box, GTK_WIDGET ( button ), TRUE, TRUE, 5 );
}


static GtkBox * gmp_media_pl_slider_menu ()
{
    gmpmedia.slider_menu = (GtkScale *)gtk_scale_new_with_range ( GTK_ORIENTATION_HORIZONTAL, 0, gmpmedia.slider_range_max, 1 );
	gtk_range_set_value ( GTK_RANGE ( gmpmedia.slider_menu ), gtk_range_get_value ( GTK_RANGE ( gmpmedia.slider_base ) ) );
    gtk_scale_set_draw_value ( gmpmedia.slider_menu, 0 );
    gmpmedia.slider_update_signal_id_m = g_signal_connect ( gmpmedia.slider_menu, "value-changed", G_CALLBACK ( gmp_slider_m_changed ), NULL );

    gmpmedia.lab_pos_m = (GtkLabel *)gtk_label_new ( gtk_label_get_text ( gmpmedia.lab_pos ) );
    gmpmedia.lab_dur_m = (GtkLabel *)gtk_label_new ( gtk_label_get_text ( gmpmedia.lab_dur ) );

    gmpmedia.sliders_m = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_HORIZONTAL, 0 );
        gtk_box_pack_start ( gmpmedia.sliders_m, GTK_WIDGET ( gmpmedia.lab_pos_m ),   FALSE, FALSE, 5 );
        gtk_box_pack_start ( gmpmedia.sliders_m, GTK_WIDGET ( gmpmedia.slider_menu ), TRUE,  TRUE,  0 );
        gtk_box_pack_start ( gmpmedia.sliders_m, GTK_WIDGET ( gmpmedia.lab_dur_m ),   FALSE, FALSE, 5 );
    
	return gmpmedia.sliders_m;
}

void gmp_media_menu ( GtkBox *vbox )
{
    GtkWindow *window =      (GtkWindow *)gtk_window_new ( GTK_WINDOW_TOPLEVEL );
    gtk_window_set_transient_for ( window, gmp_base_win_ret () );
    gtk_window_set_modal     ( window, TRUE   );
	gtk_window_set_decorated ( window, FALSE  );
	gtk_window_set_position  ( window, GTK_WIN_POS_CENTER_ON_PARENT );

	GtkVolumeButton *volbutton = (GtkVolumeButton *)gtk_volume_button_new ();
    gtk_scale_button_set_value ( GTK_SCALE_BUTTON ( volbutton ), gmpmedia.media_tv_pl ? gmpmedia.volume_tv : gmpmedia.volume_pl );
    g_signal_connect ( volbutton, "value-changed", G_CALLBACK ( gmp_volume_changed_pl_tv ), NULL );

	gtk_widget_set_sensitive ( GTK_WIDGET ( volbutton ), 
		!gmp_volume_mute_get ( gmpmedia.media_tv_pl ? gmpmedia.dvb_all_n[16] : gmpmedia.playbin_pl ) );

	gint num_audio, num_subtitle;
	g_object_get ( gmpmedia.playbin_pl, "n-audio", &num_audio, NULL );
	g_object_get ( gmpmedia.playbin_pl, "n-text",  &num_subtitle, NULL );	

	
	gchar *base_icon = gmpmedia.media_tv_pl ? "gmp-mp" : "gmp-tv";
	
    GtkBox *m_box  = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_VERTICAL,   0 );

	GtkBox *b_box  = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_HORIZONTAL, 0 );
    GtkBox *l_box  = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_VERTICAL,   0 );
    GtkBox *r_box  = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_VERTICAL,   0 );
    
    GtkBox *h_box  = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_HORIZONTAL, 0 );
	GtkBox *hm_box = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_HORIZONTAL, 0 );

	if ( gmpmedia.media_tv_pl )
	{
		gmp_media_buttons ( h_box, base_icon,    resize_icon, gmp_media_base_window, GTK_WIDGET ( window ) );
		gmp_media_buttons ( h_box, "gmp-editor", resize_icon, gmp_media_scroll_win,  GTK_WIDGET ( vbox )   );
		gmp_media_buttons ( h_box, "gmp-eqav",   resize_icon, gmp_media_menu_ega,    GTK_WIDGET ( window ) );
		gmp_media_buttons ( h_box, "gmp-eqav",   resize_icon, gmp_media_menu_egv,    GTK_WIDGET ( window ) );
		gmp_media_buttons ( h_box, "gmp-muted",  resize_icon, gmp_media_pl_tv_mute,  GTK_WIDGET ( volbutton ) );
	}
	else
	{
		gmp_media_buttons ( h_box, base_icon,    resize_icon, gmp_media_base_window, GTK_WIDGET ( window ) );
		gmp_media_buttons ( h_box, "gmp-editor", resize_icon, gmp_media_scroll_win,  GTK_WIDGET ( vbox )   );	
		gmp_media_buttons ( h_box, "gmp-eqav",   resize_icon, gmp_media_menu_ega,    GTK_WIDGET ( window ) );
		gmp_media_buttons ( h_box, "gmp-eqav",   resize_icon, gmp_media_menu_egv,    GTK_WIDGET ( window ) );	
		gmp_media_buttons ( h_box, "gmp-muted",  resize_icon, gmp_media_pl_tv_mute,  GTK_WIDGET ( volbutton ) );
	}

    gtk_box_pack_start ( l_box, GTK_WIDGET ( h_box ), TRUE, TRUE, 5 );

	if ( gmpmedia.media_tv_pl )
	{
		gmp_media_buttons ( hm_box, "gmp-media-stop",   resize_icon, gmp_media_dvb_stop,   NULL );
		gmp_media_buttons ( hm_box, "gmp-media-record", resize_icon, gmp_media_dvb_record, NULL );
		gmp_media_buttons ( hm_box, "gmp-display",      resize_icon, gmp_media_scan,       GTK_WIDGET ( window ) );
		gmp_media_buttons ( hm_box, "gmp-convert",      resize_icon, gmp_media_convert,    GTK_WIDGET ( window ) );
		gmp_media_buttons ( hm_box, "gmp-exit",         resize_icon, gmp_media_menu_close, GTK_WIDGET ( window ) );
	}
	else
	{
		gmp_media_buttons ( hm_box, "gmp-media-start", resize_icon, gmp_media_playback_start, NULL );
		gmp_media_buttons ( hm_box, "gmp-media-pause", resize_icon, gmp_media_playback_pause, NULL );	
		gmp_media_buttons ( hm_box, "gmp-media-stop",  resize_icon, gmp_media_playback_stop,  NULL );
		gmp_media_buttons ( hm_box, "gmp-slider",      resize_icon, gmp_media_slider_win,     NULL );
		gmp_media_buttons ( hm_box, "gmp-exit",        resize_icon, gmp_media_menu_close,     GTK_WIDGET ( window ) );
	}

	gtk_box_pack_start ( l_box, GTK_WIDGET ( hm_box ), TRUE, TRUE, 5 );
	
	if ( gmpmedia.media_tv_pl )
	{
		if ( gmpmedia.count_audio_track > 1 )
			gtk_box_pack_start ( r_box, GTK_WIDGET ( gmp_media_audio_button_tv () ), TRUE, TRUE, 0 );				
	}
	else
	{
		if ( num_subtitle > 1 )
			gtk_box_pack_start ( r_box, GTK_WIDGET ( gmp_media_text_button_pl () ), TRUE, TRUE, 0 );
			
		if ( num_subtitle > 1 )
			gmp_media_buttons ( r_box, ( gmpmedia.state_subtitle ) ? "gmp-set" : "gmp-unset", 16, gmp_on_off_subtitle, NULL );

		if ( num_audio > 1 )
			gtk_box_pack_start ( r_box, GTK_WIDGET ( gmp_media_audio_button_pl () ), TRUE, TRUE, 0 );		
	}
	
	gtk_box_pack_start ( r_box, GTK_WIDGET ( volbutton ), TRUE, TRUE, 5 );

	gtk_box_pack_start ( b_box, GTK_WIDGET ( l_box ), TRUE,  TRUE,  5 );
	gtk_box_pack_end   ( b_box, GTK_WIDGET ( r_box ), FALSE, FALSE, 5 );
	
	gtk_box_pack_start ( m_box, GTK_WIDGET ( b_box ), TRUE,  TRUE,  5 );

	if ( !gmpmedia.media_tv_pl )
	{
		gtk_box_pack_start ( m_box, GTK_WIDGET ( gmp_media_pl_slider_menu () ), FALSE, FALSE, 0 );	
		
		if ( GST_ELEMENT_CAST ( gmpmedia.playbin_pl )->current_state == GST_STATE_NULL )
			gtk_widget_set_sensitive ( GTK_WIDGET ( gmpmedia.sliders_m ), FALSE );
	}

    gtk_container_set_border_width ( GTK_CONTAINER ( m_box ), 5 );
    gtk_container_add   ( GTK_CONTAINER ( window ), GTK_WIDGET ( m_box ) );
    gtk_widget_show_all ( GTK_WIDGET ( window ) );

	if ( !gmpmedia.media_tv_pl && gtk_widget_get_visible ( GTK_WIDGET ( gmpmedia.sliders ) ) )
	{
		gtk_widget_hide ( GTK_WIDGET ( gmpmedia.sliders_m ) );
		gmpmedia.show_slider_menu = FALSE;
	}
	else
		gmpmedia.show_slider_menu = TRUE;

	gtk_widget_set_opacity ( GTK_WIDGET ( window ), opacity_cnt );
}

// ***** Panel *****

// ***** TreeView *****

static void gmp_media_add_fl_ch ( GtkTreeView *tree_view, gchar *file_ch, gchar *data, gboolean play )
{
    GtkTreeIter iter;
    GtkTreeModel *model = gtk_tree_view_get_model ( GTK_TREE_VIEW ( tree_view ) );
    guint ind = gtk_tree_model_iter_n_children ( model, NULL );

    gtk_list_store_append ( GTK_LIST_STORE ( model ), &iter);
    gtk_list_store_set    ( GTK_LIST_STORE ( model ), &iter,
                            COL_NUM, ind+1,
                            COL_TITLE, file_ch,
                            COL_DATA, data,
                            -1 );

	if ( play && GST_ELEMENT_CAST ( gmpmedia.playbin_pl )->current_state == GST_STATE_NULL )
	{
        gmpmedia.gmp_index_path = gtk_tree_model_get_path ( model, &iter );
        gtk_tree_selection_select_iter ( gtk_tree_view_get_selection ( GTK_TREE_VIEW ( tree_view ) ), &iter );

		gmp_media_stop_set_play ( data );
	}
}


void gmp_str_split_ch_data ( gchar *data )
{
    gchar **lines = g_strsplit ( data, ":", 0 );

        if ( !g_str_has_prefix ( data, "#" ) )
            gmp_media_add_fl_ch ( gmpmedia.treeview_tv, lines[0], data, FALSE );

    g_strfreev ( lines );
}
static void gmp_channels_to_treeview ( const gchar *filename )
{
	g_debug ( "gmp_file_to_treeview \n" );
	
    guint n = 0;
    gchar *contents;
    GError *err = NULL;

    if ( g_file_get_contents ( filename, &contents, 0, &err ) )
    {
        gchar **lines = g_strsplit ( contents, "\n", 0 );

        for ( n = 0; lines[n] != NULL; n++ )
            if ( *lines[n] )
                gmp_str_split_ch_data ( lines[n] );

        g_strfreev ( lines );
        g_free ( contents );
    }
    else
    {
        g_critical ( "gmp_file_to_treeview:: %s\n", err->message );
		g_error_free ( err );
	}
}

static void gmp_treeview_to_file ( GtkTreeView *tree_view, gchar *filename )
{
	g_debug ( "gmp_treeview_to_file \n" );
	
	GString *gstring;

	if ( gmpmedia.media_tv_pl )
		gstring = g_string_new ( "# Gmp-Dvb channel format \n" );
	else
		gstring = g_string_new ( "# EXTM3U \n" );

    GtkTreeIter iter;
    GtkTreeModel *model = gtk_tree_view_get_model ( GTK_TREE_VIEW ( tree_view ) );

    gboolean valid;
    for ( valid = gtk_tree_model_get_iter_first ( model, &iter ); valid;
          valid = gtk_tree_model_iter_next ( model, &iter ) )
    {
        gchar *data = NULL;

            gtk_tree_model_get ( model, &iter, COL_DATA, &data, -1 );

            gstring = g_string_append ( gstring, data );
            gstring = g_string_append ( gstring, "\n" );

        g_free ( data );
    }

    if ( !g_file_set_contents ( filename, gstring->str, -1, NULL ) )
		gmp_message_dialog ( "Save failed.", filename, GTK_MESSAGE_ERROR );

    g_string_free ( gstring, TRUE );
}
static void gmp_media_save ( GtkButton *button, GtkWidget *tree_view )
{
    GtkTreeModel *model = gtk_tree_view_get_model ( GTK_TREE_VIEW ( tree_view ) );
    gint ind = gtk_tree_model_iter_n_children ( model, NULL );

    if ( ind > 0 )
    {
        GtkFileChooserDialog *dialog = ( GtkFileChooserDialog *)gtk_file_chooser_dialog_new (
                    "Save",  NULL, GTK_FILE_CHOOSER_ACTION_SAVE,
                    "gtk-cancel", GTK_RESPONSE_CANCEL,
                    "gtk-save",   GTK_RESPONSE_ACCEPT,
                     NULL );

        gmp_add_filter ( dialog, gmpmedia.media_tv_pl ? "conf" : "m3u", gmpmedia.media_tv_pl ? "*.conf" : "*.m3u" );

		gchar *dir_conf = g_strdup_printf ( "%s/gtv", g_get_user_config_dir () );
			gtk_file_chooser_set_current_folder ( GTK_FILE_CHOOSER ( dialog ), gmpmedia.media_tv_pl ? dir_conf : g_get_home_dir () );
		g_free ( dir_conf );
        
        gtk_file_chooser_set_do_overwrite_confirmation ( GTK_FILE_CHOOSER ( dialog ), TRUE    );
        gtk_file_chooser_set_current_name   ( GTK_FILE_CHOOSER ( dialog ), gmpmedia.media_tv_pl ? "gtv-channel.conf" : "playlist-001.m3u" );

        if ( gtk_dialog_run ( GTK_DIALOG ( dialog ) ) == GTK_RESPONSE_ACCEPT )
        {
            gchar *filename = gtk_file_chooser_get_filename ( GTK_FILE_CHOOSER ( dialog ) );
                gmp_treeview_to_file ( GTK_TREE_VIEW ( tree_view ), filename );
            g_free ( filename );
        }

        gtk_widget_destroy ( GTK_WIDGET ( dialog ) );
    }
	
	g_debug ( "gmp_media_save:: widget name %s \n", gtk_widget_get_name ( GTK_WIDGET ( button ) ) );
}

static void gmp_treeview_reread_mini ( GtkTreeView *tree_view )
{
	g_debug ( "gmp_treeview_reread_mini \n" );
	
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

static void gmp_treeview_up_down ( GtkTreeView *tree_view, gboolean up_dw )
{
	g_debug ( "gmp_treeview_up_down \n" );
	
    GtkTreeIter iter, iter_c;
    GtkTreeModel *model = gtk_tree_view_get_model ( GTK_TREE_VIEW ( tree_view ) );
    gint ind = gtk_tree_model_iter_n_children ( model, NULL );

    if ( ind < 2 ) return;

    if ( gtk_tree_selection_get_selected ( gtk_tree_view_get_selection ( tree_view ), NULL, &iter ) )
    {
        gtk_tree_selection_get_selected ( gtk_tree_view_get_selection ( tree_view ), NULL, &iter_c );

            if ( up_dw )
                if ( gtk_tree_model_iter_previous ( model, &iter ) )
                    gtk_list_store_move_before ( GTK_LIST_STORE ( model ), &iter_c, &iter );

            if ( !up_dw )
                if ( gtk_tree_model_iter_next ( model, &iter ) )
                    gtk_list_store_move_after ( GTK_LIST_STORE ( model ), &iter_c, &iter );

        gmp_treeview_reread_mini ( tree_view );
    }
    else if ( gtk_tree_model_get_iter_first ( model, &iter ) )
    {
        gmpmedia.gmp_index_path = gtk_tree_model_get_path (model, &iter);
        gtk_tree_selection_select_iter ( gtk_tree_view_get_selection (tree_view), &iter);
    }
}

static void gmp_treeview_up_down_s ( GtkTreeView *tree_view, gboolean up_dw )
{
	g_debug ( "gmp_treeview_up_down_s \n" );
	
    GtkTreeIter iter;
    GtkTreeModel *model = gtk_tree_view_get_model ( GTK_TREE_VIEW ( tree_view ) );
    gint ind = gtk_tree_model_iter_n_children ( model, NULL );

    if ( ind < 2 ) return;

    if ( gtk_tree_selection_get_selected ( gtk_tree_view_get_selection ( tree_view ), NULL, &iter ) )
    {
		GtkTreePath *path;
		
            if ( up_dw )
                if ( gtk_tree_model_iter_previous ( model, &iter ) )
                {
                    gtk_tree_selection_select_iter ( gtk_tree_view_get_selection ( GTK_TREE_VIEW ( tree_view ) ), &iter );
					
					path = gtk_tree_model_get_path (model, &iter);
					gtk_tree_view_scroll_to_cell ( tree_view, path, NULL, FALSE, 0, 0 );
				}
				
            if ( !up_dw )
                if ( gtk_tree_model_iter_next ( model, &iter ) )
				{
                    gtk_tree_selection_select_iter ( gtk_tree_view_get_selection ( GTK_TREE_VIEW ( tree_view ) ), &iter );

					path = gtk_tree_model_get_path (model, &iter);
					gtk_tree_view_scroll_to_cell ( tree_view, path, NULL, FALSE, 0, 0 );
				}
    }
    else if ( gtk_tree_model_get_iter_first ( model, &iter ) )
    {
        gmpmedia.gmp_index_path = gtk_tree_model_get_path (model, &iter);
        gtk_tree_selection_select_iter ( gtk_tree_view_get_selection (tree_view), &iter);
    }
}

static void gmp_treeview_remove ( GtkTreeView *tree_view )
{
	gboolean prev_i = FALSE;
    GtkTreeIter iter, iter_prev;
    GtkTreeModel *model = gtk_tree_view_get_model ( GTK_TREE_VIEW ( tree_view ) );

    if ( gtk_tree_selection_get_selected ( gtk_tree_view_get_selection ( tree_view ), NULL, &iter ) )
    {
		gtk_tree_selection_get_selected ( gtk_tree_view_get_selection ( tree_view ), NULL, &iter_prev );
		prev_i = gtk_tree_model_iter_previous ( model, &iter_prev );
		
        gtk_list_store_remove ( GTK_LIST_STORE ( model ), &iter );
        gmp_treeview_reread_mini ( tree_view );
        
        if ( prev_i )
		{
			gtk_tree_selection_select_iter ( gtk_tree_view_get_selection (tree_view), &iter_prev);
		}
		else
		{
			if ( gtk_tree_model_get_iter_first ( model, &iter ) )
				gtk_tree_selection_select_iter ( gtk_tree_view_get_selection (tree_view), &iter);
		}
    }

	g_debug ( "gmp_treeview_remove:: widget name %s \n", gtk_widget_get_name ( GTK_WIDGET ( tree_view ) ) );
}


static void gmp_treeview_clear_click ( GtkButton *button, GtkTreeModel *model )
{
    gtk_list_store_clear ( GTK_LIST_STORE ( model ) );

	g_debug ( "gmp_treeview_clear_click:: widget name %s \n", gtk_widget_get_name ( GTK_WIDGET ( button ) ) );	
}
static void gmp_treeview_close_click ( GtkButton *button, GtkWindow *window )
{
    gmp_media_menu_quit ( GTK_WIDGET ( window ) );

	g_debug ( "gmp_treeview_close_click:: widget name %s \n", gtk_widget_get_name ( GTK_WIDGET ( button ) ) );	
}
static void gmp_treeview_clear ( GtkTreeView *tree_view )
{
    GtkTreeModel *model = gtk_tree_view_get_model ( GTK_TREE_VIEW ( tree_view ) );
    guint ind = gtk_tree_model_iter_n_children ( model, NULL );

    if ( ind == 0 ) return;

    GtkWindow *window =      (GtkWindow *)gtk_window_new ( GTK_WINDOW_TOPLEVEL );
    gtk_window_set_transient_for ( window, gmp_base_win_ret () );
    gtk_window_set_modal     ( window, TRUE );
    gtk_window_set_position  ( window, GTK_WIN_POS_CENTER_ON_PARENT );
    gtk_window_set_title     ( window, "Clear" );
    
    gtk_widget_set_size_request ( GTK_WIDGET ( window ), 400, 150 );


    GtkBox *m_box = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_VERTICAL,   0 );
    GtkBox *i_box = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_HORIZONTAL, 0 );
    GtkBox *h_box = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_HORIZONTAL, 0 );

	GdkPixbuf *logo = gtk_icon_theme_load_icon ( gtk_icon_theme_get_default (),
              "gmp-playlist", 48, GTK_ICON_LOOKUP_USE_BUILTIN, NULL );

  	GtkImage *image   = (GtkImage  *)gtk_image_new_from_pixbuf ( logo );
    gtk_box_pack_start ( i_box, GTK_WIDGET ( image ), TRUE, TRUE, 0 );

	GtkLabel *label = (GtkLabel *)gtk_label_new ( "" );

    gchar *text = g_strdup_printf ( "%d", ind );			
		gtk_label_set_text ( label, text );
    g_free  ( text );
       
    gtk_box_pack_start ( i_box, GTK_WIDGET ( label ), TRUE, TRUE, 10 );

	logo = gtk_icon_theme_load_icon ( gtk_icon_theme_get_default (),
              "gmp-clear", 48, GTK_ICON_LOOKUP_USE_BUILTIN, NULL );

  	image   = (GtkImage  *)gtk_image_new_from_pixbuf ( logo );
    gtk_box_pack_start ( i_box, GTK_WIDGET ( image ), TRUE, TRUE, 0 );
    
    gtk_box_pack_start ( m_box, GTK_WIDGET ( i_box ), TRUE, TRUE, 5 );
    

    GtkButton *button_clear = (GtkButton *)gtk_button_new_from_icon_name ( "gmp-ok", GTK_ICON_SIZE_SMALL_TOOLBAR );
    g_signal_connect ( button_clear, "clicked", G_CALLBACK ( gmp_treeview_clear_click ), model  );
    g_signal_connect ( button_clear, "clicked", G_CALLBACK ( gmp_treeview_close_click ), window );
    gtk_box_pack_end ( h_box, GTK_WIDGET ( button_clear ), TRUE, TRUE, 5 );

    GtkButton *button_close = (GtkButton *)gtk_button_new_from_icon_name ( "gmp-exit", GTK_ICON_SIZE_SMALL_TOOLBAR );
    g_signal_connect ( button_close, "clicked", G_CALLBACK ( gmp_treeview_close_click ), window );
    gtk_box_pack_end ( h_box, GTK_WIDGET ( button_close ), TRUE, TRUE, 5 );

    gtk_box_pack_start ( m_box, GTK_WIDGET ( h_box ), FALSE, FALSE, 5 );
    
    
    gtk_container_set_border_width ( GTK_CONTAINER ( m_box ), 5 );
    gtk_container_add ( GTK_CONTAINER ( window ), GTK_WIDGET ( m_box ) );

    gtk_widget_show_all ( GTK_WIDGET ( window ) );
    
    gtk_widget_set_opacity ( GTK_WIDGET ( window ), opacity_win );
    
	g_debug ( "gmp_treeview_clear:: \n" );
}

static void gmp_media_goup  ( GtkButton *button, GtkWidget *tree_view ) 
{ 
	gmp_treeview_up_down ( GTK_TREE_VIEW ( tree_view ), TRUE  ); 

	g_debug ( "gmp_media_goup:: widget name %s \n", gtk_widget_get_name ( GTK_WIDGET ( button ) ) );
}

static void gmp_media_down_s  ( GtkButton *button, GtkWidget *tree_view ) 
{
	gmp_treeview_up_down_s ( GTK_TREE_VIEW ( tree_view ), FALSE ); 

	g_debug ( "gmp_media_down:: widget name %s \n", gtk_widget_get_name ( GTK_WIDGET ( button ) ) );
}

static void gmp_media_goup_s  ( GtkButton *button, GtkWidget *tree_view ) 
{ 
	gmp_treeview_up_down_s ( GTK_TREE_VIEW ( tree_view ), TRUE  ); 

	g_debug ( "gmp_media_goup:: widget name %s \n", gtk_widget_get_name ( GTK_WIDGET ( button ) ) );
}

static void gmp_media_down  ( GtkButton *button, GtkWidget *tree_view ) 
{
	gmp_treeview_up_down ( GTK_TREE_VIEW ( tree_view ), FALSE ); 

	g_debug ( "gmp_media_down:: widget name %s \n", gtk_widget_get_name ( GTK_WIDGET ( button ) ) );
}

static void gmp_media_clear ( GtkButton *button, GtkWidget *tree_view ) 
{ 
	gmp_treeview_clear   ( GTK_TREE_VIEW ( tree_view ) );

	g_debug ( "gmp_media_clear:: widget name %s \n", gtk_widget_get_name ( GTK_WIDGET ( button ) ) );
}

static void gmp_media_remv  ( GtkButton *button, GtkWidget *tree_view ) 
{ 
	gmp_treeview_remove  ( GTK_TREE_VIEW ( tree_view ) );

	g_debug ( "gmp_media_remv:: widget name %s \n", gtk_widget_get_name ( GTK_WIDGET ( button ) ) );
}

void gmp_media_menu_trw ( GtkTreeView *tree_view )
{
    GtkWindow *window =      (GtkWindow *)gtk_window_new ( GTK_WINDOW_TOPLEVEL );
    gtk_window_set_transient_for ( window, gmp_base_win_ret () );
    gtk_window_set_modal     ( window, TRUE   );
	gtk_window_set_decorated ( window, FALSE  );
	gtk_window_set_position  ( window, GTK_WIN_POS_CENTER_ON_PARENT );
	
    GtkBox *m_box  = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_VERTICAL,   0 );
    GtkBox *h_box  = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_HORIZONTAL, 0 );
    GtkBox *hc_box = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_HORIZONTAL, 0 );

	gmp_media_buttons ( h_box, "gmp-up",     resize_icon, gmp_media_goup,  GTK_WIDGET ( tree_view ) );
	gmp_media_buttons ( h_box, "gmp-down",   resize_icon, gmp_media_down,  GTK_WIDGET ( tree_view ) );	
	gmp_media_buttons ( h_box, "gmp-remove", resize_icon, gmp_media_remv,  GTK_WIDGET ( tree_view ) );
	gmp_media_buttons ( h_box, "gmp-clear",  resize_icon, gmp_media_clear, GTK_WIDGET ( tree_view ) );

	gtk_box_pack_start ( m_box, GTK_WIDGET ( h_box ), TRUE, TRUE, 5 );
	
	gmp_media_buttons ( hc_box, "gmp-up-select",   resize_icon, gmp_media_goup_s,  GTK_WIDGET ( tree_view ) );
	gmp_media_buttons ( hc_box, "gmp-down-select", resize_icon, gmp_media_down_s,  GTK_WIDGET ( tree_view ) );	
	gmp_media_buttons ( hc_box, "gmp-save",        resize_icon, gmp_media_save,       GTK_WIDGET ( tree_view ) );
	gmp_media_buttons ( hc_box, "gmp-exit",        resize_icon, gmp_media_menu_close, GTK_WIDGET ( window )    );

    gtk_box_pack_start ( m_box, GTK_WIDGET ( hc_box ), TRUE, TRUE, 5 );


    gtk_container_set_border_width ( GTK_CONTAINER ( m_box ), 5 );
    gtk_container_add   ( GTK_CONTAINER ( window ), GTK_WIDGET ( m_box ) );
    gtk_widget_show_all ( GTK_WIDGET ( window ) );

	gtk_widget_set_opacity ( GTK_WIDGET ( window ), opacity_cnt );
}



static void gmp_read_m3u_to_treeview ( GtkTreeView *tree_view, gchar *name_file );

gboolean gmp_checked_filter ( gchar *file_name )
{
    gboolean res = FALSE;
    
	GError *error = NULL;
	GFile *file = g_file_new_for_path ( file_name );
	GFileInfo *file_info = g_file_query_info ( file, "standard::*", 0, NULL, &error );

	const char *content_type = g_file_info_get_content_type (file_info);
    
    if ( g_str_has_prefix ( content_type, "audio" ) || g_str_has_prefix ( content_type, "video" ) )
		res =  TRUE;

	g_object_unref ( file_info );
	g_object_unref ( file );

    return res;
}

static gint gmp_sort_func ( gconstpointer a, gconstpointer b )
{
	return g_utf8_collate ( a, b );
}

static void gmp_add_playlist ( GtkTreeView *tree_view, GSList *lists )
{
    gboolean play = TRUE;

    while ( lists != NULL )
    {
        gchar *name_file = g_strdup ( lists->data );

        if ( g_str_has_suffix ( name_file, ".m3u" ) )
            gmp_read_m3u_to_treeview ( tree_view, name_file );
        else
        {
            gchar *name = g_path_get_basename ( name_file );
                gmp_media_add_fl_ch ( tree_view, g_path_get_basename ( name ), name_file, play );
            g_free ( name );
		}
		
        g_free ( name_file );

        lists = lists->next;
        play = FALSE;
    }
}

static void gmp_add_playlist_sort ( GtkTreeView *tree_view, GSList *lists )
{
    gboolean play = TRUE;

    GSList *list_sort = g_slist_sort ( lists, gmp_sort_func );

    while ( list_sort != NULL )
    {
        gchar *name_file = g_strdup ( list_sort->data );

        if ( g_str_has_suffix ( name_file, ".m3u" ) )
            gmp_read_m3u_to_treeview ( tree_view, name_file );
        else
        {
            gchar *name = g_path_get_basename ( name_file );
                gmp_media_add_fl_ch ( tree_view, g_path_get_basename ( name ), name_file, play );
            g_free ( name );
		}

        g_free ( name_file );

        list_sort = list_sort->next;
        play = FALSE;
    }

    g_slist_free ( list_sort );
}

static void gmp_read_m3u_to_treeview ( GtkTreeView *tree_view, gchar *name_file )
{
    gchar  *contents = NULL;
    GError *err      = NULL;

    if ( g_file_get_contents ( name_file, &contents, 0, &err ) )
    {
        GSList *lists = NULL;

        gchar **lines = g_strsplit ( contents, "\n", 0 );

        gint i; for ( i = 0; lines[i] != NULL; i++ )
        //for ( i = 0; lines[i] != NULL && *lines[i]; i++ )
        {
            if ( g_str_has_prefix ( lines[i], "/" ) || g_strrstr ( lines[i], "://" ) )
                lists = g_slist_append (lists, g_strdup ( lines[i] ) );
        }

        g_strfreev ( lines );

        gmp_add_playlist ( tree_view, lists );

        g_slist_free ( lists );
        g_free ( contents );
    }
    else
    {
        g_critical ( "gmp_read_m3u_to_treeview:: file %s | %s\n", name_file, err->message );
        g_error_free ( err );
	}
}

static void gmp_read_dir ( GtkTreeView *tree_view, gchar *dir_path )
{	
    GDir *dir = g_dir_open ( dir_path, 0, NULL );
    const gchar *name = NULL;

    GSList *lists = NULL;

    if (!dir)
        g_printerr ( "opening directory %s failed\n", dir_path );
    else
    {
        while ( ( name = g_dir_read_name ( dir ) ) != NULL )
        {
            gchar *path_name = g_strconcat ( dir_path, "/", name, NULL );

            if ( g_file_test ( path_name, G_FILE_TEST_IS_DIR ) )
                gmp_read_dir ( tree_view, path_name ); // Recursion!

            if ( g_file_test ( path_name, G_FILE_TEST_IS_REGULAR ) )
            {
                if ( gmp_checked_filter ( path_name ) )
                    lists = g_slist_append ( lists, g_strdup ( path_name ) );
            }

            name = NULL;
            g_free ( path_name );
        }

        g_dir_close ( dir );

        gmp_add_playlist_sort ( tree_view, lists );

        g_slist_free ( lists );
        name = NULL;
    }
}

static void gmp_add_file_name ( GtkTreeView *tree_view, gchar *file_name )
{
	g_debug ( "gmp_add_file_name \n" );
	
    if ( g_file_test ( file_name, G_FILE_TEST_IS_DIR ) )
    {
        gmp_read_dir ( tree_view, file_name );
        return;
    }

	if ( !gmp_checked_filter ( file_name ) ) return;

    if ( g_str_has_suffix ( file_name, ".m3u" ) )
        gmp_read_m3u_to_treeview ( tree_view, file_name );
    else
    {
        GSList *lists = NULL;
        lists = g_slist_append ( lists, g_strdup ( file_name ) );
            gmp_add_playlist_sort ( tree_view, lists );
        g_slist_free ( lists );
    }
}


static void gmp_media_drag_in ( GtkDrawingArea *widget, GdkDragContext *context, gint x, gint y, GtkSelectionData *selection_data, guint info, guint32 time, GtkTreeView *tree_view )
{
	const gchar *title = gtk_tree_view_column_get_title ( gtk_tree_view_get_column ( tree_view, 1 ) );

	gchar **uris  = gtk_selection_data_get_uris ( selection_data );

	guint i = 0; 
	for ( i = 0; uris[i] != NULL; i++ )
    {
        GFile *file = g_file_new_for_uri ( uris[i] );
        gchar *file_name = g_file_get_path ( file );
        
        g_debug ( "title: %s | file: %s \n", title, file_name );	

		if ( g_str_has_prefix ( title, "Channels" ) )
		{
			if ( g_str_has_suffix ( file_name, ".conf" ) )
			{
				if ( g_strrstr ( file_name, "gtv-channel" ) )
				{
					gmp_channels_to_treeview ( ch_conf );
				}
				else
				{
					gmp_drag_cvr = g_strdup ( file_name );			
					gmp_convert_win ( TRUE );
				}
			}
		}
		else
		{
			gmp_add_file_name ( tree_view, file_name );
		}
		
        g_free ( file_name );
        g_object_unref ( file );		
    }

	g_strfreev ( uris );

	gtk_drag_finish ( context, TRUE, FALSE, time );

	g_debug ( "gmp_media_drag_in:: widget name %s | x %d | y %d | i %d \n", gtk_widget_get_name ( GTK_WIDGET ( widget ) ), x, y, info );
}


static void gmp_tree_view_row_activated ( GtkTreeView *tree_view, GtkTreePath *path, GtkTreeViewColumn *column )
{
    GtkTreeIter iter;
    GtkTreeModel *model = gtk_tree_view_get_model ( GTK_TREE_VIEW ( tree_view ) );

    if ( gtk_tree_model_get_iter ( model, &iter, path ) )
    {
        gchar *file_ch = NULL;
            gtk_tree_model_get ( model, &iter, COL_DATA, &file_ch, -1 );
            
			if ( g_str_has_prefix ( gtk_tree_view_column_get_title ( column ), "Files" ) )
			{
				gmp_media_stop_set_play ( file_ch );
				gmpmedia.gmp_index_path = gtk_tree_model_get_path ( model, &iter );
			}
			else
				gmp_media_channel_play  ( file_ch );
			
        g_free ( file_ch );
    }
}

static void gmp_create_columns ( GtkTreeView *tree_view, GtkTreeViewColumn *column, GtkCellRenderer *renderer, const gchar *name, gint column_id, gboolean col_vis )
{
    column = gtk_tree_view_column_new_with_attributes ( name, renderer, "text", column_id, NULL );
    gtk_tree_view_append_column ( tree_view, column );
    gtk_tree_view_column_set_visible ( column, col_vis );
}
static void gmp_add_columns ( GtkTreeView *tree_view, struct trw_columns sw_col_n[], guint num )
{
    GtkTreeViewColumn *column_n[num];
    GtkCellRenderer *renderer = gtk_cell_renderer_text_new ();

	guint c = 0;
    for ( c = 0; c < num; c++ )
        gmp_create_columns ( tree_view, column_n[c], renderer, sw_col_n[c].title, c, sw_col_n[c].visible );
}

static GtkScrolledWindow * gmp_treeview ( GtkTreeView *tree_view, struct trw_columns sw_col_n[], guint num )
{
    GtkScrolledWindow *scroll_win = (GtkScrolledWindow *)gtk_scrolled_window_new ( NULL, NULL );
    gtk_scrolled_window_set_policy ( scroll_win, GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC );
    gtk_widget_set_size_request ( GTK_WIDGET ( scroll_win ), 200, -1 );

    gtk_tree_view_set_search_column ( GTK_TREE_VIEW ( tree_view ), COL_TITLE );
    gmp_add_columns ( tree_view, sw_col_n, num );

    g_signal_connect ( tree_view, "row-activated", G_CALLBACK ( gmp_tree_view_row_activated ), NULL );
    gtk_container_add ( GTK_CONTAINER ( scroll_win ), GTK_WIDGET ( tree_view ) );

    return scroll_win;
}

// ***** TreeView *****


// ***** GdkEventButton *****

static gboolean gmp_media_press_event_trw ( GtkTreeView *tree_view, GdkEventButton *event )
{
	g_debug ( "gmp_media_press_event_trw:: \n" );
	
    if ( event->button == 3 )
    {
		gmp_media_menu_trw ( tree_view );

        return TRUE;
    }

    return FALSE;
}

static gboolean gmp_media_press_event ( GtkDrawingArea *widget, GdkEventButton *event, GtkBox *vbox )
{
	g_debug ( "gmp_media_press_event:: widget name %s \n", gtk_widget_get_name ( GTK_WIDGET ( widget ) ) );

    if ( event->button == 1 && event->type == GDK_2BUTTON_PRESS )
    {
        if ( gmp_base_flscr () ) gtk_widget_hide ( GTK_WIDGET ( vbox ) );

        return TRUE;
    }

    if ( event->button == 3 )
    {
		gmp_media_menu ( vbox );

        return TRUE;
    }

    return FALSE;
}

static gboolean gmp_media_scroll_event ( GtkDrawingArea *widget, GdkEventScroll *evscroll )
{
	g_debug ( "gmp_media_scroll_event:: widget name %s \n", gtk_widget_get_name ( GTK_WIDGET ( widget ) ) );

    if ( GST_ELEMENT_CAST(gmpmedia.playbin_pl)->current_state != GST_STATE_NULL )
    {
        gint64 skip = GST_SECOND * 20, end = GST_SECOND * 1, pos = 0, dur = 0, new_pos = 0;

        if ( !gst_element_query_position ( gmpmedia.playbin_pl, GST_FORMAT_TIME, &pos ) ) return TRUE;

        gst_element_query_duration ( gmpmedia.playbin_pl, GST_FORMAT_TIME, &dur );
		if ( dur == -1 ) return TRUE;

        if ( evscroll->direction == GDK_SCROLL_DOWN ) new_pos = (pos > skip) ? (pos - skip) : 0;

        if ( evscroll->direction == GDK_SCROLL_UP   ) new_pos = (dur > (pos + skip)) ? (pos + skip) : dur-end;

        if ( evscroll->direction == GDK_SCROLL_DOWN || evscroll->direction == GDK_SCROLL_UP )
        {
            if ( gst_element_seek_simple ( gmpmedia.playbin_pl, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH, new_pos ) )
            	g_usleep ( 1000 );

            return TRUE;
        }
    }

    return FALSE;
}

// ***** GdkEventButton *****


static void gmp_media_video_window_realize ( GtkDrawingArea *draw, gchar *text )
{
    gulong xid = GDK_WINDOW_XID ( gtk_widget_get_window ( GTK_WIDGET ( draw ) ) );

	g_print ( "%s xid: %ld \n", text, xid );	

	if ( g_str_has_prefix ( text, "tv" ) )
		gmpmedia.win_handle_tv = xid;
	else
		gmpmedia.win_handle_pl = xid;
}

static void gmp_media_draw_black ( GtkDrawingArea *widget, cairo_t *cr, GdkPixbuf *logo )
{
    GdkRGBA color; color.red = 0; color.green = 0; color.blue = 0; color.alpha = 1.0;

    gint width  = gtk_widget_get_allocated_width  ( GTK_WIDGET ( widget ) );
    gint height = gtk_widget_get_allocated_height ( GTK_WIDGET ( widget ) );

    gint widthl  = gdk_pixbuf_get_width  ( logo );
    gint heightl = gdk_pixbuf_get_height ( logo );

    cairo_rectangle ( cr, 0, 0, width, height );
    gdk_cairo_set_source_rgba ( cr, &color );
    cairo_fill (cr);

    if ( logo != NULL )
    {
        cairo_rectangle ( cr, 0, 0, width, height );
        gdk_cairo_set_source_pixbuf ( cr, logo,
            ( width / 2  ) - ( widthl  / 2 ),
            ( height / 2 ) - ( heightl / 2 ) );

        cairo_fill (cr);
    }
}

static gboolean gmp_media_draw_callback ( GtkDrawingArea *widget, cairo_t *cr, GdkPixbuf *logo )
{
	if ( gmpmedia.media_tv_pl )
	{
    	if (  GST_ELEMENT_CAST ( gmpmedia.dvbsrc_tv )->current_state == GST_STATE_NULL  )
        	{ gmp_media_draw_black ( widget, cr, logo ); return TRUE; }

	    if (  GST_ELEMENT_CAST ( gmpmedia.dvbsrc_tv )->current_state != GST_STATE_NULL  )
    	    if ( !gmpmedia.video_enable )
    	        { gmp_media_draw_black ( widget, cr, logo ); return TRUE; }
	}
	
	if ( !gmpmedia.media_tv_pl )
	{
    	if ( GST_ELEMENT_CAST ( gmpmedia.playbin_pl )->current_state == GST_STATE_NULL )
        	{ gmp_media_draw_black ( widget, cr, logo ); return TRUE; }

	    gint n_video = 0;
    	g_object_get ( gmpmedia.playbin_pl, "n-video", &n_video, NULL );

    	if ( n_video == 0 )
    	{
    	    if ( GST_ELEMENT_CAST ( gmpmedia.playbin_pl )->current_state == GST_STATE_PLAYING ||
    	         GST_ELEMENT_CAST ( gmpmedia.playbin_pl )->current_state == GST_STATE_PAUSED     )
    	        { gmp_media_draw_black ( widget, cr, logo ); return TRUE; }
    	}
	}

    return FALSE;
}


void gmp_set_sgn_snr ( GstElement *element, GtkLabel *label, GtkProgressBar *barsgn, GtkProgressBar *barsnr, gdouble sgl, gdouble snr, gboolean hlook )
{	
    gtk_progress_bar_set_fraction ( barsgn, sgl/100 );
    gtk_progress_bar_set_fraction ( barsnr, snr/100 );

    gchar *texta = g_strdup_printf ( "Level %d%s", (int)sgl, "%" );
    gchar *textb = g_strdup_printf ( "Snr %d%s",   (int)snr, "%" );

    const gchar *format = NULL, *rec = " ● ";

	if ( gmpmedia.rec_trig_vis ) rec = " ◌ ";

    gboolean play = TRUE;
    if ( GST_ELEMENT_CAST ( element )->current_state != GST_STATE_PLAYING ) play = FALSE;

        if ( hlook )
            format = "<span>\%s</span><span foreground=\"#00ff00\"> ◉ </span><span>\%s</span><span foreground=\"#ff0000\">  \%s</span>";
        else
            format = "<span>\%s</span><span foreground=\"#ff0000\"> ◉ </span><span>\%s</span><span foreground=\"#ff0000\">  \%s</span>";

        if ( !play )
            format = "<span>\%s</span><span foreground=\"#ff8000\"> ◉ </span><span>\%s</span><span foreground=\"#ff0000\">  \%s</span>";

        gchar *markup = g_markup_printf_escaped ( format, texta, textb, gmpmedia.rec_ses ? rec : "" );
            gtk_label_set_markup ( label, markup );
        g_free ( markup );

    g_free ( texta );
    g_free ( textb );

	gmpmedia.rec_trig_vis = !gmpmedia.rec_trig_vis;
}

static GtkBox * gmp_create_sgn_snr ()
{	
    GtkBox *tbar_dvb = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_VERTICAL, 0 );
    gtk_widget_set_margin_start ( GTK_WIDGET ( tbar_dvb ), 5 );
    gtk_widget_set_margin_end   ( GTK_WIDGET ( tbar_dvb ), 5 );

    gmpmedia.signal_snr = (GtkLabel *)gtk_label_new ( "Level  &  Quality" );
    gtk_box_pack_start ( tbar_dvb, GTK_WIDGET ( gmpmedia.signal_snr  ), FALSE, FALSE, 5 );

    gmpmedia.bar_sgn = (GtkProgressBar *)gtk_progress_bar_new ();
    gmpmedia.bar_snr = (GtkProgressBar *)gtk_progress_bar_new ();
    
    gtk_box_pack_start ( tbar_dvb, GTK_WIDGET ( gmpmedia.bar_sgn  ), FALSE, FALSE, 0 );
    gtk_box_pack_start ( tbar_dvb, GTK_WIDGET ( gmpmedia.bar_snr  ), FALSE, FALSE, 3 );

    return tbar_dvb;
}

static GtkBox * gmp_media_pl_slider ()
{	
    gmpmedia.slider_base = (GtkScale *)gtk_scale_new_with_range ( GTK_ORIENTATION_HORIZONTAL, 0, 100, 1 );
    gtk_scale_set_draw_value ( gmpmedia.slider_base, 0 );
    gmpmedia.slider_update_signal_id = g_signal_connect ( gmpmedia.slider_base, "value-changed", G_CALLBACK ( gmp_slider_changed ), NULL );

    gmpmedia.slider_vol = (GtkScale *)gtk_scale_new_with_range ( GTK_ORIENTATION_HORIZONTAL, 0, 1, 1 );
    gtk_scale_set_draw_value ( gmpmedia.slider_vol, 0 );
	gtk_range_set_value ( GTK_RANGE ( gmpmedia.slider_vol ), gmpmedia. volume_pl );
    g_signal_connect ( gmpmedia.slider_vol, "value-changed", G_CALLBACK ( gmp_slider_changed_vol ), NULL );

    gmpmedia.lab_pos = (GtkLabel *)gtk_label_new ( "0:00:00" );
    gmpmedia.lab_dur = (GtkLabel *)gtk_label_new ( "0:00:00" );

    gmpmedia.sliders = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_HORIZONTAL, 0 );
        gtk_box_pack_start ( gmpmedia.sliders, GTK_WIDGET ( gmpmedia.lab_pos ),     FALSE, FALSE, 5 );
        gtk_box_pack_start ( gmpmedia.sliders, GTK_WIDGET ( gmpmedia.slider_base ), TRUE,  TRUE,  0 );
        gtk_box_pack_start ( gmpmedia.sliders, GTK_WIDGET ( gmpmedia.lab_dur ),     FALSE, FALSE, 5 );
		gtk_box_pack_start ( gmpmedia.sliders, GTK_WIDGET ( gmpmedia.slider_vol  ), FALSE, FALSE, 0 );
    
	gtk_widget_set_size_request ( GTK_WIDGET ( gmpmedia.slider_base ), 300, -1 );
	gtk_widget_set_size_request ( GTK_WIDGET ( gmpmedia.slider_vol  ), 100, -1 );

	return gmpmedia.sliders;
}

static void gmp_media_hide_box ( GtkButton *button, GtkBox *box )
{
	gtk_widget_hide ( GTK_WIDGET ( box ) );

	g_debug ( "gmp_media_hide_box:: widget name %s \n", gtk_widget_get_name ( GTK_WIDGET ( button ) ) );
}

static gboolean gmp_media_update_data ( GtkBox *box )
{
	g_debug ( "gmp_media_update_data \n" );
	
	gtk_widget_hide ( GTK_WIDGET ( box ) );
	gtk_widget_hide ( GTK_WIDGET ( gmpmedia.sliders ) );

	gmp_set_sensitive ( FALSE, FALSE );

	return FALSE;
}
static gboolean gmp_media_update_arg ( GtkTreeView *tree_view )
{
	if ( gmp_arg_one ) 
	{
		g_debug ( "gmp_media_update_arg:: gmp_arg_one: %s \n", gmp_arg_one );
		
		gmp_add_file_name ( tree_view, gmp_arg_one );
		g_free ( gmp_arg_one );
	}

	return FALSE;
}

static void gmp_media_init ()
{
	gmpmedia.volume_tv = 0.5;
	gmpmedia.volume_pl = 0.5;
	gmpmedia.slider_range_max = 100;
	gmpmedia.win_handle_tv = 0;
	gmpmedia.win_handle_pl = 0;
	gmpmedia.firstfile = FALSE;
	gmpmedia.rec_status = FALSE;
	gmpmedia.rec_ses = FALSE;
	gmpmedia.rec_trig_vis = TRUE;
	gmpmedia.ch_name = NULL;
	gmpmedia.all_info = FALSE;
	gmpmedia.state_subtitle = TRUE;
}

void gmp_media_win ( GtkBox *box, GdkPixbuf *logo, gboolean set_tv_pl, struct trw_columns sw_col_n[], guint num )
{
	gmp_media_init ();
	
	gchar *text = "tv";
	if ( !set_tv_pl ) text = "pl";

	if ( set_tv_pl )
		gmp_media_gst_create_tv ();
	else
		gmp_media_gst_create_pl ();

	GtkBox *sw_vbox = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_VERTICAL, 0 );

    GtkDrawingArea *video_window = (GtkDrawingArea *)gtk_drawing_area_new ();
    g_signal_connect ( video_window, "realize", G_CALLBACK ( gmp_media_video_window_realize ), text );
    g_signal_connect ( video_window, "draw",    G_CALLBACK ( gmp_media_draw_callback ), logo );

    gtk_widget_add_events ( GTK_WIDGET ( video_window ), GDK_BUTTON_PRESS_MASK | GDK_SCROLL_MASK  );
    g_signal_connect ( video_window, "button-press-event", G_CALLBACK ( gmp_media_press_event  ), sw_vbox );
	g_signal_connect ( video_window, "scroll-event",       G_CALLBACK ( gmp_media_scroll_event ), NULL );


    GtkListStore *liststore   = (GtkListStore *)gtk_list_store_new ( 3, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING );
    GtkTreeView *tree_view = (GtkTreeView *)gtk_tree_view_new_with_model ( GTK_TREE_MODEL ( liststore ) );
	g_signal_connect ( tree_view, "button-press-event", G_CALLBACK ( gmp_media_press_event_trw  ), NULL );

    GtkScrolledWindow *scroll_win = gmp_treeview ( tree_view, sw_col_n, num );
	gtk_box_pack_start ( sw_vbox, GTK_WIDGET ( scroll_win ), TRUE,  TRUE,  0 );


	GtkBox *hb_box   = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_HORIZONTAL, 0 );
	gmp_media_buttons ( hb_box, "gmp-exit", 16, gmp_media_hide_box,  GTK_WIDGET ( sw_vbox ) );
  	gtk_box_pack_end ( sw_vbox, GTK_WIDGET ( hb_box ), FALSE, FALSE, 5 );


    GtkPaned *hpaned = (GtkPaned *)gtk_paned_new ( GTK_ORIENTATION_HORIZONTAL );
    gtk_paned_add1 ( hpaned, GTK_WIDGET ( sw_vbox )      );
    gtk_paned_add2 ( hpaned, GTK_WIDGET ( video_window ) );


	GtkBox *main_hbox   = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_HORIZONTAL, 0 );
    
	gtk_box_pack_start ( main_hbox, GTK_WIDGET ( hpaned ),    TRUE,  TRUE,  0 );
	gtk_box_pack_start ( box,       GTK_WIDGET ( main_hbox ), TRUE,  TRUE,  0 );
	
	if ( set_tv_pl )
	{
		gtk_box_pack_start ( sw_vbox, GTK_WIDGET ( gmp_create_sgn_snr () ), FALSE, FALSE, 0 );
	}
	else
	{
		gtk_box_pack_start ( box, GTK_WIDGET ( gmp_media_pl_slider () ), FALSE,  FALSE,  0 );
	}

    gtk_drag_dest_set ( GTK_WIDGET ( video_window ), GTK_DEST_DEFAULT_ALL, NULL, 0, GDK_ACTION_COPY );
    gtk_drag_dest_add_uri_targets ( GTK_WIDGET ( video_window ) );
    g_signal_connect  ( video_window, "drag-data-received", G_CALLBACK ( gmp_media_drag_in ), tree_view );
	
	if ( set_tv_pl )
	{
		gmpmedia.treeview_tv = tree_view;
		
		gmp_get_dvb_info ( FALSE, 0, 0 );
		
		if ( g_file_test ( ch_conf, G_FILE_TEST_EXISTS ) )
			gmp_channels_to_treeview ( ch_conf );
	}	
	else
	{
		gmpmedia.treeview_pl = tree_view;
		
		if ( gmp_arg_one )
			g_timeout_add ( 250, (GSourceFunc)gmp_media_update_arg,  tree_view );
		else
			g_timeout_add ( 250, (GSourceFunc)gmp_media_update_data, sw_vbox   );
	}
	
	g_debug ( "gmp_media_win \n" );
}

void gmp_media_set_tv ()
{
	gmpmedia.media_tv_pl = TRUE;

	g_debug ( "gmp_media_set_tv:: gmpmedia.media_tv_pl = TRUE" );
}

void gmp_media_set_mp ()
{
	gmpmedia.media_tv_pl = FALSE;

	g_debug ( "gmp_media_set_mp:: gmpmedia.media_tv_pl = FALSE" );
}
