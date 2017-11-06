#!/bin/sh

###  Gtk TV  ###
###  sh compile.sh

NAME="gtv"

gcc -Wall \
	src/*.c \
	-o $NAME \
	`pkg-config gtk+-3.0 --cflags --libs` \
	`pkg-config gstreamer-video-1.0 --cflags --libs` \
	`pkg-config gstreamer-mpegts-1.0 --libs`

strip $NAME
