# src/Makefile.am

bin_PROGRAMS=netmon
netmon_SOURCES=netmon.c netmon.h \
	img/st-undef.png img/st-undef.c \
	img/st-unknown.png img/st-unknown.c \
	img/st-ok.png img/st-ok.c \
	img/st-fail.png img/st-fail.c

netmon_LDFLAGS=-lpthread

img/st-undef.c: img/st-undef.png
	img/rdtosrc.sh $<
img/st-unknown.c: img/st-unknown.png
	img/rdtosrc.sh $<
img/st-ok.c: img/st-ok.png
	img/rdtosrc.sh $<
img/st-fail.c: img/st-fail.png
	img/rdtosrc.sh $<

