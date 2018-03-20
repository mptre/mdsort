include ${.CURDIR}/Makefile.inc

PROG=	mdsort

SRC=	compat-arc4random.c compat-pledge.c compat-reallocarray.c \
	maildir.c message.c mdsort.c parse.c rule.c
OBJ=	${SRC:.c=.o}
DEP=	${SRC:.c=.d}

CFLAGS+=	${DEBUG}
CPPFLAGS+=	-I${.CURDIR}

all: ${PROG}

${PROG}: ${OBJ}
	${CC} ${DEBUG} -o ${PROG} ${OBJ} ${LDFLAGS}

clean:
	rm -f ${DEP} ${OBJ} ${PROG}
.PHONY: clean

test: ${PROG}
	@${MAKE} -C ${.CURDIR}/tests
.PHONY: test

-include ${DEP}
