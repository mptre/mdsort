/*
 * Copyright (c) 2023 Anton Lindqvist <anton@basename.se>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef LIBKS_BUFFER_H
#define LIBKS_BUFFER_H

#include <stdarg.h> /* va_list */
#include <stddef.h> /* size_t */

#if !defined(KS_PRIVATE)
#  define KS_PRIVATE
#endif

struct buffer;

struct buffer_callbacks {
	void *(*alloc)(size_t, void *);
	void *(*realloc)(void *, size_t, size_t, void *);
	void (*free)(void *, size_t, void *);
	void *arg;
};

struct buffer_getline {
	struct buffer *bf;
	size_t off;
};

KS_PRIVATE struct buffer *buffer_alloc(size_t);
KS_PRIVATE struct buffer *buffer_alloc_impl(size_t, int,
    const struct buffer_callbacks *);
KS_PRIVATE void buffer_free(struct buffer *);

KS_PRIVATE struct buffer *buffer_read(const char *);
KS_PRIVATE struct buffer *buffer_read_fd(int);
KS_PRIVATE int buffer_read_fd_impl(struct buffer *, int);

KS_PRIVATE char *buffer_release(struct buffer *);
KS_PRIVATE char *buffer_str(struct buffer *);
KS_PRIVATE void buffer_reset(struct buffer *);

KS_PRIVATE size_t buffer_pop(struct buffer *, size_t);

KS_PRIVATE int buffer_cmp(const struct buffer *, const struct buffer *);

KS_PRIVATE int buffer_puts(struct buffer *, const char *, size_t);
KS_PRIVATE int buffer_putc(struct buffer *, char);
KS_PRIVATE int buffer_printf(struct buffer *, const char *, ...)
	__attribute__((format(printf, 2, 3)));
KS_PRIVATE int buffer_vprintf(struct buffer *, const char *, va_list)
	__attribute__((format(printf, 2, 0)));

KS_PRIVATE const char *buffer_getline(const struct buffer *,
    struct buffer_getline *);
KS_PRIVATE const char *buffer_getline_impl(const struct buffer *,
    struct buffer_getline *);
KS_PRIVATE void buffer_getline_free(struct buffer_getline *);

KS_PRIVATE const char *buffer_get_ptr(const struct buffer *);
KS_PRIVATE size_t buffer_get_len(const struct buffer *);
KS_PRIVATE size_t buffer_get_size(const struct buffer *);

#endif /* !LIBKS_BUFFER_H */
