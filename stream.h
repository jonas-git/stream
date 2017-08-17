#ifndef STREAM_H
#define STREAM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdio.h>

/* implementation of a byte/binary stream.
 | wrapper functions operate the same as described in the standard
 |   except when specified differently.
 | additionally, functions prefixed with `stream_f*` exclusively
 |   operate on the internal [FILE *] pointer, i.e. a file.
 | when working on a buffer instead of a file, the last byte will
 |   be reserved for a NUL value. this is because all `*printf` functions
 |   that work on buffers (namely the `*s*printf` family) append a
 |   null-terminator to the result. there are several solutions to this
 |   problem, but leaving space for a single byte is the fastest and
 |   easiest one. this note can be safely ignored when no `stream_*printf`
 |   will be used on an instance of [struct stream].
 */

// holds information about a (binary/byte) stream object.
struct stream {
	FILE *fstream;
	char *cursor;
	char *begin;
	char *end;
	int eof;
	int err; // placeholder
};

#define stream_perror(str) perror(str)
#define stream_getchar(stream) stream_getc(stream)

void stream_init(struct stream *stream, char *buf, size_t size);
FILE *stream_finit(struct stream *stream, const char *filename,
                   const char *mode);

// the following two functions (which take a `mode` argument)
// force the binary mode specifier. the passed `mode` string argument
// is copied into a new (static) buffer and the specified is inserted
// into a valid spot - if it's not already present.
FILE *stream_fopen(struct stream *stream, const char *filename,
                   const char *mode);
FILE *stream_freopen(const char *filename, const char *mode,
                     struct stream *stream);
int stream_fclose(const struct stream *stream);

int stream_eof(const struct stream *stream);
int stream_error(const struct stream *stream);
void stream_clearerr(struct stream *stream);

int stream_seek(struct stream *stream, size_t offset, int origin);
size_t stream_tell(const struct stream *stream);
void stream_rewind(struct stream *stream);
int stream_flush(struct stream *stream);

int stream_getpos(const struct stream *stream, size_t *pos);
int stream_setpos(struct stream *stream, const size_t *pos);

size_t stream_read(void *ptr, size_t size, size_t count,
                   struct stream *stream);
size_t stream_write(const void *ptr, size_t size, size_t count,
                    struct stream *stream);

// unlike `ungetc()`, this function writes the character to the stream
// when not working on a file because a raw character stream is not buffered
// internally (because it is a buffer by itself).
int stream_ungetc(int character, struct stream *stream);
int stream_getc(struct stream *stream);
char *stream_gets(char *str, size_t num, struct stream *stream);

int stream_putc(int c, struct stream *stream);
int stream_puts(const char *str, struct stream *stream);

int stream_scanf(const struct stream *stream, const char *format, ...);
int stream_printf(const struct stream *stream, const char *format, ...);

int stream_vscanf(const struct stream *stream,
                  const char *format, va_list args);
int stream_vprintf(const struct stream *stream,
                   const char *format, va_list args);

#ifdef __cplusplus
}
#endif

#endif
