# src/Makefile.am

bin_PROGRAMS=netmon
netmon_SOURCES=main.c main.h util.c util.h webserver.c \
	img/st-undef.png img/st-undef.c \
	img/st-unknown.png img/st-unknown.c \
	img/st-ok.png img/st-ok.c \
	img/st-fail.png img/st-fail.c \
	img/rdtosrc.sh img/st.xcf \
	build-embdoc.sh check.sh \
	printargs.sh netmon.html.c

img/st-undef.c: img/st-undef.png
	img/rdtosrc.sh $<
img/st-unknown.c: img/st-unknown.png
	img/rdtosrc.sh $<
img/st-ok.c: img/st-ok.png
	img/rdtosrc.sh $<
img/st-fail.c: img/st-fail.png
	img/rdtosrc.sh $<

netmon.html.c: ${top_srcdir}/doc/netmon.html
	./build-embdoc.sh

