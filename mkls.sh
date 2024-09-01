mkls -s "$@" -- \
SRCS		!(fuzz-*|mdsort|t).c parse.c $(cd libks && ls *.c) -- \
KNFMT		!(compat-*|parse).c !(config).h \
		compat-?(arc4random|pledge).c -- \
CLANGTIDY	!(compat-*|parse).c !(config).h \
		compat-?(arc4random|pledge).c -- \
CPPCHECK	!(compat-*|parse).c compat-?(arc4random|pledge).c -- \
SHLINT		configure tests/!(t).sh

cd tests
mkls -s "$@" -- \
TESTS	!(t|util).sh
