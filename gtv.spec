Name: gtv
Version: 1.0
Release: alt1

Summary: DVB-T/T2/S/S2/C player

License: LGPLv2
Group: Video
Url:https://github.com/vl-nix/gtv

Source: %name-%version.tar
BuildRequires: pkgconfig(gtk+-3.0) pkgconfig(gstreamer-1.0) pkgconfig(gstreamer-plugins-base-1.0) pkgconfig(gstreamer-plugins-bad-1.0) 
Requires: v4l-utils

%description
DVB-T/T2/S/S2/C player

%prep
%setup

%build
gcc -Wall gtv.c -o gtv `pkg-config gtk+-3.0 --cflags --libs` `pkg-config gstreamer-video-1.0 --cflags --libs`

%install
mkdir -p %buildroot%_bindir
install -m755 gtv %buildroot%_bindir/gtv

%files
%_bindir/gtv
%doc Readme

%changelog
