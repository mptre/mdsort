#include <limits.h>	/* NAME_MAX, PATH_MAX */
#include <stddef.h>	/* size_t */

#ifdef HAVE_QUEUE
#  include <sys/queue.h>
#else
#  include "compat-queue.h"
#endif

struct environment;

struct message_flags {
	unsigned int	mf_upper;
	unsigned int	mf_lower;
};

struct message {
	char			 me_path[PATH_MAX];	/* full path */
	char			 me_name[NAME_MAX + 1];	/* file name */
	const char		*me_body;
	char			*me_buf;
	char			*me_buf_dec;		/* decoded body */
	int			 me_fd;
	unsigned int		 me_flags;
#define MESSAGE_FLAG_ATTACHMENT	0x00000001u

	struct message_flags	 me_mflags;		/* maildir flags */

	struct {
		struct header	*h_v;
		size_t		 h_nmemb;
		size_t		 h_size;
	} me_headers;

	struct message_list	*me_attachments;

	TAILQ_ENTRY(message)	 me_entry;
};

TAILQ_HEAD(message_list, message);

char	*message_flags_str(const struct message_flags *, char *, size_t);
int	 message_flags_isset(const struct message_flags *, char);
int	 message_flags_clr(struct message_flags *, char);
int	 message_flags_set(struct message_flags *, char);

struct message	*message_parse(const char *, int, const char *);
void		 message_free(struct message *);

int	message_write(struct message *, int);

int				 message_get_fd(struct message *,
    const struct environment *, int);
const char			*message_get_body(struct message *);
const struct string_list	*message_get_header(const struct message *,
    const char *);
const char			*message_get_header1(const struct message *,
    const char *);

void	message_set_header(struct message *, const char *, char *);
int	message_set_file(struct message *, const char *, const char *, int);

struct message_list	*message_get_attachments(struct message *);
void			 message_list_free(struct message_list *);
