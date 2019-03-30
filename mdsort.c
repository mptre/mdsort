#include "config.h"

#include <err.h>
#include <errno.h>
#include <paths.h>
#include <pwd.h>
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

static const char *defaultconf(const struct environment *);
static void readenv(struct environment *);
static __dead void usage(void);

int
main(int argc, char *argv[])
{
	struct match_list matches = MATCH_LIST_INITIALIZER(matches);
	struct environment env;
	struct config_list *config;
	struct config *conf;
	struct maildir *md;
	struct message *msg;
	const char *path;
	unsigned int mdflags = MAILDIR_WALK | MAILDIR_ROOT;
	int c;
	int error = 0;
	int dostdin = 0;
	int verbose = 0;

	if (pledge("stdio rpath wpath cpath fattr getpw", NULL) == -1)
		err(1, "pledge");

	memset(&env, 0, sizeof(env));

	while ((c = getopt(argc, argv, "dnvf:")) != -1)
		switch (c) {
		case 'd':
			env.options |= OPTION_DRYRUN;
			break;
		case 'f':
			env.confpath = optarg;
			break;
		case 'n':
			env.options |= OPTION_SYNTAX;
			break;
		case 'v':
			verbose++;
			break;
		default:
			usage();
		}
	argc -= optind;
	argv += optind;
	if (argc > 0) {
		if (strcmp(*argv, "-"))
			usage();
		argc--;
		argv++;
		dostdin = 1;
		mdflags |= MAILDIR_STDIN;
	}
	if (argc > 0)
		usage();
	if ((env.options & OPTION_DRYRUN) && verbose < 1)
		verbose = 1;
	log_init(verbose);

	/* Extract mandatory data from the current environment. */
	readenv(&env);

	if (pledge("stdio rpath wpath cpath fattr", NULL) == -1)
		err(1, "pledge");

	if (env.confpath == NULL)
		env.confpath = defaultconf(&env);
	config = config_parse(env.confpath, &env);
	if (config == NULL) {
		error = 1;
		goto done;
	}
	if (env.options & OPTION_SYNTAX)
		goto done;

	TAILQ_FOREACH(conf, config, entry) {
		if (conf->maildir != NULL && dostdin) {
			log_debug("%s: %s: skip\n",
			    __func__, conf->maildir);
			continue;
		} else if (conf->maildir == NULL && !dostdin) {
			log_debug("%s: <stdin>: skip\n", __func__);
			continue;
		}

		md = maildir_open(conf->maildir, mdflags, &env);
		if (md == NULL) {
			error = 1;
			continue;
		}

		while ((path = maildir_walk(md))) {
			msg = message_parse(path);
			if (msg == NULL) {
				error = 1;
				continue;
			}

			matches_clear(&matches);
			if (expr_eval(conf->expr, &matches, msg, &env)) {
				message_free(msg);
				continue;
			}
			if (matches_interpolate(&matches, msg)) {
				error = 1;
				message_free(msg);
				continue;
			}

			log_info("%s -> %s\n",
			    dostdin ? "<stdin>" : path, matches.ml_path);
			if (env.options & OPTION_DRYRUN) {
				matches_inspect(&matches, stdout, &env);
				message_free(msg);
				continue;
			}

			if (matches_exec(&matches, md, msg, &env))
				error = 1;
			message_free(msg);
		}
		maildir_close(md);
	}

done:
	while (config != NULL && (conf = TAILQ_FIRST(config)) != NULL) {
		TAILQ_REMOVE(config, conf, entry);
		expr_free(conf->expr);
		free(conf->maildir);
		free(conf);
	}

	if (error && dostdin)
		return EX_TEMPFAIL;

	return error;
}

static __dead void
usage(void)
{
	fprintf(stderr, "usage: mdsort [-dnv] [-f file] [-]\n");
	exit(1);
}

static const char *
defaultconf(const struct environment *env)
{
	static char buf[PATH_MAX];
	int len, n;

	len = sizeof(buf);
	n = snprintf(buf, len, "%s/.mdsort.conf", env->home);
	if (n < 0 || n >= len)
		errc(1, ENAMETOOLONG, "%s", __func__);
	return buf;
}

static void
readenv(struct environment *env)
{
	struct passwd *pw;
	struct tm *tm;
	char *p;

	if (gethostname(env->hostname, sizeof(env->hostname)) == -1)
		err(1, "gethostname");
	else if ((p = strchr(env->hostname, '.')) != NULL)
		*p = '\0';

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
	if (strlcpy(env->home, p, sizeof(env->home)) >= sizeof(env->home))
		errc(1, ENAMETOOLONG, "%s: HOME", __func__);

	if ((p = getenv("TMPDIR")) == NULL || *p == '\0')
		p = _PATH_TMP;
	if (strlcpy(env->tmpdir, p, sizeof(env->tmpdir)) >= sizeof(env->tmpdir))
		errc(1, ENAMETOOLONG, "%s: TMPDIR", __func__);

	if ((p = getenv("TZ")) == NULL) {
		env->tz_state = TZ_STATE_LOCAL;
	} else {
		if (*p == '\0')
			env->tz_state = TZ_STATE_UTC;
		else
			env->tz_state = TZ_STATE_SET;
		if (strlcpy(env->tz_buf, p, sizeof(env->tz_buf)) >= sizeof(env->tz_buf))
			errc(1, ENAMETOOLONG, "%s: TZ", __func__);
	}

	env->now = time(NULL);
	tm = localtime(&env->now);
	if (tm == NULL)
		err(1, "localtime");
	env->tz_offset = tm->tm_gmtoff;

	log_debug("%s: home=\"%s\", hostname=\"%s\", tmpdir=\"%s\", now=%lld, "
	    "tz_offset=%ld\n", __func__, env->home, env->hostname, env->tmpdir,
	    (long long)env->now, env->tz_offset);
}
