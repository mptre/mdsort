#include <stddef.h>	/* size_t */

struct environment;

struct message_flags {
	unsigned int	mf_upper;
	unsigned int	mf_lower;
};

char	*message_flags_str(const struct message_flags *, char *, size_t);
int	 message_flags_isset(const struct message_flags *, char);
int	 message_flags_clr(struct message_flags *, char);
int	 message_flags_set(struct message_flags *, char);

struct message	*message_parse(const char *, int, const char *);
void		 message_free(struct message *);

int	message_write(struct message *, int);

int			 message_get_fd(struct message *,
    const struct environment *, int);
const char		*message_get_body(struct message *);
char *const		*message_get_header(const struct message *,
    const char *);
const char		*message_get_header1(const struct message *,
    const char *);
const char		*message_get_path(const struct message *);
struct message_flags	*message_get_flags(const struct message *);
const char		*message_get_name(const struct message *);

struct message	**message_get_attachments(struct message *);
void		  message_free_attachments(struct message **);

void	message_set_header(struct message *, const char *, char *);
int	message_set_file(struct message *, const char *, const char *, int);
