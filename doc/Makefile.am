# doc/Makefile.am

MANPAGEEN=${srcdir}/netmon.1
HTMLPAGEEN=${srcdir}/netmon.html
TEXTPAGEEN=${srcdir}/netmon.txt

dist_doc_DATA=${HTMLPAGEEN} ${TEXTPAGEEN} ${srcdir}/netmon-sample.ini

EXTRA_DIST=${HTMLPAGEEN} ${TEXTPAGEEN} ${srcdir}/build-man-en.sh ${srcdir}/netmonen-prefix.1 ${srcdir}/netmonen-postfix.1
	${srcdir}/netmon-sample.ini

dist_man_MANS=${MANPAGEEN}

if BUILD_DOCS

CLEANFILES=${srcdir}/netmonen-m0.txt ${srcdir}/netmonen-m1.1 ${srcdir}/netmonen-m2.1

${MANPAGEEN}: ${srcdir}/netmonen-prefix.1 ${srcdir}/netmon-sample.ini ${srcdir}/netmonen-postfix.1
	${srcdir}/build-man-en.sh

${HTMLPAGEEN}: ${MANPAGEEN}
	mandoc -T html ${MANPAGEEN} > $@
${TEXTPAGEEN}: ${MANPAGEEN}
	nroff -man ${MANPAGEEN} | col -b > $@
	unix2dos $@

# maintainer-clean-local:
#     rm -rf netmonen-m1.1 netmonen-m2.1
	
endif

