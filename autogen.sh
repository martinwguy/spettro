#! /bin/sh

# Runes to generate "configure" and "Makefile"

aclocal
autoconf
autoheader	# generates configure.h.in
automake --add-missing
