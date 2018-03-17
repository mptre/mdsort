MDSORT=	${.CURDIR}/../mdsort
TESTS=	basic.sh conf.sh dry.sh

.SUFFIXES: .sh .out

.sh.out:
	sh ${.CURDIR}/run.sh ${MDSORT} ${.CURDIR}/${<}

all: ${TESTS:.sh=.out}
