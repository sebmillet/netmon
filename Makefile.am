# Makefile.am

SUBDIRS = src
ACLOCAL_AMFLAGS = -I admin

dist_doc_DATA=README

EXTRA_DIST=extracfg.h

dist-hook:
	rm -rf `find $(distdir) -name .svn`

appsdir = $(datadir)/applications

