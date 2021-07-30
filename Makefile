include ${.CURDIR}/config.mk

VERSION=	10.0.1

PROG=	mdsort

SRCS+=	compat-arc4random.c
SRCS+=	compat-base64.c
SRCS+=	compat-errc.c
SRCS+=	compat-pledge.c
SRCS+=	compat-reallocarray.c
SRCS+=	compat-strlcpy.c
SRCS+=	compat-utimensat.c
SRCS+=	compat-warnc.c
SRCS+=	expr.c
SRCS+=	fault.c
SRCS+=	maildir.c
SRCS+=	match.c
SRCS+=	mdsort.c
SRCS+=	message.c
SRCS+=	parse.c
SRCS+=	time.c
SRCS+=	util.c

OBJS=	${SRCS:.c=.o}
DEPS=	${SRCS:.c=.d}

KNFMT+=	compat-arc4random.c
KNFMT+=	compat-pledge.c
KNFMT+=	compat-utimensat.c
KNFMT+=	expr.c
KNFMT+=	extern.h
KNFMT+=	fault.c
KNFMT+=	fault.h
KNFMT+=	maildir.c
KNFMT+=	match.c
KNFMT+=	mdsort.c
KNFMT+=	message.c
KNFMT+=	time.c
KNFMT+=	util.c

DISTFILES+=	CHANGELOG.md
DISTFILES+=	GNUmakefile
DISTFILES+=	LICENSE
DISTFILES+=	Makefile
DISTFILES+=	README.md
DISTFILES+=	compat-arc4random.c
DISTFILES+=	compat-base64.c
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
DISTFILES+=	fault.c
DISTFILES+=	fault.h
DISTFILES+=	maildir.c
DISTFILES+=	match.c
DISTFILES+=	mdsort.1
DISTFILES+=	mdsort.c
DISTFILES+=	mdsort.conf.5
DISTFILES+=	message.c
DISTFILES+=	parse.y
DISTFILES+=	tests/GNUmakefile
DISTFILES+=	tests/Makefile
DISTFILES+=	tests/action-attachment.sh
DISTFILES+=	tests/action-break.sh
DISTFILES+=	tests/action-discard.sh
DISTFILES+=	tests/action-exec.sh
DISTFILES+=	tests/action-flag.sh
DISTFILES+=	tests/action-label.sh
DISTFILES+=	tests/action-move.sh
DISTFILES+=	tests/action-pass.sh
DISTFILES+=	tests/action-reject.sh
DISTFILES+=	tests/basic.sh
DISTFILES+=	tests/conf.sh
DISTFILES+=	tests/dry.sh
DISTFILES+=	tests/macros.sh
DISTFILES+=	tests/maildir.sh
DISTFILES+=	tests/match-all.sh
DISTFILES+=	tests/match-attachment.sh
DISTFILES+=	tests/match-body-b64.sh
DISTFILES+=	tests/match-body-qp.sh
DISTFILES+=	tests/match-body.sh
DISTFILES+=	tests/match-date.sh
DISTFILES+=	tests/match-header.sh
DISTFILES+=	tests/match-isdirectory.sh
DISTFILES+=	tests/match-logical.sh
DISTFILES+=	tests/match-new.sh
DISTFILES+=	tests/match-old.sh
DISTFILES+=	tests/stdin.sh
DISTFILES+=	tests/t.sh
DISTFILES+=	tests/util.sh
DISTFILES+=	time.c
DISTFILES+=	util.c

TESTFLAGS?=	-Tfault

all: ${PROG}

${PROG}: ${OBJS}
	${CC} ${DEBUG} -o ${PROG} ${OBJS} ${LDFLAGS}

clean:
	rm -f ${DEPS} ${OBJS} ${PROG} parse.c y.tab.h
.PHONY: clean

dist:
	set -e; \
	d=mdsort-${VERSION}; \
	mkdir $$d; \
	for f in ${DISTFILES}; do \
		mkdir -p $$d/`dirname $$f`; \
		cp -p ${.CURDIR}/$$f $$d/$$f; \
	done; \
	find $$d -type d -exec touch -r ${.CURDIR}/Makefile {} \;; \
	tar czvf ${.CURDIR}/$$d.tar.gz $$d; \
	(cd ${.CURDIR}; sha256 $$d.tar.gz >$$d.sha256); \
	rm -r $$d
.PHONY: dist

distclean: clean
	rm -f ${.CURDIR}/config.h ${.CURDIR}/config.log ${.CURDIR}/config.mk \
		${.CURDIR}/mdsort-${VERSION}.tar.gz \
		${.CURDIR}/mdsort-${VERSION}.sha256
.PHONY: distclean

format:
	cd ${.CURDIR} && knfmt -i ${KNFMT}
.PHONY: format

install: all
	@mkdir -p ${DESTDIR}${BINDIR}
	${INSTALL} ${PROG} ${DESTDIR}${BINDIR}
	@mkdir -p ${DESTDIR}${MANDIR}/man1
	${INSTALL_MAN} ${.CURDIR}/mdsort.1 ${DESTDIR}${MANDIR}/man1
	@mkdir -p ${DESTDIR}${MANDIR}/man5
	${INSTALL_MAN} ${.CURDIR}/mdsort.conf.5 ${DESTDIR}${MANDIR}/man5
.PHONY: install

lint:
	cd ${.CURDIR} && mandoc -Tlint -Wstyle mdsort.1 mdsort.conf.5
	cd ${.CURDIR} && knfmt -d ${KNFMT}
.PHONY: lint

test: all
	${MAKE} -C ${.CURDIR}/tests \
		"COREDUMP=${.CURDIR}" \
		"MDSORT=${.OBJDIR}/${PROG}" \
		"TESTFLAGS=${TESTFLAGS}"
.PHONY: test

-include ${DEPS}
