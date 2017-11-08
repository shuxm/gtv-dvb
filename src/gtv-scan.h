/*
 * Copyright 2014 - 2017 Stepan Perun
 * This program is free software.
 * License: GNU LESSER GENERAL PUBLIC LICENSE
 * http://www.gnu.org/licenses/lgpl.html
*/

#ifndef GTV_SCAN_H
#define GTV_SCAN_H

#include "gtv.h"


void dvb_mpegts_initialize ();

const gchar * enum_name ( GType instance_type, gint val );

void tv_set_lnb ( GstElement *element, gint num_lnb );

void tv_win_scan ();


#endif // GTV_SCAN_H
