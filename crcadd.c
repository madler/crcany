/* crcadd.c -- Generate CRC code for models read from stdin
 * Copyright (C) 2020 Mark Adler
 * For conditions of distribution and use, see copyright notice in crcany.c.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include "model.h"
#include "crc.h"
#include "crcgen.h"

// Define INTMAX_BITS.
#ifndef INTMAX_BITS
#  if INTMAX_MAX == 2147483647
#    define INTMAX_BITS 32
#  elif INTMAX_MAX == 9223372036854775807
#    define INTMAX_BITS 64
#  else
#    error Unexpected maximum integer size
#  endif
#endif

// A strcpy() that returns a pointer to the terminating null, like stpcpy().
static char *strcpytail(char *dst, char const *src) {
    size_t i = 0;
    for (;;) {
        int ch = src[i];
        dst[i] = ch;
        if (ch == 0)
            return dst + i;
        i++;
    }
}

// A strncmp() that ignores case, like POSIX strncasecmp().
static int strncmpi(char const *s1, char const *s2, size_t n) {
    unsigned char const *a = (unsigned char const *)s1,
                        *b = (unsigned char const *)s2;
    for (size_t i = 0; i < n; i++) {
        int diff = tolower(a[i]) - tolower(b[i]);
        if (diff != 0)
            return diff;
        if (a[i] == 0)
            break;
    }
    return 0;
}

// Make a base name for the CRC routines and source files, making use of the
// name in the model description. All names start with "crc*", where "*" is
// replaced by the number of bits in the CRC. The returned name is allocated
// space, or NULL if there was an error. This transformation is tuned to the
// names that appear in the RevEng CRC catalogue.
static char *crc_name(model_t *model) {
    char *id = model->name;
    char *name = malloc(8 + strlen(id));
    if (name == NULL)
        return NULL;
    char *next = strcpytail(name, "crc");
    next += sprintf(next, "%u", model->width);
    if (strncmpi(id, "crc", 3) == 0) {
        id += 3;
        if (*id == '-')
            id++;
        while (*id >= '0' && *id <= '9')
            id++;
        if (*id == '/')
            id++;
    }
    if (*id) {
        char *suff = next;
        do {
            if (isalnum(*id)) {
                if (next == suff && isdigit(*id))
                    *next++ = '_';
                *next++ = tolower(*id);
            }
            else if (*id == '-')
                *next++ = '_';
        } while (*++id);
    }
    *next = 0;
    return name;
}

// Create the src directory if necessary, and create src/name.h and src/name.c
// source files for writing, returning their handles in *head and *code
// respectively. If head or code is NULL, then the respective file is not
// created. If a file by either of those names already exists, then an error is
// returned. A failure to create the directory or writable files will return
// true, with no open handles and *head and *code containing NULL. If the
// problem was a source file that already existed, then create_source() will
// return 2. Otherwise it will return 1 on error, 0 on success.
static int create_source(char *src, char *name, FILE **head, FILE **code) {
    // For error return.
    if (head != NULL)
        *head = NULL;
    if (code != NULL)
        *code = NULL;

    // Create the src directory if it does not exist.
    int ret = mkdir(src, 0755);
    if (ret && errno != EEXIST)
        return 1;

    // Construct the path for the source files, leaving suff pointing to the
    // position for the 'h' or 'c'.
    char path[strlen(src) + 1 + strlen(name) + 2 + 1];
    char *suff = strcpytail(path, src);
    *suff++ = '/';
    suff = strcpytail(suff, name);
    *suff++ = '.';
    suff[1] = 0;

    // Create header file.
    if (head != NULL) {
        *suff = 'h';
        *head = fopen(path, "wx");
        if (*head == NULL)
            return errno == EEXIST ? 2 : 1;
    }

    // Create code file.
    if (code != NULL) {
        *suff = 'c';
        *code = fopen(path, "wx");
        if (*code == NULL) {
            int err = errno;
            if (head != NULL) {
                fclose(*head);
                *head = NULL;
                *suff = 'h';
                unlink(path);
            }
            return err == EEXIST ? 2 : 1;
        }
    }

    // All good -- return handles for header and code.
    return 0;
}

// Subdirectory for source files.
#define SRC "src"

// Read CRC models from stdin, one per line, and generate C tables and routines
// to compute each one. Each CRC goes into it's own .h and .c source files in
// the "src" subdirectory of the current directory.
int main(int argc, char **argv) {
    // Set endianess default to be that of this machine, and bits of the
    // largest integer type for this machine. (Usually 64.)
    unsigned little = 1;
    little = *((unsigned char *)(&little));
    int bits = INTMAX_BITS;

    // Process options for generated code endianess and word bits.
    for (int i = 1; i < argc; i++)
        if (argv[i][0] == '-')
            for (char *opt = argv[i] + 1; *opt; opt++)
                switch (*opt) {
                case 'b':
                    little = 0;
                    break;
                case 'l':
                    little = 1;
                    break;
                case '4':
                    bits = 32;
                    break;
                case 'h':
                    fputs("usage: crcadd [-b] [-l] [-4] < crc-defs\n"
                          "    -b for big endian\n"
                          "    -l (ell) for little endian\n"
                          "    -4 for four-byte words\n", stderr);
                    return 0;
                default:
                    fprintf(stderr, "unknown option: %c\n", *opt);
                    return 1;
                }
        else {
            fputs("must precede options with a dash\n", stderr);
            return 1;
        }

    // Read each line from stdin, process the CRC description.
    char *line = NULL;
    size_t size;
    ptrdiff_t len;
    while ((len = getcleanline(&line, &size, stdin)) != -1) {
        if (len == 0)
            continue;
        model_t model;

        // Read the model, allowing for no check value.
        model.name = NULL;
        int ret = read_model(&model, line, 1);
        if (ret == 2) {
            fputs("out of memory -- aborting\n", stderr);
            break;
        }
        else if (ret == 1)
            fprintf(stderr, "%s is an unusable model -- skipping\n",
                    model.name);
        else if (model.width > INTMAX_BITS)
            fprintf(stderr, "%s is too wide (%u bits) -- skipping\n",
                    model.name, model.width);
        else {
            // Convert the parameters for calculation, and fill in the tables
            // that are independent of endianess and word length.
            process_model(&model);
            crc_table_combine(&model);
            crc_table_bytewise(&model);

            // Generate the routine name prefix for this model.
            char *name = crc_name(&model);
            if (name == NULL) {
                fputs("out of memory -- aborting\n", stderr);
                break;
            }

            // Generate the code.
            FILE *head, *code;
            int ret = create_source(SRC, name, &head, &code);
            if (ret)
                fprintf(stderr, "%s/%s.[ch] %s -- skipping\n", SRC, name,
                        errno == 1 ? "create error" : "exists");
            else {
                crc_gen(&model, name, little, bits, head, code);
                fclose(code);
                fclose(head);
            }
            free(name);
        }
        free(model.name);
    }
    free(line);
    return 0;
}
