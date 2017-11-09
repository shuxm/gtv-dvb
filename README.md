# GTV 1.0

	Digital TV player
	Digital TV ( DVB-T/T2, DVB-S/S2, DVB-C )



![alt text](screenshots.png "Preview")



## Requirements:
	Graphical interfaces - Gtk+3
	Audio & Video & Digital TV - Gstreamer 1.0


## Depends:
	gtk+3, gstreamer, gst-plugins-base, gst-plugins-good, gst-plugins-ugly, gst-plugins-bad, gst-libav

## Compilation:
	sh compile.sh
  
## Install ( home ):
  	sh install-home.sh

## Unistall ( home ):
	sh unistall-home.sh


## Channels:
	1. Scan channels manually ( Ctrl + U ).
	2. Convert - dvb_channel.conf ( format DVBv5 ).
	
	dvb_channel.conf - created by command: dvbv5-scan [OPTION...] <initial file>

	dvbv5-scan - https://www.linuxtv.org/downloads/v4l-utils/
	initial file - https://www.linuxtv.org/downloads/dtv-scan-tables/


