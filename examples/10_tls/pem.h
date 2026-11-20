/*
 * pem.h - read a PEM file for the TLS examples.
 *
 * liboncrpc takes certificates and keys as PEM text in memory, so that it
 * never reads files itself. A program reads them however it likes; these
 * examples simply read them with stdio before the loop starts.
 */
#ifndef EXAMPLE_PEM_H
#define EXAMPLE_PEM_H

#include <stdio.h>
#include <stdlib.h>

/* The contents of dir/name as a NUL-terminated string (free() it), or NULL. */
static char *read_pem(const char *dir, const char *name)
{
    char path[1024];
    FILE *f;
    char *text;
    long size;

    snprintf(path, sizeof(path), "%s/%s", dir, name);
    f = fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr, "cannot open %s\n", path);
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0 ||
        (text = (char *)malloc((size_t)size + 1)) == NULL) {
        fclose(f);
        return NULL;
    }
    if (fread(text, 1, (size_t)size, f) != (size_t)size) {
        free(text);
        fclose(f);
        return NULL;
    }
    text[size] = '\0';
    fclose(f);
    return text;
}

#endif
