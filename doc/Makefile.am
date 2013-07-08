# doc/Makefile.am

MANPAGEEN=${srcdir}/netmon.1
HTMLPAGEEN=${srcdir}/netmonen.html
TEXTPAGEEN=${srcdir}/netmonen.txt

dist_doc_DATA=${HTMLPAGEEN} ${TEXTPAGEEN}

EXTRA_DIST=${HTMLPAGEEN} ${TEXTPAGEEN}

dist_man_MANS=${MANPAGEEN}

if BUILD_DOCS

${HTMLPAGEEN}: ${MANPAGEEN}
	man2html ${MANPAGEEN} > $@
${TEXTPAGEEN}: ${MANPAGEEN}
	nroff -man ${MANPAGEEN} | col -b > $@

endif

