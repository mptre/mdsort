#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "libks/arena.h"

#include "decode.h"

struct test_context {
	struct {
		struct arena	*scratch;
	} arena;
};

#define test_base64_decode(str, exp)					\
	error |= test_base64_decode0(&c, (str), (exp), "base64_decode", __LINE__);\
	if (xflag && error) goto out;
static int	test_base64_decode0(struct test_context *, const char *,
    const char *, const char *,
    int);

#define test_rfc2047_decode(str, exp)					\
	error |= test_rfc2047_decode0(&c, (str), (exp), "rfc2047_decode", __LINE__);\
	if (xflag && error) goto out;
static int	test_rfc2047_decode0(struct test_context *, const char *,
    const char *, const char *,
    int);

static void	usage(void) __attribute__((noreturn));

int
main(int argc, char *argv[])
{
	struct test_context c = {0};
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

	c.arena.scratch = arena_alloc();

	test_base64_decode("", "");
	test_base64_decode("Zg==", "f");
	test_base64_decode("Zm8=", "fo");
	test_base64_decode("Zm9v", "foo");
	test_base64_decode("Zm9vYg==", "foob");
	test_base64_decode("Zm9vYmE=", "fooba");
	test_base64_decode("Zm9vYmFy", "foobar");
	test_base64_decode(
	    "YWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFh"
	    "YWFhCmJiYmJiYmJiYmJiYmJiYmJiYmJiYmJiYmJiYmJiYmJiYmJiYmJiYmJiYmJiYmJiYmJiYmJi"
	    "YmJiYmJiYgo=",
	    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n"
	    "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\n");

	test_rfc2047_decode("", "");
	test_rfc2047_decode("a", "a");
	test_rfc2047_decode("=?UTF-8?B?Zm9vYmFy?=", "foobar");
	test_rfc2047_decode("=?UTF-8?Q?a?=", "a");
	test_rfc2047_decode("(=?UTF-8?Q?a?=)", "(a)");
	test_rfc2047_decode("(=?UTF-8?Q?a?= b)", "(a b)");
	test_rfc2047_decode("(=?UTF-8?Q?a?= =?UTF-8?Q?b?=)", "(ab)");
	test_rfc2047_decode("(=?UTF-8?Q?a?=  =?UTF-8?Q?b?=)", "(ab)");
	test_rfc2047_decode("(=?UTF-8?Q?a?= \n =?UTF-8?Q?b?=)", "(ab)");
	test_rfc2047_decode("(=?UTF-8?Q?a_b?=)", "(a b)");
	test_rfc2047_decode("(=?UTF-8?Q?a?= =?ISO-8859-2?Q?_b?=)", "(a b)");
	test_rfc2047_decode("=?UTF-8?", "=?UTF-8?");
	test_rfc2047_decode("=?UTF-8?Q", "=?UTF-8?Q");

out:
	arena_free(c.arena.scratch);
	return error;
}

static void
usage(void)
{
	fprintf(stderr, "usage: t [-x]\n");
	exit(1);
}

static int
test_rfc2047_decode0(struct test_context *c, const char *str, const char *exp,
    const char *fun, int lno)
{
	const char *act;
	int error = 0;

	arena_scope(c->arena.scratch, s);

	act = rfc2047_decode(str, &s);
	if (strcmp(exp, act) != 0) {
		fprintf(stderr, "%s:%d:\n\texp %s\n\tgot %s\n",
		    fun, lno, exp, act);
		error = 1;
	}
	return error;
}

static int
test_base64_decode0(struct test_context *c, const char *str, const char *exp,
    const char *fun, int lno)
{
	const char *act;
	int error = 0;

	arena_scope(c->arena.scratch, s);

	act = base64_decode(str, &s);
	if (strcmp(exp, act) != 0) {
		fprintf(stderr, "%s:%d:\n\texp %s\n\tgot %s\n",
		    fun, lno, exp, act);
		error = 1;
	}
	return error;
}
