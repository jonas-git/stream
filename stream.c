#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "stream.h"

// constant describing the longest possible length for a mode string:
// r, w, a, rb, wb, ab, rb+, wb+, ab+, wx, wbx, w+x or wb+x
#define FMODE_SIZE_MAX (4)

#define MUL_NO_OVERFLOW_LIMIT (((size_t)1 << (sizeof(size_t) * 4)) - 1)
#define detect_mul_overflow(a, b) \
	(((a) >= MUL_NO_OVERFLOW_LIMIT || (b) >= MUL_NO_OVERFLOW_LIMIT) \
		&& (a) && SIZE_MAX / (a) < (b))

// hasless(v, 1):
// https://graphics.stanford.edu/~seander/bithacks.html#ZeroInWord
#define has_nul(v) (((v) - 0x01010101UL) & ~(v) & 0x80808080UL)

// non-zero if both addresses are long-aligned.
#define is_unaligned(a) ((uintptr_t)(a) & (sizeof(long) - 1))

#define stream_remaining(stream) ((stream)->end - (stream)->cursor)
#define stream_is_eof(stream) ((stream)->cursor == (stream)->end)

// prototypes for internal functions (definitions are at the end of this file).
char *fmode_force_binary(char *dest, const char *mode);
size_t rawstrncpy_fast(unsigned char *dest, const unsigned char *src,
                       size_t num);

void stream_init(struct stream *stream, char *buf, size_t size)
{
	stream->fstream = NULL;
	stream->begin = stream->cursor = buf;
	// `size - 1` to account space for the terminating null-terminator.
	stream->end = buf + size - 1;
	stream->eof = 0;
}

FILE *stream_finit(struct stream *stream, const char *filename,
                   const char *mode)
{
	*stream = (struct stream){ 0 };
	return stream_fopen(stream, filename, mode);
}

FILE *stream_fopen(struct stream *stream, const char *filename,
                   const char *mode)
{
	char mode_bin[FMODE_SIZE_MAX + 1];
	fmode_force_binary(mode_bin, mode);
	return (stream->fstream = fopen(filename, mode));
}

FILE *stream_freopen(const char *filename, const char *mode,
                     struct stream *stream)
{
	return (stream->fstream = freopen(filename, mode, stream->fstream));
}

int stream_fclose(const struct stream *stream)
{
	return fclose(stream->fstream);
}

int stream_eof(const struct stream *stream)
{
	if (stream->fstream)
		return feof(stream->fstream);
	return stream->eof;
}

int stream_error(const struct stream *stream)
{
	if (stream->fstream)
		return ferror(stream->fstream);
	return stream->err;
}

void stream_clearerr(struct stream *stream)
{
	if (stream->fstream) {
		clearerr(stream->fstream);
		return;
	}

	stream->eof = 0;
	stream->err = 0;
}

int stream_seek(struct stream *stream, size_t offset, int origin)
{
	if (stream->fstream)
		return fseek(stream->fstream, (long)offset, origin);

	stream->eof = 0;
	switch (origin) {
	case SEEK_SET: stream->cursor = stream->begin + offset; break;
	case SEEK_CUR: stream->cursor += offset; break;
	case SEEK_END: stream->cursor = stream->end + offset; break;
	}

	return 0;
}

size_t stream_tell(const struct stream *stream)
{
	if (stream->fstream)
		return ftell(stream->fstream);

	return stream->cursor - stream->begin;
}

void stream_rewind(struct stream *stream)
{
	if (stream->fstream) {
		rewind(stream->fstream);
		return;
	}

	stream->cursor = stream->begin;
	stream->eof = 0;
}

int stream_flush(struct stream *stream)
{
	if (stream->fstream)
		return fflush(stream->fstream);

	return 0;
}

int stream_getpos(const struct stream *stream, size_t *pos)
{
	if (stream->fstream)
		return fgetpos(stream->fstream, (fpos_t *)pos);

	*pos = stream->cursor - stream->begin;
	return 0;
}

int stream_setpos(struct stream *stream, const size_t *pos)
{
	if (stream->fstream)
		return fsetpos(stream->fstream, (const fpos_t *)pos);

	stream->cursor = stream->begin + *pos;
	stream->eof = 0;
	return 0;
}

size_t stream_read(void *ptr, size_t size, size_t count,
                   struct stream *stream)
{
	if (stream->fstream)
		return fread(ptr, size, count, stream->fstream);

	// overflow detection
	if (detect_mul_overflow(size, count)) {
		errno = EOVERFLOW;
		return 0;
	}

	size_t total = size * count;
	size_t remaining = stream_remaining(stream);
	if (remaining < total) {
		stream->eof = 1;
		// prevent dividing by zero and copying nothing later
		if (!remaining)
			return 0;
		total = remaining;
	}

	memcpy(ptr, stream->cursor, total);
	stream->cursor += total;
	return total / size;
}

size_t stream_write(const void *ptr, size_t size, size_t count,
                    struct stream *stream)
{
	if (stream->fstream)
		return fwrite(ptr, size, count, stream->fstream);

	// overflow detection
	if (detect_mul_overflow(size, count)) {
		errno = EOVERFLOW;
		return 0;
	}

	size_t total = size * count;
	size_t remaining = stream_remaining(stream);
	if (remaining < total)
		total = remaining;

	memcpy(stream->cursor, ptr, total);
	stream->cursor += total;
	return total / size;
}

int stream_getc(struct stream *stream)
{
	if (stream->fstream)
		return fgetc(stream->fstream);

	if (stream_is_eof(stream)) {
		stream->eof = 1;
		return EOF;
	}

	return *stream->cursor++;
}

int stream_ungetc(int character, struct stream *stream)
{
	if (stream->fstream)
		return ungetc(character, stream->fstream);

	if (character < 0)
		return EOF;

	stream->eof = 0;
	*--stream->cursor = (char)character;
	return character;
}

char *stream_gets(char *str, size_t num, struct stream *stream)
{
	if (stream->fstream)
		return fgets(str, (int)num, stream->fstream);

	if (!num)
		return NULL;

	--num; // for the null-terminator
	size_t remaining = stream_remaining(stream);
	if (remaining < num) {
		stream->eof = 1;
		// prevent copying nothing later
		if (!remaining)
			return NULL;
		num = remaining;
	}

	// find the next line feed
	char *lf_pos = memchr(stream->cursor, '\n', num);
	if (lf_pos)
		num = lf_pos - stream->cursor + 1;

	memcpy(str, stream->cursor, num);
	str[num] = 0;
	return str;
}

int stream_putc(int c, struct stream *stream)
{
	if (stream->fstream)
		return fputc(c, stream->fstream);

	if (stream_is_eof(stream)) {
		stream->eof = 1;
		return EOF;
	}

	return (*stream->cursor++ = (char)c);
}

int stream_puts(const char *str, struct stream *stream)
{
	if (stream->fstream)
		return fputs(str, stream->fstream);

	size_t remaining = stream_remaining(stream);
	size_t written = rawstrncpy_fast(stream->cursor, str, remaining);
	stream->cursor += written;
	return 0;
}

int stream_scanf(const struct stream *stream, const char *format, ...)
{
	va_list args;
	va_start(args, format);

	int filled = stream_vscanf(stream, format, args);

	va_end(args);
	return filled;
}

int stream_printf(const struct stream *stream, const char *format, ...)
{
	va_list args;
	va_start(args, format);

	int filled = stream_vprintf(stream, format, args);

	va_end(args);
	return filled;
}

int stream_vscanf(const struct stream *stream,
                  const char *format, va_list args)
{
	if (stream->fstream)
		return vfscanf(stream->fstream, format, args);
	return vsscanf(stream->cursor, format, args);
}

int stream_vprintf(const struct stream *stream,
                   const char *format, va_list args)
{
	if (stream->fstream)
		return vfprintf(stream->fstream, format, args);

	// find the position where the null-terminator would be written
	// and store the character at that position to put it back later.
	int nt_pos = vsnprintf(NULL, 0, format, args);

	// in both of the following calls to `vsnprintf()` the size argument
	// is set to `remaining + 1` because the size of the buffer was
	// truncated before to leave space for the null-terminator that
	// `vsnprintf()` includes.

	size_t remaining = stream->end - stream->cursor;
	if (nt_pos >= remaining)
		return vsnprintf(stream->cursor, remaining + 1, format, args);

	char nt_char = stream->cursor[nt_pos];
	vsnprintf(stream->cursor, remaining + 1, format, args);
	stream->cursor[nt_pos] = nt_char;
	return nt_pos;
}



/* internal functions: */

static char *fmode_force_binary(char *dest, const char *mode)
{
	char *ptr = dest;
	*ptr++ = *mode++;

	// force binary mode for the to be opened file.
	// the second character in the mode string is appropriate in
	// any pattern (refer to the reference of `fopen()` for more details).
	*ptr++ = strchr(mode, 'b') ? *mode++ : 'b';

	// the literal `2` in `FMODE_SIZE_MAX - 2` represents the amount
	// of characters before the memory address in `ptr`. one could
	// also write `ptr - mode_bin` to get the same result.
	strncpy(ptr, mode, FMODE_SIZE_MAX - 2);

	// set null-terminator manually in case that `strncpy()` did not
	// do it already for some reason (e.g. the `mode` string is too long).
	dest[FMODE_SIZE_MAX] = 0;

	return dest;
}

static size_t rawstrncpy_fast(unsigned char *dest, const unsigned char *src,
                              register size_t num)
{
	size_t total = num;
	if (num >= sizeof(long) && !(is_unaligned(src) | is_unaligned(dest))) {
		register const unsigned long *src_word = (const unsigned long *)src;
		register unsigned long *dest_word = (unsigned long *)dest;

		while (!has_nul(*src_word)) {
			*dest_word++ = *src_word++;
			if ((num -= sizeof(long)) >= sizeof(long))
				break;
		}

		src = (const unsigned char *)src_word;
		dest = (unsigned char *)dest_word;
	}

	unsigned int c;
	while (num && (c = *src++))
		*dest++ = c, --num;
	return total - num;
}
