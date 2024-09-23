include ${.CURDIR}/config.mk

VERSION=	11.6.0

SRCS+=	arena-buffer.c
SRCS+=	arena-vector.c
SRCS+=	arena.c
SRCS+=	arithmetic.c
SRCS+=	buffer.c
SRCS+=	compat-arc4random.c
SRCS+=	compat-errc.c
SRCS+=	compat-pledge.c
SRCS+=	compat-strlcpy.c
SRCS+=	compat-warnc.c
SRCS+=	conf.c
SRCS+=	consistency.c
SRCS+=	date-time.c
SRCS+=	decode.c
SRCS+=	environment.c
SRCS+=	expr.c
SRCS+=	fault.c
SRCS+=	log.c
SRCS+=	macro.c
SRCS+=	maildir.c
SRCS+=	match.c
SRCS+=	message.c
SRCS+=	parse.c
SRCS+=	string-list.c
SRCS+=	tmp.c
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

KNFMT+=	compat-arc4random.c
KNFMT+=	compat-pledge.c
KNFMT+=	conf.c
KNFMT+=	conf.h
KNFMT+=	date-time.c
KNFMT+=	date-time.h
KNFMT+=	decode.c
KNFMT+=	decode.h
KNFMT+=	environment.c
KNFMT+=	environment.h
KNFMT+=	expr.c
KNFMT+=	expr.h
KNFMT+=	fault.c
KNFMT+=	fault.h
KNFMT+=	fuzz-config.c
KNFMT+=	fuzz-message.c
KNFMT+=	log.c
KNFMT+=	log.h
KNFMT+=	macro.c
KNFMT+=	macro.h
KNFMT+=	maildir.c
KNFMT+=	maildir.h
KNFMT+=	match.c
KNFMT+=	match.h
KNFMT+=	mdsort.c
KNFMT+=	message.c
KNFMT+=	message.h
KNFMT+=	string-list.c
KNFMT+=	string-list.h
KNFMT+=	t.c
KNFMT+=	util.c
KNFMT+=	util.h

CLANGTIDY+=	compat-arc4random.c
CLANGTIDY+=	compat-pledge.c
CLANGTIDY+=	conf.c
CLANGTIDY+=	conf.h
CLANGTIDY+=	date-time.c
CLANGTIDY+=	date-time.h
CLANGTIDY+=	decode.c
CLANGTIDY+=	decode.h
CLANGTIDY+=	environment.c
CLANGTIDY+=	environment.h
CLANGTIDY+=	expr.c
CLANGTIDY+=	expr.h
CLANGTIDY+=	fault.c
CLANGTIDY+=	fault.h
CLANGTIDY+=	fuzz-config.c
CLANGTIDY+=	fuzz-message.c
CLANGTIDY+=	log.c
CLANGTIDY+=	log.h
CLANGTIDY+=	macro.c
CLANGTIDY+=	macro.h
CLANGTIDY+=	maildir.c
CLANGTIDY+=	maildir.h
CLANGTIDY+=	match.c
CLANGTIDY+=	match.h
CLANGTIDY+=	mdsort.c
CLANGTIDY+=	message.c
CLANGTIDY+=	message.h
CLANGTIDY+=	string-list.c
CLANGTIDY+=	string-list.h
CLANGTIDY+=	t.c
CLANGTIDY+=	util.c
CLANGTIDY+=	util.h

CPPCHECK+=	compat-arc4random.c
CPPCHECK+=	compat-pledge.c
CPPCHECK+=	conf.c
CPPCHECK+=	date-time.c
CPPCHECK+=	decode.c
CPPCHECK+=	environment.c
CPPCHECK+=	expr.c
CPPCHECK+=	fault.c
CPPCHECK+=	fuzz-config.c
CPPCHECK+=	fuzz-message.c
CPPCHECK+=	log.c
CPPCHECK+=	macro.c
CPPCHECK+=	maildir.c
CPPCHECK+=	match.c
CPPCHECK+=	mdsort.c
CPPCHECK+=	message.c
CPPCHECK+=	string-list.c
CPPCHECK+=	t.c
CPPCHECK+=	util.c

CPPCHECKFLAGS+=	--quiet
CPPCHECKFLAGS+=	--check-level=exhaustive
CPPCHECKFLAGS+=	--enable=all
CPPCHECKFLAGS+=	--error-exitcode=1
CPPCHECKFLAGS+=	--max-configs=2
CPPCHECKFLAGS+=	--suppress-xml=cppcheck-suppressions.xml
CPPCHECKFLAGS+=	${CPPFLAGS}

IWYUFLAGS+=	-DDIAGNOSTIC
IWYUFLAGS+=	-d config.h
IWYUFLAGS+=	${CPPFLAGS}

SHLINT+=	configure
SHLINT+=	tests/action-add-header.sh
SHLINT+=	tests/action-attachment.sh
SHLINT+=	tests/action-break.sh
SHLINT+=	tests/action-discard.sh
SHLINT+=	tests/action-exec.sh
SHLINT+=	tests/action-flag.sh
SHLINT+=	tests/action-flags.sh
SHLINT+=	tests/action-label.sh
SHLINT+=	tests/action-move.sh
SHLINT+=	tests/action-pass.sh
SHLINT+=	tests/action-reject.sh
SHLINT+=	tests/basic.sh
SHLINT+=	tests/conf.sh
SHLINT+=	tests/dry.sh
SHLINT+=	tests/macros.sh
SHLINT+=	tests/maildir.sh
SHLINT+=	tests/match-all.sh
SHLINT+=	tests/match-attachment.sh
SHLINT+=	tests/match-body-b64.sh
SHLINT+=	tests/match-body-qp.sh
SHLINT+=	tests/match-body.sh
SHLINT+=	tests/match-command.sh
SHLINT+=	tests/match-date.sh
SHLINT+=	tests/match-header-b64.sh
SHLINT+=	tests/match-header-qp.sh
SHLINT+=	tests/match-header.sh
SHLINT+=	tests/match-isdirectory.sh
SHLINT+=	tests/match-logical.sh
SHLINT+=	tests/match-new.sh
SHLINT+=	tests/match-old.sh
SHLINT+=	tests/stdin.sh
SHLINT+=	tests/util.sh

SHELLCHECKFLAGS+=	-f gcc
SHELLCHECKFLAGS+=	-s ksh
SHELLCHECKFLAGS+=	-o add-default-case
SHELLCHECKFLAGS+=	-o avoid-nullary-conditions
SHELLCHECKFLAGS+=	-o quote-safe-variables
SHELLCHECKFLAGS+=	-o require-variable-braces

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
	set -e; p=mdsort-${VERSION}; cd ${.CURDIR}; \
	git archive --output $$p.tar.gz --prefix $$p/ v${VERSION}; \
	sha256 $$p.tar.gz >$$p.sha256
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
.PHONY: lint

lint-clang-tidy:
	cd ${.CURDIR} && echo ${CLANGTIDY} | xargs printf '%s\n' | \
		xargs -I{} clang-tidy --quiet {} -- ${CPPFLAGS}
.PHONY: lint-clang-tidy

lint-cppcheck:
	cd ${.CURDIR} && cppcheck ${CPPCHECKFLAGS} ${CPPCHECK}
.PHONY: lint-cppcheck

lint-include-what-you-use:
	cd ${.CURDIR} && iwyu-filter ${IWYUFLAGS} -- ${CPPCHECK}
.PHONY: lint-include-what-you-use

lint-shellcheck:
	cd ${.CURDIR} && shellcheck ${SHELLCHECKFLAGS} ${SHLINT}
.PHONY: lint-shellcheck

test: ${PROG_mdsort} test-${PROG_test}
	${MAKE} -C ${.CURDIR}/tests \
		"COREDUMP=${.CURDIR}" \
		"MDSORT=${.OBJDIR}/${PROG_mdsort}" \
		"TESTFLAGS=${TESTFLAGS}"
.PHONY: test

test-${PROG_test}: ${PROG_test}
	${EXEC} ./${PROG_test}

-include ${DEPS_mdsort}
-include ${DEPS_test}
-include ${DEPS_fuzz-config}
-include ${DEPS_fuzz-message}
