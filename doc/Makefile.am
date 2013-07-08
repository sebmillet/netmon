# doc/Makefile.am

MANPAGEEN=${srcdir}/netmon.1
HTMLPAGEEN=${srcdir}/netmonen.html
TEXTPAGEEN=${srcdir}/netmonen.txt

dist_doc_DATA=${HTMLPAGEEN} ${TEXTPAGEEN}

EXTRA_DIST=${HTMLPAGEEN} ${TEXTPAGEEN} ${srcdir}/build-man-en.sh ${srcdir}/netmonen-prefix.1 ${srcdir}/netmonen-postfix.1
	${srcdir}/netmon-sample.ini

dist_man_MANS=${MANPAGEEN}

if BUILD_DOCS

${MANPAGEEN}: ${srcdir}/netmonen-prefix.1 ${srcdir}/netmon-sample.ini ${srcdir}/netmonen-postfix.1
	${srcdir}/build-man-en.sh

${HTMLPAGEEN}: ${MANPAGEEN}
	man2html ${MANPAGEEN} > $@
${TEXTPAGEEN}: ${MANPAGEEN}
	nroff -man ${MANPAGEEN} | col -b > $@

maintainer-clean-local:
	rm -rf netmonen-m1.1 netmonen-m2.1
	
endif

