#ifdef DIAGNOSTIC

#define FAULT(probe, ret, ...) do {							\
	if (fault((probe), __VA_ARGS__)) {						\
		warnx("fault: %s", (probe));						\
		return (ret);								\
	}										\
} while (0)

#define FAULT_SHUTDOWN() fault_shutdown()

int fault(const char *, ...);
void fault_shutdown(void);

#else

#define FAULT(probe, ...)	do {} while (0)
#define FAULT_SHUTDOWN()	do {} while (0)

#endif	/* DIAGNOSTIC */
