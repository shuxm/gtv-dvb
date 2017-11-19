#############################################################################
#
# Makefile for Gtv-dvb
# Get Gtv-dvb: https://github.com/vl-nix/gtv-dvb
#
#
# Depends
# -------
# gcc
# gtk+3
# gstreamer
# gst-plugins-base
# gst-plugins-good
# gst-plugins-ugly
# gst-plugins-bad gst-libav
#
#
# Make Target:
# ------------
#   $ make            compile all
#   $ make depends    check dependencies
#   $ make build      only build
#   $ make install    install all
#   $ make uninstall  uninstall all
#   $ make clean 	  clean all
#   $ make help       get the usage of the makefile
#   $ make info       show variables
#
#   $ For translators:
#		make gen_pot         only xgettext -> pot
#		make msg_merge_init  only msgmerge or msginit pot -> po
#		make msgfmt          only msgfmt lang.po -> lang.mo
#
#===========================================================================


## Set prefix=PREFIX ( install files in PREFIX )
##==========================================================================
# prefix     = /usr
prefix     = $(HOME)/.local


program    = gtv-dvb
version    = 1.0

bindir     = $(prefix)/bin
datadir    = $(prefix)/share
desktopdir = $(datadir)/applications

localedir  = $(datadir)/locale
obj_locale = $(subst :, ,$(LANGUAGE))

obj_depends = gtk+-3.0 gstreamer-1.0 gstreamer-plugins-base-1.0 gstreamer-plugins-good-1.0 gstreamer-plugins-bad-1.0 gstreamer-video-1.0


all: depends build translation


depends:
	for depend in $(obj_depends); do \
		echo $$depend; \
		pkg-config --exists --print-errors $$depend; \
	done

build:
	gcc -Wall -Wextra \
		src/*.c \
		-o $(program) \
		`pkg-config gtk+-3.0 --cflags --libs` \
		`pkg-config gstreamer-video-1.0 --cflags --libs` \
		`pkg-config gstreamer-mpegts-1.0 --libs`

translation: gen_pot msg_merge_init msgfmt	

gen_pot:
	mkdir -p po
	xgettext src/*.c --language=C --keyword=N_ --escape --sort-output --from-code=UTF-8 --package-name=$(program) --package-version=$(version) -o po/$(program).pot
	sed 's|PACKAGE VERSION|$(program) $(version)|g;s|charset=CHARSET|charset=UTF-8|g' -i po/$(program).pot

msg_merge_init:
	for lang in $(obj_locale); do \
		echo $$lang; \
		if [ ! -f po/$$lang.po ]; then msginit -i po/$(program).pot --locale=$$lang -o po/$$lang.po; \
		else msgmerge --update po/$$lang.po po/$(program).pot; fi \
	done

msgfmt:
	for lang in $(obj_locale); do \
		echo $$lang; \
		msgfmt -v po/$$lang.po -o $$lang.mo; \
		mkdir -pv locale/$$lang/LC_MESSAGES/; \
		mv $$lang.mo locale/$$lang/LC_MESSAGES/$(program).mo; \
	done

install:
	mkdir -p $(bindir) $(datadir) $(desktopdir)
	install -Dp -m0755 $(program) $(bindir)/$(program)
	install -Dp -m0644 res/$(program).desktop $(desktopdir)/$(program).desktop
	sed 's|bindir|$(bindir)|g' -i $(desktopdir)/$(program).desktop
	cp -r locale $(datadir)

uninstall:
	rm -fr $(bindir)/$(program) $(desktopdir)/$(program).desktop $(localedir)/*/*/$(program).mo

clean:
	rm -f $(program) src/*.o po/$(program).pot
	rm -r locale



# Show help.
help:
	@echo 'Makefile for Gtv-dvb'
	@echo 'Get Gtv-dvb: https://github.com/vl-nix/gtv-dvb'
	@echo
	@echo 'Usage: make [TARGET]'
	@echo 'TARGETS:'
	@echo '  all        (=make) compile all'
	@echo '  depends    check dependencies'
	@echo '  build      only build'
	@echo '  install    install all'
	@echo '  uninstall  uninstall all'
	@echo '  clean      clean all'
	@echo '  show       show variables'
	@echo '  help       print this message'
	@echo
	@echo 'For translators:'
	@echo '  gen_pot         only xgettext -> pot'
	@echo '  msg_merge_init  only msgmerge or msginit pot -> po'
	@echo '  msgfmt          only msgfmt lang.po -> lang.mo'
	@echo

# Show variables.
info:
	@echo
	@echo 'program      :' $(program)
	@echo 'prefix       :' $(prefix)
	@echo 'bindir       :' $(bindir)
	@echo 'datadir      :' $(datadir)
	@echo 'desktopdir   :' $(desktopdir)
	@echo 'localedir    :' $(localedir)
	@echo 'obj_locale   :' $(obj_locale)
	@echo 'obj_depends  :' $(obj_depends)
	@echo

## End of the Makefile
#############################################################################
