Gtv-Dvb
-------
* Digital TV player
* DVB-T/T2, DVB-S/S2, DVB-C
* Version 1.0 stable


Requirements
------------
* Graphical user interface - [Gtk+3](https://developer.gnome.org/gtk3)
* Audio & Video & Digital TV - [Gstreamer 1.0](https://gstreamer.freedesktop.org)

* [GNU Lesser General Public License](http://www.gnu.org/licenses/lgpl.html)


Depends
-------
* gcc
* gtk+3
* gstreamer
* gst-plugins
  * base, good, ugly, bad
* gst-libav


Makefile
--------
* make [target]:
  * help
  * info
  * ...


Channels
--------
* Scan channels manually ( Ctrl + U )
* Convert - dvb_channel.conf ( format [DVBv5](https://www.linuxtv.org/docs/libdvbv5/index.html) ) 
  * dvb_channel.conf - created by command: [dvbv5-scan](https://www.linuxtv.org/downloads/v4l-utils) [OPTION...] [initial file](https://www.linuxtv.org/downloads/dtv-scan-tables)


Design
------
* Icon-theme [Faenza-lite](https://github.com/vl-nix/Faenza-lite)
* Theme [Adwaita-dark](https://github.com/GNOME/gnome-themes-standard)
