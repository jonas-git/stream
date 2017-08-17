# stream
**Stream implementation for files and buffers in C.**

# Important notes
* wrapper functions operate the same as described in the standard
    except when specified differently.
* additionally, functions prefixed with `stream_f*` exclusively
    operate on the internal `[FILE *]` pointer, i.e. a file.
* when working on a buffer instead of a file, the last byte will
    be reserved for a NUL value. this is because all `*printf` functions
    that work on buffers (namely the `*s*printf` family) append a
    null-terminator to the result. there are several solutions to this
    problem, but leaving space for a single byte is the fastest and
    easiest one. this note can be safely ignored when no `stream_*printf`
    will be used on an instance of `[struct stream]`.
