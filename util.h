#include <stddef.h>	/* size_t */

int	exec(char *const *, int);

char	*pathjoin(char *, size_t, const char *, const char *);
char	*pathslice(const char *, char *, size_t, int, int);

size_t	nspaces(const char *);

int	isstdin(const char *);
