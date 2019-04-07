#!/bin/sh

valgrind --leak-check=full --show-leak-kinds=all ../src/netmon -p -c netmon.ini 2> valgrind.out

