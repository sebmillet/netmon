#!/bin/sh

ZIPNAME=netmon-1.1.1.zip

mkdir ztmp
cd ztmp

cp -vi ~/netmon.exe .
cp -vi ../doc/netmon.html .
cp -vi ../doc/netmon-sample.ini .
cp -vi ../README README.TXT

zip $ZIPNAME netmon.exe netmon.html netmon-sample.ini README.TXT
mv $ZIPNAME ..

