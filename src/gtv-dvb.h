/*
 * Copyright 2014 - 2018 Stepan Perun
 * This program is free software.
 * License: Gnu Lesser General Public License
 * http://www.gnu.org/licenses/lgpl.html
*/

#ifndef GTV_DVB_H
#define GTV_DVB_H


#include <gtk/gtk.h>
#include <gst/gst.h>


struct GtvDvb
{
	guint adapter;
	guint frontend;
	const gchar *lv_snr;
	
	GdkPixbuf *gtv_logo;
	GtkLabel *signal_snr;
	GtkProgressBar *bar_sgn, *bar_snr;	

	gboolean video_enable;
	gboolean rec_status, rec_en_ts;
	gboolean first_msg;
	
	guint sid;
	guint audio_ind, count_audio;
	guint size_ind_m, size_ind_p;
};

struct GtvDvb gtvdvb;


void gtv_stop_play ( gchar *data );
void gtv_str_split_ch_data ( gchar *data );
void gtv_set_combo_lang ( gchar *text_lang, guint apid );

void gtv_combo_size_add ( GtkComboBoxText *combo_box, guint num );
void gtv_combo_size_m   ( GtkComboBox *combo_box );
void gtv_combo_size_p   ( GtkComboBox *combo_box );

gboolean gtv_rec_schedule  ();
void gtv_rec_stop_schedule ();

int gtv_base   ();
void gtv_rec   ();
void gtv_stop  ();
void gtv_audio ();
void gtv_video ();
void gtv_plist ();
void gtv_scan  ();
void gtv_goup  ();
void gtv_down  ();
void gtv_remv  ();
void gtv_clear ();
void gtv_mute  ();
void gtv_flscr ();
void gtv_mini  ();
void gtv_pref  ();
void gtv_about ();
void gtv_quit  ();


#endif // GTV_DVB_H
