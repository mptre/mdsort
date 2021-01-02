#ifdef DIAGNOSTIC

#define FAULT(probe, ...)	fault((probe), __VA_ARGS__)
#define FAULT_SHUTDOWN()	fault_shutdown()

int fault(const char *, ...);
void fault_shutdown(void);

#else

#define FAULT(probe, ...)	0
#define FAULT_SHUTDOWN()	do {} while (0)

#endif	/* DIAGNOSTIC */
