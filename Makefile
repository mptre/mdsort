include ${.CURDIR}/config.mk

VERSION=	11.5.0

PROG=	mdsort

SRCS+=	buffer.c
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
SRCS+=	macro.c
SRCS+=	maildir.c
SRCS+=	match.c
SRCS+=	mdsort.c
SRCS+=	message.c
SRCS+=	parse.c
SRCS+=	time.c
SRCS+=	util.c
SRCS+=	vector.c

OBJS=	${SRCS:.c=.o}
DEPS=	${SRCS:.c=.d}

KNFMT+=	buffer.c
KNFMT+=	buffer.h
KNFMT+=	cdefs.h
KNFMT+=	compat-arc4random.c
KNFMT+=	compat-pledge.c
KNFMT+=	compat-utimensat.c
KNFMT+=	expr.c
KNFMT+=	extern.h
KNFMT+=	fault.c
KNFMT+=	fault.h
KNFMT+=	macro.c
KNFMT+=	macro.h
KNFMT+=	maildir.c
KNFMT+=	match.c
KNFMT+=	mdsort.c
KNFMT+=	message.c
KNFMT+=	message.h
KNFMT+=	time.c
KNFMT+=	util.c
KNFMT+=	vector.c
KNFMT+=	vector.h

CLANGTIDY+=	buffer.c
CLANGTIDY+=	buffer.h
CLANGTIDY+=	cdefs.h
CLANGTIDY+=	compat-arc4random.c
CLANGTIDY+=	compat-pledge.c
CLANGTIDY+=	compat-utimensat.c
CLANGTIDY+=	expr.c
CLANGTIDY+=	extern.h
CLANGTIDY+=	fault.c
CLANGTIDY+=	fault.h
CLANGTIDY+=	macro.c
CLANGTIDY+=	macro.h
CLANGTIDY+=	maildir.c
CLANGTIDY+=	match.c
CLANGTIDY+=	mdsort.c
CLANGTIDY+=	message.c
CLANGTIDY+=	message.h
CLANGTIDY+=	time.c
CLANGTIDY+=	util.c
CLANGTIDY+=	vector.c
CLANGTIDY+=	vector.h

CPPCHECK+=	buffer.c
CPPCHECK+=	compat-arc4random.c
CPPCHECK+=	compat-pledge.c
CPPCHECK+=	compat-utimensat.c
CPPCHECK+=	expr.c
CPPCHECK+=	fault.c
CPPCHECK+=	macro.c
CPPCHECK+=	maildir.c
CPPCHECK+=	match.c
CPPCHECK+=	mdsort.c
CPPCHECK+=	message.c
CPPCHECK+=	time.c
CPPCHECK+=	util.c
CPPCHECK+=	vector.c

DISTFILES+=	CHANGELOG.md
DISTFILES+=	GNUmakefile
DISTFILES+=	LICENSE
DISTFILES+=	Makefile
DISTFILES+=	README.md
DISTFILES+=	buffer.c
DISTFILES+=	buffer.h
DISTFILES+=	cdefs.h
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
DISTFILES+=	macro.c
DISTFILES+=	macro.h
DISTFILES+=	maildir.c
DISTFILES+=	match.c
DISTFILES+=	mdsort.1
DISTFILES+=	mdsort.c
DISTFILES+=	mdsort.conf.5
DISTFILES+=	message.c
DISTFILES+=	message.h
DISTFILES+=	parse.y
DISTFILES+=	tests/GNUmakefile
DISTFILES+=	tests/Makefile
DISTFILES+=	tests/action-add-header.sh
DISTFILES+=	tests/action-attachment.sh
DISTFILES+=	tests/action-break.sh
DISTFILES+=	tests/action-discard.sh
DISTFILES+=	tests/action-exec.sh
DISTFILES+=	tests/action-flag.sh
DISTFILES+=	tests/action-flags.sh
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
DISTFILES+=	tests/match-command.sh
DISTFILES+=	tests/match-date.sh
DISTFILES+=	tests/match-header-b64.sh
DISTFILES+=	tests/match-header-qp.sh
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
DISTFILES+=	vector.c
DISTFILES+=	vector.h

TESTFLAGS?=	-Tfault

all: ${PROG}

${PROG}: ${OBJS}
	${CC} ${DEBUG} -o ${PROG} ${OBJS} ${LDFLAGS}

clean:
	rm -f ${DEPS} ${OBJS} ${PROG} parse.c y.tab.h
.PHONY: clean

cleandir: clean
	cd ${.CURDIR} && rm -f config.h config.log config.mk
.PHONY: cleandir

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

format:
	cd ${.CURDIR} && knfmt -is ${KNFMT}
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
	cd ${.CURDIR} && knfmt -ds ${KNFMT}
	cd ${.CURDIR} && mandoc -Tlint -Wstyle mdsort.1 mdsort.conf.5
	${MAKE} -C ${.CURDIR}/tests lint
.PHONY: lint

lint-clang-tidy:
	cd ${.CURDIR} && clang-tidy --quiet ${CLANGTIDY}
.PHONY: lint-clang-tidy

lint-cppcheck:
	cd ${.CURDIR} && cppcheck --quiet --enable=all --error-exitcode=1 \
		--max-configs=2 --suppress-xml=cppcheck-suppressions.xml \
		${CPPCHECK}
.PHONY: lint-cppcheck

test: all
	${MAKE} -C ${.CURDIR}/tests \
		"COREDUMP=${.CURDIR}" \
		"MDSORT=${.OBJDIR}/${PROG}" \
		"TESTFLAGS=${TESTFLAGS}"
.PHONY: test

-include ${DEPS}
