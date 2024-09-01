#include "config.h"

#include <err.h>
#include <errno.h>
#include <limits.h>	/* PATH_MAX */
#include <locale.h>
#include <paths.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "libks/arena.h"
#include "libks/vector.h"

#include "conf.h"
#include "environment.h"
#include "expr.h"
#include "fault.h"
#include "log.h"
#include "macro.h"
#include "maildir.h"
#include "match.h"
#include "message.h"
#include "string-list.h"
#include "util.h"

/*
 * When reading messages from stdin and an error occurred, always exit with
 * a code indicating temporary failure as opposed of permanent failure.
 * The actual number is a convention established by sendmail.
 */
#define EX_TEMPFAIL	75

/*
 * When reading messages from stdin and a reject rule is matched, always exit
 * non-zero indicating permanent failure.
 * The actual number is quite arbitrary but something that at least OpenSMTPD
 * will interpret as a permanent failure during MDA delivery.
 */
#define EX_PERMFAIL	1

static int		 config_has_exec(const struct config_list *,
    const struct environment *);
static const char	*defaultconf(const char *);
static int		 maildir_skip(const char *, const struct environment *);
static void		 readenv(struct environment *);
static void		 usage(void)
	__attribute__((__noreturn__));

static int	handle_message(struct expr *, struct maildir *,
    const struct maildir_entry *, int *, const struct environment *,
    struct arena *);

int
main(int argc, char *argv[])
{
	struct arena *scratch = NULL;
	struct config_list cl;
	struct environment env;
	struct maildir_entry me;
	struct maildir *md;
	size_t i;
	int dousage = 0;
	int error = 0;
	int reject = 0;
	int c;

	if (pledge("stdio rpath wpath cpath fattr getpw proc exec", NULL) == -1)
		err(1, "pledge");

	setlocale(LC_CTYPE, "");

	config_list_init(&cl);
	environment_init(&env);

	scratch = arena_alloc();
	arena_scope(scratch, s);

	while ((c = getopt(argc, argv, "D:df:nv")) != -1) {
		switch (c) {
		case 'D': {
			char *eq;

			if ((eq = strchr(optarg, '=')) == NULL) {
				warnx("missing macro separator: %s", optarg);
				error = 1;
				goto out;
			}
			*eq = '\0';
			if (macros_insert(cl.cl_macros, optarg, &eq[1],
			    MACRO_FLAG_CONST | MACRO_FLAG_STICKY, 0)) {
				warnx("invalid macro: %s", optarg);
				error = 1;
				goto out;
			}
			break;
		}
		case 'd':
			env.ev_options |= OPTION_DRYRUN;
			break;
		case 'f':
			env.ev_confpath = optarg;
			break;
		case 'n':
			env.ev_options |= OPTION_SYNTAX;
			break;
		case 'v':
			log_level++;
			break;
		default:
			dousage = 1;
			goto out;
		}
	}
	argc -= optind;
	argv += optind;
	if (argc > 0) {
		if (strcmp(*argv, "-") != 0) {
			dousage = 1;
			goto out;
		}
		argc--;
		argv++;
		env.ev_options |= OPTION_STDIN;
	}
	if (argc > 0) {
		dousage = 1;
		goto out;
	}
	if ((env.ev_options & OPTION_DRYRUN) && log_level < 1)
		log_level = 1;

	readenv(&env);

	if (pledge("stdio rpath wpath cpath fattr proc exec", NULL) == -1)
		err(1, "pledge");

	if (env.ev_confpath == NULL)
		env.ev_confpath = defaultconf(env.ev_home);
	if (config_list_parse(&cl, env.ev_confpath, &env, &s)) {
		error = 1;
		goto out;
	}

	/* Drop exec privilegies unless needed. */
	if (!config_has_exec(&cl, &env)) {
		if (pledge("stdio rpath wpath cpath fattr", NULL) == -1)
			err(1, "pledge");
	}

	if (env.ev_options & OPTION_SYNTAX)
		goto out;

	for (i = 0; i < VECTOR_LENGTH(cl.cl_list); i++) {
		struct config *conf = &cl.cl_list[i];
		const struct string *str;

		TAILQ_FOREACH(str, conf->paths, entry) {
			const char *path = str->val;
			unsigned int flags;

			if (maildir_skip(path, &env))
				continue;

			flags = MAILDIR_WALK;
			if (isstdin(path))
				flags |= MAILDIR_STDIN;
			md = maildir_open(path, flags, &env);
			if (md == NULL) {
				error = 1;
				continue;
			}

			for (;;) {
				int w;

				w = maildir_walk(md, &me);
				if (w == 0)
					break;
				if (w == -1) {
					error = 1;
					break;
				}

				if (handle_message(conf->expr, md,
				    &me, &reject, &env, scratch))
					error = 1;
			}
			maildir_close(md);
		}
	}

out:
	config_list_free(&cl);
	arena_free(scratch);
	FAULT_SHUTDOWN();

	if (dousage)
		usage();

	if (env.ev_options & OPTION_STDIN) {
		if (error)
			return EX_TEMPFAIL;
		if (reject)
			return EX_PERMFAIL;
	}

	return error;
}

static void
usage(void)
{
	fprintf(stderr, "usage: mdsort [-dnv] [-D macro=value] [-f file] "
	    "[-]\n");
	exit(1);
}

/*
 * Returns non-zero if any of the expressions associated with the given
 * configuration requires execution of external commands.
 */
static int
config_has_exec(const struct config_list *cl, const struct environment *env)
{
	size_t i;
	int nexec = 0;

	for (i = 0; i < VECTOR_LENGTH(cl->cl_list); i++) {
		const struct config *conf = &cl->cl_list[i];
		const struct string *str;

		TAILQ_FOREACH(str, conf->paths, entry) {
			if (maildir_skip(str->val, env))
				continue;
			if (expr_count(conf->expr, EXPR_TYPE_COMMAND) > 0)
				return 1;
			if (expr_count(conf->expr, EXPR_TYPE_EXEC) > 0)
				nexec++;
			/* All maildir paths share the same expression. */
			break;
		}
	}

	return (env->ev_options & OPTION_DRYRUN) == 0 && nexec > 0;
}

static const char *
defaultconf(const char *home)
{
	static char path[PATH_MAX];
	size_t siz = sizeof(path);
	int n;

	n = snprintf(path, siz, "%s/.mdsort.conf", home);
	if (n < 0 || (size_t)n >= siz)
		errc(1, ENAMETOOLONG, "%s", __func__);
	return path;
}

/*
 * Returns non-zero if the given maildir must be skipped. Maildirs must be
 * skipped when running in stdin mode and vice versa.
 */
static int
maildir_skip(const char *path, const struct environment *env)
{
	int dostdin = env->ev_options & OPTION_STDIN;

	return (dostdin && !isstdin(path)) || (!dostdin && isstdin(path));
}

static void
readenv(struct environment *env)
{
	const struct passwd *pw;
	const struct tm *tm;
	const char *p;
	char *dot;
	size_t siz;

	if (gethostname(env->ev_hostname, sizeof(env->ev_hostname)) == -1)
		err(1, "gethostname");
	else if ((dot = strchr(env->ev_hostname, '.')) != NULL)
		*dot = '\0';

	if ((p = getenv("HOME")) == NULL || *p == '\0') {
		log_debug("%s: HOME: unset or empty\n", __func__);
		pw = getpwuid(getuid());
		if (pw != NULL)
			p = pw->pw_dir;
		else
			p = NULL;
	}
	if (p == NULL)
		errx(1, "%s: cannot find home directory", __func__);
	siz = sizeof(env->ev_home);
	if (strlcpy(env->ev_home, p, siz) >= siz)
		errc(1, ENAMETOOLONG, "%s: HOME", __func__);

	if ((p = getenv("TMPDIR")) == NULL || *p == '\0')
		p = _PATH_TMP;
	siz = sizeof(env->ev_tmpdir);
	if (strlcpy(env->ev_tmpdir, p, siz) >= siz)
		errc(1, ENAMETOOLONG, "%s: TMPDIR", __func__);

	if ((p = getenv("TZ")) == NULL) {
		env->ev_tz.t_state = TZ_STATE_LOCAL;
	} else {
		if (*p == '\0')
			env->ev_tz.t_state = TZ_STATE_UTC;
		else
			env->ev_tz.t_state = TZ_STATE_SET;
		siz = sizeof(env->ev_tz.t_buf);
		if (strlcpy(env->ev_tz.t_buf, p, siz) >= siz)
			errc(1, ENAMETOOLONG, "%s: TZ", __func__);
	}

	env->ev_now = time(NULL);
	tm = localtime((const time_t *)&env->ev_now);
	if (tm == NULL)
		err(1, "localtime");
	env->ev_tz.t_offset = tm->tm_gmtoff;

	env->ev_pid = getpid();

	log_debug("%s: home=\"%s\", hostname=\"%s\", tmpdir=\"%s\", now=%lld, "
	    "tz_offset=%ld\n", __func__, env->ev_home, env->ev_hostname,
	    env->ev_tmpdir, (long long)env->ev_now, env->ev_tz.t_offset);
}

static int
handle_message(struct expr *expr, struct maildir *md,
    const struct maildir_entry *me, int *reject, const struct environment *env,
    struct arena *scratch)
{
	struct match_list matches;
	struct message *msg;
	int error = 0;

	msg = message_parse(me->dir, me->dirfd, me->path);
	if (msg == NULL)
		return 1;

	arena_scope(scratch, s);

	TAILQ_INIT(&matches);

	struct expr_eval_arg ea = {
		.ea_ml		= &matches,
		.ea_msg		= msg,
		.ea_scope	= &s,
		.ea_env		= env,
	};
	switch (expr_eval(expr, &ea)) {
	case EXPR_MATCH:
		break;
	case EXPR_NOMATCH:
		goto out;
	case EXPR_ERROR:
		error = 1;
		goto out;
	}

	if (matches_interpolate(&matches)) {
		error = 1;
		goto out;
	}
	if (matches_inspect(&matches, env, scratch)) {
		/* Dry run, we're done. */
		goto out;
	}
	switch (matches_exec(&matches, md, env)) {
	case MATCH_EXEC_SUCCESS:
		break;
	case MATCH_EXEC_REJECTED:
		*reject = 1;
		break;
	case MATCH_EXEC_ERROR:
		error = 1;
		break;
	}

out:
	matches_clear(&matches);
	message_free(msg);
	return error;
}
