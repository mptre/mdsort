include ${.CURDIR}/Makefile.inc

VERSION=	0.1.0

PROG=	mdsort

SRC=	compat-arc4random.c \
	compat-pledge.c \
	compat-reallocarray.c \
	compat-utimensat.c \
	maildir.c \
	message.c \
	mdsort.c \
	parse.c \
	rule.c
OBJ=	${SRC:.c=.o}
DEP=	${SRC:.c=.d}

CFLAGS+=	${DEBUG}
CPPFLAGS+=	-I${.CURDIR}

DISTFILES=	GNUmakefile \
		LICENSE \
		Makefile \
		README.md \
		compat-arc4random.c \
		compat-pledge.c \
		compat-queue.h \
		compat-reallocarray.c \
		compat-utimensat.c \
		configure \
		extern.h \
		maildir.c \
		mdsort.1 \
		mdsort.c \
		mdsort.conf.5 \
		message.c \
		parse.y \
		rule.c \
		tests/GNUmakefile \
		tests/Makefile \
		tests/basic.sh \
		tests/conf.sh \
		tests/dry.sh \
		tests/run.sh

PREFIX=	/usr/local

all: ${PROG}

${PROG}: ${OBJ}
	${CC} ${DEBUG} -o ${PROG} ${OBJ} ${LDFLAGS}

clean:
	rm -f ${DEP} ${OBJ} ${PROG} parse.c
.PHONY: clean

dist:
	d=${PROG}-${VERSION}; \
	mkdir $$d; \
	for f in ${DISTFILES}; do \
		mkdir -p $$d/`dirname $$f`; \
		cp ${.CURDIR}/$$f $$d/$$f; \
	done; \
	tar czvf ${.CURDIR}/$$d.tar.gz $$d; \
	rm -r $$d
.PHONY: dist

distclean: clean
	rm -f ${.CURDIR}/Makefile.inc ${.CURDIR}/config.h \
		${.CURDIR}/config.log ${.CURDIR}/${PROG}-${VERSION}.tar.gz
.PHONY: distclean

install: all
	@mkdir -p ${DESTDIR}${PREFIX}/bin
	${INSTALL} ${PROG} ${DESTDIR}${PREFIX}/bin
	@mkdir -p ${DESTDIR}${PREFIX}/man/man1
	${INSTALL} ${.CURDIR}/mdsort.1 ${DESTDIR}${PREFIX}/man/man1
	@mkdir -p ${DESTDIR}${PREFIX}/man/man5
	${INSTALL} ${.CURDIR}/mdsort.conf.5 ${DESTDIR}${PREFIX}/man/man5
.PHONY: install

test: ${PROG}
	@${MAKE} -C ${.CURDIR}/tests
.PHONY: test

-include ${DEP}
