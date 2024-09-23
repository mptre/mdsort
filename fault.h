#ifdef DIAGNOSTIC

struct arena_scope;

#define FAULT(probe)		fault((probe))
#define FAULT_INIT(s)		fault_init((s))
#define FAULT_SHUTDOWN()	fault_shutdown()

int	fault(const char *);
void	fault_init(struct arena_scope *);
void	fault_shutdown(void);

#else

#define FAULT(probe)		0
#define FAULT_INIT(s)		do {} while (0)
#define FAULT_SHUTDOWN()	do {} while (0)

#endif	/* DIAGNOSTIC */
