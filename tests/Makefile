MDSORT=		${.CURDIR}/../mdsort
TESTFLAGS+=	-b ${MDSORT}

TESTS=	basic.sh conf.sh dry.sh

.SUFFIXES: .sh .out

.sh.out:
	sh ${.CURDIR}/run.sh ${TESTFLAGS} ${.CURDIR}/${<}

all: ${TESTS:.sh=.out}
