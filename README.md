[Gtv-Dvb](https://github.com/vl-nix/gtv-dvb)
-------

* Digital TV player
* DVB-T2/S2/C, ATSC, DTMB
* Version 1.1 stable


Preview
------------

![alt text](https://static.wixstatic.com/media/650ea5_8d15ce1cb90e4b17a3d452abd0eb28bd~mv2.png/v1/fill/w_759,h_416,al_c,usm_0.66_1.00_0.01/650ea5_8d15ce1cb90e4b17a3d452abd0eb28bd~mv2.png "Preview")


Requirements
------------

* Graphical user interface - [Gtk+3](https://developer.gnome.org/gtk3)
* Audio & Video & Digital TV - [Gstreamer 1.0](https://gstreamer.freedesktop.org)
* [GNU Lesser General Public License](http://www.gnu.org/licenses/lgpl.html)


Depends
-------

* gcc
* make
* gettext
* libgtk 3 ( & dev )
* gstreamer 1.0 ( & dev )
* gst-plugins 1.0 ( & dev )
  * base, good, ugly, bad ( & dev )
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

* Icon-theme: [Faenza-lite](https://github.com/vl-nix/Faenza-lite)
* Theme: [Obsidian 2](https://github.com/madmaxms/theme-obsidian-2);  [Cloak-3.22](https://github.com/killhellokitty/Cloak-3.22);  [Adwaita-dark](https://github.com/GNOME/gnome-themes-standard)
