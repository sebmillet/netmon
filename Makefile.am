# Makefile.am

SUBDIRS = src doc
ACLOCAL_AMFLAGS = -I admin

dist_doc_DATA=README
dist_pkgdata_SCRIPTS = src/printargs.sh

EXTRA_DIST=netmon.bkl gen-win-makefile.cmd test_linux makefile.gcc extracfg.h createzip.sh bootstrap

dist-hook:
	rm -rf `find $(distdir) -name .git`

appsdir = $(datadir)/applications

