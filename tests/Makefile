MDSORT=	${.CURDIR}/../mdsort

TESTS=	basic.sh conf.sh dry.sh flag.sh

.SUFFIXES: .sh .out

.sh.out:
	sh ${.CURDIR}/run.sh -b ${MDSORT} ${TESTFLAGS} ${.CURDIR}/${<}

all: ${TESTS:.sh=.out}
