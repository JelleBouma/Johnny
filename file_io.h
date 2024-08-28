#ifndef JOHNNY_FILE_IO_H
#define JOHNNY_FILE_IO_H

/*
 * 'slurp' reads the file identified by 'path' into a character buffer
 * pointed at by 'buf' On success, the size of the file is returned; on
 * failure, -1 is returned and ERRNO is set by the underlying system
 * or library call that failed.
 *
 * WARNING: 'slurp' malloc()s memory to '*buf' which must be freed by
 * the caller.
 */
size_t slurp(char const* path, unsigned char **buf);

#endif //JOHNNY_FILE_IO_H
