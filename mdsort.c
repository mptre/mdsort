#include "config.h"

#include <err.h>
#include <errno.h>
#include <locale.h>
#include <paths.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "extern.h"

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
static __dead void	 usage(void);

int
main(int argc, char *argv[])
{
	struct config_list config;
	struct environment env;
	struct match_list matches;
	struct maildir_entry me;
	struct config *conf;
	struct maildir *md;
	struct message *msg;
	int error = 0;
	int reject = 0;
	int c, w;

	if (pledge("stdio rpath wpath cpath fattr getpw proc exec", NULL) == -1)
		err(1, "pledge");

	setlocale(LC_CTYPE, "");

	config_init(&config);
	memset(&env, 0, sizeof(env));
	TAILQ_INIT(&matches);

	while ((c = getopt(argc, argv, "D:df:nv")) != -1)
		switch (c) {
		case 'D': {
			char *eq;

			if ((eq = strchr(optarg, '=')) == NULL)
				errx(1, "missing macro separator: %s", optarg);
			*eq = '\0';
			if (macros_insert(&config.cl_macros, optarg, &eq[1],
			    MACRO_FLAG_CONST | MACRO_FLAG_STICKY, 0))
				errx(1, "invalid macro: %s", optarg);
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
			usage();
		}
	argc -= optind;
	argv += optind;
	if (argc > 0) {
		if (strcmp(*argv, "-") != 0)
			usage();
		argc--;
		argv++;
		env.ev_options |= OPTION_STDIN;
	}
	if (argc > 0)
		usage();
	if ((env.ev_options & OPTION_DRYRUN) && log_level < 1)
		log_level = 1;

	readenv(&env);

	if (pledge("stdio rpath wpath cpath fattr proc exec", NULL) == -1)
		err(1, "pledge");

	if (env.ev_confpath == NULL)
		env.ev_confpath = defaultconf(env.ev_home);
	if (config_parse(&config, env.ev_confpath, &env)) {
		error = 1;
		goto out;
	}

	/* Drop exec privilegies unless needed. */
	if (!config_has_exec(&config, &env))
		if (pledge("stdio rpath wpath cpath fattr", NULL) == -1)
			err(1, "pledge");

	if (env.ev_options & OPTION_SYNTAX)
		goto out;

	TAILQ_FOREACH(conf, &config.cl_list, entry) {
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

			while ((w = maildir_walk(md, &me))) {
				struct expr_eval_arg ea = {
					.ea_ml	= &matches,
					.ea_env	= &env,
				};

				if (w == -1) {
					error = 1;
					break;
				}

				msg = message_parse(me.e_dir, me.e_dirfd,
				    me.e_path);
				if (msg == NULL) {
					error = 1;
					goto loop;
				}
				ea.ea_msg = msg;

				switch (expr_eval(conf->expr, &ea)) {
				case EXPR_ERROR:
					error = 1;
					/* FALLTHROUGH */
				case EXPR_NOMATCH:
					goto loop;
				}

				if (matches_interpolate(&matches)) {
					error = 1;
					goto loop;
				}

				if (matches_inspect(&matches, &env)) {
					/* Dry run, we're done. */
					goto loop;
				}

				if (matches_exec(&matches, md, &reject, &env))
					error = 1;
loop:
				message_free(msg);
				matches_clear(&matches);
			}
			maildir_close(md);
		}
	}

out:
	config_free(&config);

	FAULT_SHUTDOWN();

	if (env.ev_options & OPTION_STDIN) {
		if (error)
			return EX_TEMPFAIL;
		if (reject)
			return EX_PERMFAIL;
	}

	return error;
}

static __dead void
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
config_has_exec(const struct config_list *config, const struct environment *env)
{
	const struct config *conf;
	int nexec = 0;

	TAILQ_FOREACH(conf, &config->cl_list, entry) {
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
	struct passwd *pw;
	struct tm *tm;
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
	tm = localtime(&env->ev_now);
	if (tm == NULL)
		err(1, "localtime");
	env->ev_tz.t_offset = tm->tm_gmtoff;

	env->ev_pid = getpid();

	log_debug("%s: home=\"%s\", hostname=\"%s\", tmpdir=\"%s\", now=%lld, "
	    "tz_offset=%ld\n", __func__, env->ev_home, env->ev_hostname,
	    env->ev_tmpdir, (long long)env->ev_now, env->ev_tz.t_offset);
}
