TESTS+=	action-attachment.sh
TESTS+=	action-break.sh
TESTS+=	action-discard.sh
TESTS+=	action-exec.sh
TESTS+=	action-flag.sh
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
TESTS+=	match-date.sh
TESTS+=	match-header.sh
TESTS+=	match-isdirectory.sh
TESTS+=	match-logical.sh
TESTS+=	match-new.sh
TESTS+=	match-old.sh
TESTS+=	stdin.sh

.SUFFIXES: .sh .fake

.sh.fake:
	sh ${.CURDIR}/t.sh ${TESTFLAGS} ${.CURDIR}/util.sh ${.CURDIR}/$<

all: ${TESTS:.sh=.fake}
