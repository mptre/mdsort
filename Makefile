include ${.CURDIR}/config.mk

VERSION=	11.5.1

SRCS+=	buffer.c
SRCS+=	compat-arc4random.c
SRCS+=	compat-errc.c
SRCS+=	compat-pledge.c
SRCS+=	compat-reallocarray.c
SRCS+=	compat-strlcpy.c
SRCS+=	compat-utimensat.c
SRCS+=	compat-warnc.c
SRCS+=	conf.c
SRCS+=	decode.c
SRCS+=	expr.c
SRCS+=	fault.c
SRCS+=	macro.c
SRCS+=	maildir.c
SRCS+=	match.c
SRCS+=	message.c
SRCS+=	parse.c
SRCS+=	time.c
SRCS+=	util.c
SRCS+=	vector.c

SRCS_mdsort+=	${SRCS}
SRCS_mdsort+=	mdsort.c
OBJS_mdsort=	${SRCS_mdsort:.c=.o}
DEPS_mdsort=	${SRCS_mdsort:.c=.d}
PROG_mdsort=	mdsort

SRCS_test+=	${SRCS}
SRCS_test+=	t.c
OBJS_test=	${SRCS_test:.c=.o}
DEPS_test=	${SRCS_test:.c=.d}
PROG_test=	t

SRCS_fuzz-config+=	${SRCS}
SRCS_fuzz-config+=	fuzz-config.c
OBJS_fuzz-config=	${SRCS_fuzz-config:.c=.o}
DEPS_fuzz-config=	${SRCS_fuzz-config:.c=.d}
PROG_fuzz-config=	fuzz-config

SRCS_fuzz-message+=	${SRCS}
SRCS_fuzz-message+=	fuzz-message.c
OBJS_fuzz-message=	${SRCS_fuzz-message:.c=.o}
DEPS_fuzz-message=	${SRCS_fuzz-message:.c=.d}
PROG_fuzz-message=	fuzz-message

KNFMT+=	cdefs.h
KNFMT+=	compat-arc4random.c
KNFMT+=	compat-pledge.c
KNFMT+=	compat-utimensat.c
KNFMT+=	conf.c
KNFMT+=	conf.h
KNFMT+=	decode.c
KNFMT+=	decode.h
KNFMT+=	expr.c
KNFMT+=	extern.h
KNFMT+=	fault.c
KNFMT+=	fault.h
KNFMT+=	fuzz-config.c
KNFMT+=	fuzz-message.c
KNFMT+=	macro.c
KNFMT+=	macro.h
KNFMT+=	maildir.c
KNFMT+=	match.c
KNFMT+=	mdsort.c
KNFMT+=	message.c
KNFMT+=	message.h
KNFMT+=	t.c
KNFMT+=	time.c
KNFMT+=	util.c

CLANGTIDY+=	cdefs.h
CLANGTIDY+=	compat-arc4random.c
CLANGTIDY+=	compat-pledge.c
CLANGTIDY+=	compat-utimensat.c
CLANGTIDY+=	conf.c
CLANGTIDY+=	conf.h
CLANGTIDY+=	decode.c
CLANGTIDY+=	decode.h
CLANGTIDY+=	expr.c
CLANGTIDY+=	extern.h
CLANGTIDY+=	fault.c
CLANGTIDY+=	fault.h
CLANGTIDY+=	fuzz-config.c
CLANGTIDY+=	fuzz-message.c
CLANGTIDY+=	macro.c
CLANGTIDY+=	macro.h
CLANGTIDY+=	maildir.c
CLANGTIDY+=	match.c
CLANGTIDY+=	mdsort.c
CLANGTIDY+=	message.c
CLANGTIDY+=	message.h
CLANGTIDY+=	t.c
CLANGTIDY+=	time.c
CLANGTIDY+=	util.c

CPPCHECK+=	compat-arc4random.c
CPPCHECK+=	compat-pledge.c
CPPCHECK+=	compat-utimensat.c
CPPCHECK+=	conf.c
CPPCHECK+=	decode.c
CPPCHECK+=	expr.c
CPPCHECK+=	fault.c
CPPCHECK+=	fuzz-config.c
CPPCHECK+=	fuzz-message.c
CPPCHECK+=	macro.c
CPPCHECK+=	maildir.c
CPPCHECK+=	match.c
CPPCHECK+=	mdsort.c
CPPCHECK+=	message.c
CPPCHECK+=	t.c
CPPCHECK+=	time.c
CPPCHECK+=	util.c

DISTFILES+=	CHANGELOG.md
DISTFILES+=	GNUmakefile
DISTFILES+=	LICENSE
DISTFILES+=	Makefile
DISTFILES+=	README.md
DISTFILES+=	cdefs.h
DISTFILES+=	compat-arc4random.c
DISTFILES+=	compat-errc.c
DISTFILES+=	compat-pledge.c
DISTFILES+=	compat-queue.h
DISTFILES+=	compat-reallocarray.c
DISTFILES+=	compat-strlcpy.c
DISTFILES+=	compat-utimensat.c
DISTFILES+=	compat-warnc.c
DISTFILES+=	conf.c
DISTFILES+=	conf.h
DISTFILES+=	configure
DISTFILES+=	decode.c
DISTFILES+=	decode.h
DISTFILES+=	expr.c
DISTFILES+=	extern.h
DISTFILES+=	fault.c
DISTFILES+=	fault.h
DISTFILES+=	fuzz-config.c
DISTFILES+=	fuzz-message.c
DISTFILES+=	libks/arithmetic.h
DISTFILES+=	libks/buffer.c
DISTFILES+=	libks/buffer.h
DISTFILES+=	libks/vector.c
DISTFILES+=	libks/vector.h
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
DISTFILES+=	t.c
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

TESTFLAGS?=	-Tfault

all: ${PROG_mdsort}

${PROG_mdsort}: ${OBJS_mdsort}
	${CC} ${DEBUG} -o ${PROG_mdsort} ${OBJS_mdsort} ${LDFLAGS}

${PROG_test}: ${OBJS_test}
	${CC} ${DEBUG} -o ${PROG_test} ${OBJS_test} ${LDFLAGS}

${PROG_fuzz-config}: ${OBJS_fuzz-config}
	${CC} ${DEBUG} -o ${PROG_fuzz-config} ${OBJS_fuzz-config} ${LDFLAGS}

${PROG_fuzz-message}: ${OBJS_fuzz-message}
	${CC} ${DEBUG} -o ${PROG_fuzz-message} ${OBJS_fuzz-message} ${LDFLAGS}

fuzz: ${PROG_fuzz-config} ${PROG_fuzz-message}

clean:
	rm -f ${DEPS_mdsort} ${OBJS_mdsort} ${PROG_mdsort} parse.c y.tab.h \
		${DEPS_test} ${OBJS_test} ${PROG_test} \
		${DEPS_fuzz-config} ${OBJS_fuzz-config} ${PROG_fuzz-config} \
		${DEPS_fuzz-message} ${OBJS_fuzz-message} ${PROG_fuzz-message}
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
	${INSTALL} ${PROG_mdsort} ${DESTDIR}${BINDIR}
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
	cd ${.CURDIR} && clang-tidy --quiet ${CLANGTIDY} -- ${CPPFLAGS}
.PHONY: lint-clang-tidy

lint-cppcheck:
	cd ${.CURDIR} && cppcheck --quiet --enable=all --error-exitcode=1 \
		--max-configs=2 --suppress-xml=cppcheck-suppressions.xml \
		${CPPCHECK}
.PHONY: lint-cppcheck

test: ${PROG_mdsort} test-${PROG_test}
	${MAKE} -C ${.CURDIR}/tests \
		"COREDUMP=${.CURDIR}" \
		"MDSORT=${.OBJDIR}/${PROG_mdsort}" \
		"TESTFLAGS=${TESTFLAGS}"
.PHONY: test

test-${PROG_test}: ${PROG_test}
	${EXEC} ./${PROG_test}

-include ${DEPS_mdsort}
