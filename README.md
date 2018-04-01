[Gtv-Dvb](https://github.com/vl-nix/gtv-dvb)
-------

* Digital TV player
* DVB-T2/S2/C, ATSC, DTMB
* Stable Version [1.1.9 classic](https://github.com/vl-nix/gtv-dvb/releases/tag/1.1.9) ( long term support )


Preview [1.1.9 classic](https://github.com/vl-nix/gtv-dvb/releases/tag/1.1.9)
------------

![alt text](https://static.wixstatic.com/media/650ea5_8d15ce1cb90e4b17a3d452abd0eb28bd~mv2.png)


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

RPM
* libgtk+3 gstreamer1.0
* gst-plugins-base1.0 gst-plugins-good1.0
* gst-plugins-bad1.0 gst-plugins-ugly1.0 gst-libav

DEB
* libgtk-3-0 gstreamer1.0-x
* gstreamer1.0-plugins-base gstreamer1.0-plugins-good
* gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-libav


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

* Dark Theme
