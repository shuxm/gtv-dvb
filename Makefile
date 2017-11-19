target     = gtv-dvb
version    = 1.0
prefix     = $(HOME)/.local
bindir     = $(prefix)/bin
datadir    = $(prefix)/share
desktopdir = $(datadir)/applications
localedir  = $(datadir)/locale
obj_locale = $(subst :, ,$(LANGUAGE))


all: build gen_pot msg_merge_init msgfmt

build:
	gcc -Wall -Wextra \
		src/*.c \
		-o $(target) \
		`pkg-config gtk+-3.0 --cflags --libs` \
		`pkg-config gstreamer-video-1.0 --cflags --libs` \
		`pkg-config gstreamer-mpegts-1.0 --libs`

gen_pot:
	mkdir -p po
	xgettext src/*.c --language=C --keyword=N_ --escape --sort-output --from-code=utf-8 -o po/$(target).pot
	sed 's|PACKAGE VERSION|$(target) $(version)|g' -i po/$(target).pot

msg_merge_init:
	for lang in $(obj_locale); do \
		echo $$lang; \
		if [ ! -f po/$$lang.po ]; then msginit -i po/$(target).pot --locale=$$lang -o po/$$lang.po; \
		else msgmerge --update po/$$lang.po po/$(target).pot; fi \
	done

msgfmt:
	for lang in $(obj_locale); do \
		echo $$lang; \
		msgfmt -v po/$$lang.po -o $$lang.mo; \
		mkdir -pv locale/$$lang/LC_MESSAGES/; \
		mv $$lang.mo locale/$$lang/LC_MESSAGES/$(target).mo; \
	done

install:
	mkdir -p $(bindir) $(datadir) $(desktopdir)
	install -Dp -m0755 $(target) $(bindir)/$(target)
	install -Dp -m0644 res/$(target).desktop $(desktopdir)/$(target).desktop
	sed 's|bindir|$(bindir)|g' -i $(desktopdir)/$(target).desktop
	cp -r locale $(datadir)

uninstall:
	rm -fr $(bindir)/$(target) $(desktopdir)/$(target).desktop $(localedir)/*/*/$(target).mo

clean:
	rm -f src/*.o po/$(target).pot
	rm -r locale

