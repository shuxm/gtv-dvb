#############################################################################
#
# Makefile for Gtv-dvb Modern Ver. 2.0
# Get Gtv-dvb: https://github.com/vl-nix/gtv-dvb
#
# Depends
# -------
# gcc
# make
# gettext
# libgtk 3	  ( & dev )
# gstreamer 1.0	  ( & dev )
# gst-plugins 1.0 ( & dev )
# 	base, good, ugly, bad
# gst-libav
#
#===========================================================================
#
# Set prefix = PREFIX ( install files in PREFIX )
# Verify variables: make info
#
#===========================================================================
# /usr:       /usr, /usr/bin ...
# prefix    = /usr

# /usr/local: /usr/local, /usr/local/bin ...
# prefix    = /usr/local

# Home:       $(HOME)/.local, $(HOME)/.local/bin ...
prefix      = $(HOME)/.local

program     = gtv-dvb
version     = 2.0
bindir      = $(prefix)/bin
datadir     = $(prefix)/share
desktopdir  = $(datadir)/applications
localedir   = $(datadir)/locale

obj_locale  = $(subst :, ,$(LANGUAGE))
cflags_libs = `pkg-config gtk+-3.0 --cflags --libs` `pkg-config gstreamer-video-1.0 --cflags --libs` `pkg-config gstreamer-mpegts-1.0 --libs`

xres  = $(wildcard res/*.xml)
gres := $(xres:.xml=.c)

srcs := $(wildcard src/*.c res/*.c )
srcs += res/gtv-dvb.gresource.c
objs  = $(srcs:.c=.o)


all: genres setlcdir build revlcdir msgfmt

compile: $(objs)

build: $(objs)
	@echo '    build: ' $(program)
	@gcc -Wall $^ -o $(program) $(CFLAG) $(cflags_libs)
	@echo

%.o: %.c
	@echo 'compile: ' $@
	@gcc -Wall -c $< -o $@ $(CFLAG) $(cflags_libs)


genres: $(gres)

%.c: %.xml
	@echo 'gresource: ' $@
	@glib-compile-resources $< --target=$@ --generate-source
	@echo

setlcdir:
	@sed 's|/usr/share/locale/|$(localedir)|g' -i src/gmp-dvb.c

revlcdir:
	@sed 's|$(localedir)|/usr/share/locale/|g' -i src/gmp-dvb.c


genpot:
	mkdir -p po
	xgettext src/*.c --language=C --keyword=N_ --keyword=_ --escape --sort-output --from-code=UTF-8 \
	--package-name=$(program) --package-version=$(version) -o po/$(program).pot
	sed 's|PACKAGE VERSION|$(program) $(version)|g;s|charset=CHARSET|charset=UTF-8|g' -i po/$(program).pot

mergeinit: genpot
	for lang in $(obj_locale); do \
		echo $$lang; \
		if [ ! -f po/$$lang.po ]; then msginit -i po/$(program).pot --locale=$$lang -o po/$$lang.po; \
		else msgmerge --update po/$$lang.po po/$(program).pot; fi \
	done

msgfmt:
	@for language in po/*.po; do \
		lang=`basename $$language | cut -f 1 -d '.'`; \
		mkdir -pv locale/$$lang/LC_MESSAGES/; \
		msgfmt -v $$language -o locale/$$lang/LC_MESSAGES/$(program).mo; \
	done

install:
	mkdir -p $(DESTDIR)$(bindir) $(DESTDIR)$(datadir) $(DESTDIR)$(desktopdir)
	install -Dp -m0755 $(program) $(DESTDIR)$(bindir)/$(program)
	install -Dp -m0644 res/$(program).desktop $(DESTDIR)$(desktopdir)/$(program).desktop
	sed 's|bindir|$(bindir)|g' -i $(DESTDIR)$(desktopdir)/$(program).desktop
	cp -r locale $(DESTDIR)$(datadir)

uninstall:
	rm -fr $(DESTDIR)$(bindir)/$(program) $(DESTDIR)$(desktopdir)/$(program).desktop \
	$(DESTDIR)$(localedir)/*/*/$(program).mo

clean:
	rm -f $(program) src/*.o res/*.o res/*.c po/$(program).pot po/*.po~
	rm -fr locale


# Show variables.
info:
	@echo
	@echo 'program      :' $(program)
	@echo 'prefix       :' $(prefix)
	@echo 'bindir       :' $(bindir)
	@echo 'datadir      :' $(datadir)
	@echo 'desktopdir   :' $(desktopdir)
	@echo


# Show help.
help:
	@echo 'Makefile for Gtv-dvb'
	@echo 'Get Gtv-dvb: https://github.com/vl-nix/gtv-dvb'
	@echo
	@echo 'Installation directories:'
	@echo '  Open the Makefile and set the prefix value'
	@echo '    prefix = PREFIX 	install files in PREFIX'
	@echo
	@echo '  Verify variables: make info'
	@echo
	@echo 'Usage: make [TARGET]'
	@echo 'TARGETS:'
	@echo '  all        or make'
	@echo '  help       print this message'
	@echo '  info       show variables'
	@echo '  compile    only compile'
	@echo '  build      build'
	@echo '  install    install'
	@echo '  uninstall  uninstall'
	@echo '  clean      clean all'
	@echo
	@echo 'Showing debug:'
	@echo '  G_MESSAGES_DEBUG=all ./$(program)'
	@echo

#############################################################################
