SUBDIRS=talk

noinst_HEADERS = config.h

EXTRA_DIST=README.win DOCUMENTATION

dist-hook:
	sed -i -f talk/sanitize.sed `find $(distdir) -type f`
