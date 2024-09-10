#define log_info(fmt, ...) do {						\
	if (log_level >= 1)						\
		logit((fmt), __VA_ARGS__);				\
} while (0)

#define log_debug(fmt, ...) do {					\
	if (log_level >= 2)						\
		logit((fmt), __VA_ARGS__);				\
} while (0)

extern int log_level;

void	logit(const char *, ...) __attribute__((format(printf, 1, 2)));
