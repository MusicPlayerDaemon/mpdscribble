#!/bin/sh -e

rm -rf config.cache build
mkdir build

aclocal -I $PWD/m4
autoconf
autoheader
automake --add-missing

if test x$NOCONFIGURE = x; then
	echo "./configure $*"
	./configure "$@"
fi
