struct arena_scope;
struct environment;
struct message;

/* Flags passed to maildir_open(). */
#define MAILDIR_WALK	0x00000001u
#define MAILDIR_STDIN	0x00000002u

struct maildir_entry {
	const char	*dir;
	const char	*path;
	int		 dirfd;
};

struct maildir	*maildir_open(const char *, unsigned int,
    const struct environment *, struct arena_scope *);
void		 maildir_close(struct maildir *);

int	maildir_walk(struct maildir *, struct maildir_entry *);
int	maildir_move(const struct maildir *, const struct maildir *,
    struct message *, const struct environment *);
int	maildir_unlink(const struct maildir *, const char *);
int	maildir_write(struct maildir *, struct message *,
    const struct environment *);

int	maildir_cmp(const struct maildir *, const struct maildir *);
