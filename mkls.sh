mkls "$@" \
SRCS \
	*.c \
	parse.c \
	-- \
KNFMT \
	!(compat-*).c !(config|compat-queue).h \
	compat-?(arc4random|pledge|utimensat).c \
	-- \
CLANGTIDY \
	!(buffer|compat-*).c !(buffer|config|compat-queue).h \
	compat-?(arc4random|pledge|utimensat).c \
	-- \
CPPCHECK \
	!(buffer|compat-*).c compat-?(arc4random|pledge|utimensat).c \
	-- \
DISTFILES \
	*.1 *.5 *.c !(config).h *.md \
	GNUmakefile LICENSE Makefile configure parse.y \
	tests/*.sh \
	tests/GNUmakefile tests/Makefile

cd tests
mkls "$@" \
TESTS \
	!(t|util).sh
