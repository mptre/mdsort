PROG=	mdsort

SRC=	maildir.c message.c mdsort.c parse.c rule.c
OBJ=	${SRC:.c=.o}
DEP=	${SRC:.c=.d}

CFLAGS+=	-Wall -Wextra -MD -MP
CFLAGS+=	${DEBUG}

# XXX zap
CFLAGS+=	-Werror
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
