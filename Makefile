include ${.CURDIR}/Makefile.inc

VERSION=	1.1.0

PROG=	mdsort

SRCS=	compat-arc4random.c \
	compat-pledge.c \
	compat-reallocarray.c \
	compat-utimensat.c \
	maildir.c \
	message.c \
	mdsort.c \
	parse.c \
	rule.c \
	util.c
OBJS=	${SRCS:.c=.o}
DEPS=	${SRCS:.c=.d}

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
		tests/flag.sh \
		tests/match-all.sh \
		tests/match-body.sh \
		tests/match-header.sh \
		tests/match-logical.sh \
		tests/match-new.sh \
		tests/run.sh \
		util.c

PREFIX=	/usr/local

TESTFLAGS?=	-e MALLOC_OPTIONS=${MALLOC_OPTIONS}

all: ${PROG}

${PROG}: ${OBJS}
	${CC} ${DEBUG} -o ${PROG} ${OBJS} ${LDFLAGS}

clean:
	rm -f ${DEPS} ${OBJS} ${PROG} parse.c
.PHONY: clean

dist:
	set -e; \
	d=${PROG}-${VERSION}; \
	mkdir $$d; \
	for f in ${DISTFILES}; do \
		mkdir -p $$d/`dirname $$f`; \
		cp ${.CURDIR}/$$f $$d/$$f; \
	done; \
	tar czvf ${.CURDIR}/$$d.tar.gz $$d; \
	(cd ${.CURDIR}; ${SHA256} $$d.tar.gz >$$d.sha256); \
	rm -r $$d
.PHONY: dist

distclean: clean
	rm -f ${.CURDIR}/Makefile.inc ${.CURDIR}/config.h \
		${.CURDIR}/config.log ${.CURDIR}/${PROG}-${VERSION}.tar.gz \
		${.CURDIR}/${PROG}-${VERSION}.sha256
.PHONY: distclean

install: all
	@mkdir -p ${DESTDIR}${PREFIX}/bin
	${INSTALL} ${PROG} ${DESTDIR}${PREFIX}/bin
	@mkdir -p ${DESTDIR}${PREFIX}/man/man1
	${INSTALL} ${.CURDIR}/mdsort.1 ${DESTDIR}${PREFIX}/man/man1
	@mkdir -p ${DESTDIR}${PREFIX}/man/man5
	${INSTALL} ${.CURDIR}/mdsort.conf.5 ${DESTDIR}${PREFIX}/man/man5
.PHONY: install

lint:
	mandoc -Tlint -Wstyle ${.CURDIR}/mdsort.1 ${.CURDIR}/mdsort.conf.5
.PHONY: lint

test: ${PROG}
	${MAKE} -C ${.CURDIR}/tests \
		"MDSORT=${.OBJDIR}/${PROG}" \
		"TESTFLAGS=${TESTFLAGS}"
.PHONY: test

-include ${DEPS}
