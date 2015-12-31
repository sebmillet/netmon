#!/bin/sh

INP=memdbg.log
OUT=memdbg-parsed.txt

echo "Parsing file $INP into $OUT"

./check-memlog.pl "$INP" > "$OUT"

