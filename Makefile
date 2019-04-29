include ${.CURDIR}/Makefile.inc

VERSION=	3.1.0

PROG=	mdsort

SRCS+=	compat-arc4random.c
SRCS+=	compat-errc.c
SRCS+=	compat-pledge.c
SRCS+=	compat-reallocarray.c
SRCS+=	compat-strlcpy.c
SRCS+=	compat-utimensat.c
SRCS+=	compat-warnc.c
SRCS+=	expr.c
SRCS+=	maildir.c
SRCS+=	match.c
SRCS+=	mdsort.c
SRCS+=	message.c
SRCS+=	parse.c
SRCS+=	time.c
SRCS+=	util.c

OBJS=	${SRCS:.c=.o}
DEPS=	${SRCS:.c=.d}

DISTFILES+=	CHANGELOG.md
DISTFILES+=	GNUmakefile
DISTFILES+=	LICENSE
DISTFILES+=	Makefile
DISTFILES+=	README.md
DISTFILES+=	compat-arc4random.c
DISTFILES+=	compat-errc.c
DISTFILES+=	compat-pledge.c
DISTFILES+=	compat-queue.h
DISTFILES+=	compat-reallocarray.c
DISTFILES+=	compat-strlcpy.c
DISTFILES+=	compat-utimensat.c
DISTFILES+=	compat-warnc.c
DISTFILES+=	configure
DISTFILES+=	expr.c
DISTFILES+=	extern.h
DISTFILES+=	maildir.c
DISTFILES+=	match.c
DISTFILES+=	mdsort.1
DISTFILES+=	mdsort.c
DISTFILES+=	mdsort.conf.5
DISTFILES+=	message.c
DISTFILES+=	parse.y
DISTFILES+=	tests/GNUmakefile
DISTFILES+=	tests/Makefile
DISTFILES+=	tests/action-break.sh
DISTFILES+=	tests/action-discard.sh
DISTFILES+=	tests/action-flag.sh
DISTFILES+=	tests/action-label.sh
DISTFILES+=	tests/basic.sh
DISTFILES+=	tests/conf.sh
DISTFILES+=	tests/dry.sh
DISTFILES+=	tests/exdev.sh
DISTFILES+=	tests/match-all.sh
DISTFILES+=	tests/match-attachment.sh
DISTFILES+=	tests/match-body.sh
DISTFILES+=	tests/match-date.sh
DISTFILES+=	tests/match-header.sh
DISTFILES+=	tests/match-logical.sh
DISTFILES+=	tests/match-new.sh
DISTFILES+=	tests/match-old.sh
DISTFILES+=	tests/match-reject.sh
DISTFILES+=	tests/stdin.sh
DISTFILES+=	tests/t.sh
DISTFILES+=	tests/util.sh
DISTFILES+=	time.c
DISTFILES+=	util.c

all: ${PROG}

${PROG}: ${OBJS}
	${CC} ${DEBUG} -o ${PROG} ${OBJS} ${LDFLAGS}

clean:
	rm -f ${DEPS} ${OBJS} ${PROG} parse.c y.tab.h
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
	(cd ${.CURDIR}; sha256 $$d.tar.gz >$$d.sha256); \
	rm -r $$d
.PHONY: dist

distclean: clean
	rm -f ${.CURDIR}/Makefile.inc ${.CURDIR}/config.h \
		${.CURDIR}/config.log ${.CURDIR}/${PROG}-${VERSION}.tar.gz \
		${.CURDIR}/${PROG}-${VERSION}.sha256
.PHONY: distclean

install: all
	@mkdir -p ${DESTDIR}${BINDIR}
	${INSTALL} ${PROG} ${DESTDIR}${BINDIR}
	@mkdir -p ${DESTDIR}${MANDIR}/man1
	${INSTALL_MAN} ${.CURDIR}/mdsort.1 ${DESTDIR}${MANDIR}/man1
	@mkdir -p ${DESTDIR}${MANDIR}/man5
	${INSTALL_MAN} ${.CURDIR}/mdsort.conf.5 ${DESTDIR}${MANDIR}/man5
.PHONY: install

lint:
	mandoc -Tlint -Wstyle ${.CURDIR}/mdsort.1 ${.CURDIR}/mdsort.conf.5
.PHONY: lint

test: all
	${MAKE} -C ${.CURDIR}/tests \
		"MALLOC_OPTIONS=${MALLOC_OPTIONS}" \
		"MDSORT=${.OBJDIR}/${PROG}" \
		"TESTFLAGS=${TESTFLAGS}"
.PHONY: test

-include ${DEPS}
