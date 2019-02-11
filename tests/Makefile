TESTS=	action-break.sh \
	action-discard.sh \
	action-flag.sh \
	basic.sh \
	conf.sh \
	dry.sh \
	exdev.sh \
	match-all.sh \
	match-attachment.sh \
	match-body.sh \
	match-date.sh \
	match-header.sh \
	match-logical.sh \
	match-new.sh \
	match-old.sh \
	stdin.sh

.SUFFIXES: .sh .out

.sh.out:
	sh ${.CURDIR}/t.sh ${TESTFLAGS} ${.CURDIR}/util.sh ${.CURDIR}/$<

all: ${TESTS:.sh=.out}
