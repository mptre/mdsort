TESTS+=	action-attachment.sh
TESTS+=	action-break.sh
TESTS+=	action-discard.sh
TESTS+=	action-exec.sh
TESTS+=	action-flag.sh
TESTS+=	action-flags.sh
TESTS+=	action-label.sh
TESTS+=	action-move.sh
TESTS+=	action-pass.sh
TESTS+=	action-reject.sh
TESTS+=	basic.sh
TESTS+=	conf.sh
TESTS+=	dry.sh
TESTS+=	macros.sh
TESTS+=	maildir.sh
TESTS+=	match-all.sh
TESTS+=	match-attachment.sh
TESTS+=	match-body-b64.sh
TESTS+=	match-body-qp.sh
TESTS+=	match-body.sh
TESTS+=	match-command.sh
TESTS+=	match-date.sh
TESTS+=	match-header-b64.sh
TESTS+=	match-header-qp.sh
TESTS+=	match-header.sh
TESTS+=	match-isdirectory.sh
TESTS+=	match-logical.sh
TESTS+=	match-new.sh
TESTS+=	match-old.sh
TESTS+=	stdin.sh

SHLINT+=	${TESTS}
SHLINT+=	util.sh

SHELLCHECKFLAGS+=	-f gcc
SHELLCHECKFLAGS+=	-s ksh
SHELLCHECKFLAGS+=	-o add-default-case
SHELLCHECKFLAGS+=	-o avoid-nullary-conditions
SHELLCHECKFLAGS+=	-o quote-safe-variables

.SUFFIXES: .sh .fake

.sh.fake:
	sh ${.CURDIR}/t.sh ${TESTFLAGS} ${.CURDIR}/util.sh ${.CURDIR}/$<

all: ${TESTS:.sh=.fake}

lint:
	cd ${.CURDIR} && shellcheck ${SHELLCHECKFLAGS} ${SHLINT}
.PHONY: lint
