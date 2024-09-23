struct arena_scope;

const char	*base64_decode(const char *, struct arena_scope *);
const char	*quoted_printable_decode(const char *, struct arena_scope *);
const char	*rfc2047_decode(const char *, struct arena_scope *);
