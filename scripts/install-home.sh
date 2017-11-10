#!/bin/sh

###  GTV  ###
###  sh install-home.sh

NAME="gtv"

if [ ! -f $NAME ]
then
	echo "ERROR: INSTALL DROP ( $NAME not found; sh compile.sh? )"
	exit 1
fi

mkdir -p $HOME/.local/bin
cp $NAME $HOME/.local/bin

mkdir -p $HOME/.local/share/applications

echo "[Desktop Entry]
Name=$NAME
Comment=Media TV
Type=Application
TryExec=$HOME/.local/bin/$NAME
Exec=$HOME/.local/bin/$NAME %U
Icon=display
Terminal=false
Categories=GTK;AudioVideo" > $HOME/.local/share/applications/$NAME.desktop
