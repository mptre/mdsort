#include <limits.h>	/* PATH_MAX */
#include <stdint.h>

struct environment {
	char		 ev_home[PATH_MAX];
	char		 ev_tmpdir[PATH_MAX];
	char		 ev_hostname[256];
	const char	*ev_confpath;

	struct {
		enum {
			TZ_STATE_LOCAL,	/* getenv("TZ") == NULL */
			TZ_STATE_UTC,	/* strlen(getenv("TZ")) == 0 */
			TZ_STATE_SET,	/* strlen(getenv("TZ")) > 0 */
		} t_state;
		char	t_buf[256];
		long	t_offset;
	} ev_tz;

	int64_t		 ev_now;
	int32_t		 ev_pid;

	unsigned int	 ev_options;
#define OPTION_DRYRUN	0x00000001u
#define OPTION_SYNTAX	0x00000002u
#define OPTION_STDIN	0x00000004u
};

void	environment_init(struct environment *);
