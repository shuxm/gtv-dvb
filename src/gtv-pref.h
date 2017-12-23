/*
 * Copyright 2014 - 2018 Stepan Perun
 * This program is free software.
 * License: Gnu Lesser General Public License
 * http://www.gnu.org/licenses/lgpl.html
*/


#ifndef GTV_PREF_H
#define GTV_PREF_H


#include <gtk/gtk.h>


struct GtvPrefSchedule
{
	gchar *sched;
	guint offset;
	guint duration;
	guint ch_number;
	
	gboolean sched_rec;
};

struct GtvPref
{
	gchar *audio_encoder, *audio_enc_p, 
	      *video_encoder, *video_enc_p, 
	      *muxer, *file_ext, *rec_dir;
	
	struct GtvPrefSchedule gtvpshed;
};

struct GtvPref gtvpref;

void gtv_win_pref ();


#endif // GTV_PREF_H
