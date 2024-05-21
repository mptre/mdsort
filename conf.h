struct environment;

struct config {
	struct string_list	*paths;
	struct expr		*expr;
};

struct config_list {
	struct macro_list	*cl_macros;
	struct config		*cl_list;	/* VECTOR(struct config) */
};

void	config_list_init(struct config_list *);
void	config_list_free(struct config_list *);
int	config_list_parse(struct config_list *, const char *,
    const struct environment *);
