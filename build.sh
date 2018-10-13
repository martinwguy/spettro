#! /bin/sh

# Runes to generate "configure", run it and compile the program.

aclocal
autoconf
autoheader	# generates configure.h.in
automake --add-missing
./configure
make
