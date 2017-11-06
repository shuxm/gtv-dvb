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
void tv_win_scan ();
void tv_set_lnb ( GstElement *element, gint num_lnb );


#endif // GTV_SCAN_H
