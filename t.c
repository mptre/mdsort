#include "config.h"

// XXX
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "decode.h"

#define test_rfc2047_decode(a, b)					\
	error |= test_rfc2047_decode0((a), (b), "rfc2047_decode", __LINE__);\
	if (xflag && error) goto out;
static int	test_rfc2047_decode0(const char *, const char *, const char *,
    int);

static __dead void	usage(void);

int
main(int argc, char *argv[])
{
	int error = 0;
	int xflag = 0;
	int ch;

	while ((ch = getopt(argc, argv, "x")) != -1) {
		switch (ch) {
		case 'x':
			xflag = 1;
			break;
		default:
			usage();
		}
	}

	test_rfc2047_decode("", "");
	test_rfc2047_decode("a", "a");
	test_rfc2047_decode("=?UTF-8?Q?a?=", "a");
	test_rfc2047_decode("(=?UTF-8?Q?a?=)", "(a)");
	test_rfc2047_decode("(=?UTF-8?Q?a?= b)", "(a b)");
	test_rfc2047_decode("(=?UTF-8?Q?a?= =?UTF-8?Q?b?=)", "(ab)");
	test_rfc2047_decode("(=?UTF-8?Q?a?=  =?UTF-8?Q?b?=)", "(ab)");
	test_rfc2047_decode("(=?UTF-8?Q?a?= \n =?UTF-8?Q?b?=)", "(ab)");
	test_rfc2047_decode("(=?UTF-8?Q?a_b?=)", "(a b)");
	test_rfc2047_decode("(=?UTF-8?Q?a?= =?ISO-8859-2?Q?_b?=)", "(a b)");

out:
	return error;
}

static __dead void
usage(void)
{
	fprintf(stderr, "usage: t [-x]\n");
	exit(1);
}

static int
test_rfc2047_decode0(const char *str, const char *exp, const char *fun, int lno)
{
	char *act;
	int error = 0;

	act = rfc2047_decode(str);
	if (strcmp(exp, act) != 0) {
		fprintf(stderr, "%s:%d:\n\texp %s\n\tgot %s\n",
		    fun, lno, exp, act);
		error = 1;
	}
	free(act);
	return error;
}
