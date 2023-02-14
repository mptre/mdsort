export LC_ALL=C

mkls "$@" \
SRCS \
	!(fuzz-*|mdsort|t).c \
	parse.c \
	$(find libks -type f -name '*.c' -exec basename {} \;) \
	-- \
KNFMT \
	!(compat-*|parse).c !(config|compat-queue).h \
	compat-?(arc4random|pledge|utimensat).c \
	-- \
CLANGTIDY \
	!(compat-*|parse).c !(config|compat-queue).h \
	compat-?(arc4random|pledge|utimensat).c \
	-- \
CPPCHECK \
	!(compat-*|parse).c compat-?(arc4random|pledge|utimensat).c \
	-- \
DISTFILES \
	*.1 *.5 !(parse).c !(config).h libks/*.[ch] *.md \
	GNUmakefile LICENSE Makefile configure parse.y \
	tests/*.sh \
	tests/GNUmakefile tests/Makefile

cd tests
mkls "$@" \
TESTS \
	!(t|util).sh
