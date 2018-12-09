MDSORT=	${.CURDIR}/../mdsort

TESTS=	action-flag.sh \
	action-discard.sh \
	action-pass.sh \
	basic.sh \
	conf.sh \
	dry.sh \
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
	sh ${.CURDIR}/run.sh -b ${MDSORT} ${TESTFLAGS} ${.CURDIR}/${<}

all: ${TESTS:.sh=.out}
