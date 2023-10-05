#include <stddef.h>	/* size_t */

struct environment;

char	*time_format(long long int, char *, size_t);
int	 time_parse(const char *, long long int *, const struct environment *);
