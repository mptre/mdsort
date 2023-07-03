mkls -s "$@" -- \
SRCS		!(fuzz-*|mdsort|t).c parse.c $(cd libks && ls *.c) -- \
KNFMT		!(compat-*|parse).c !(config|compat-queue).h \
		compat-?(arc4random|pledge|utimensat).c -- \
CLANGTIDY	!(compat-*|parse).c !(config|compat-queue).h \
		compat-?(arc4random|pledge|utimensat).c -- \
CPPCHECK	!(compat-*|parse).c compat-?(arc4random|pledge|utimensat).c -- \
SHLINT		configure -- \
DISTFILES \
	*.1 *.5 !(parse).c !(config).h libks/*.[ch] *.md \
	GNUmakefile LICENSE Makefile configure parse.y \
	tests/*.sh \
	tests/GNUmakefile tests/Makefile

cd tests
mkls -s "$@" -- \
TESTS	!(t|util).sh
