#include <stdlib.h>
#include <stdio.h>

size_t slurp(char const* path, unsigned char **buf)
{
    long off_end;

    /* Open the file */
    FILE* fp = fopen(path, "rb");
    if (fp == NULL)
        return -1L;

    /* Seek to the end of the file */
    const int rc = fseek(fp, 0L, SEEK_END);
    if (0 != rc)
        return -1L;

    /* Byte offset to the end of the file (size) */
    if ( 0 > (off_end = ftell(fp)) )
        return -1L;
    const size_t fsz = off_end;

    /* Allocate a buffer to hold the whole file */
    *buf = malloc( fsz );
    if (*buf == NULL)
        return -1L;

    /* Rewind file pointer to start of file */
    rewind(fp);

    /* Slurp file into buffer */
    if (fsz != fread(*buf, 1, fsz, fp)) {
        free(*buf);
        return -1L;
    }

    /* Close the file */
    if (EOF == fclose(fp)) {
        free(*buf);
        return -1L;
    }

    /* Return the file size */
    return fsz;
}