/* crcall.c -- Generate CRC and test code for every model read from stdin
 * Copyright (C) 2020 Mark Adler
 * For conditions of distribution and use, see copyright notice in crcany.c.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
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

// printf() directive to print a uintmax_t in hexadecimal (e.g. "llx" or "jx").
#define X PRIxMAX

/* A strcpy() that returns a pointer to the terminating null, like stpcpy(). */
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

/* A strncmp() that ignores case, like POSIX strncasecmp(). */
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

// Generate test code for model and name. Append the include for the header
// file for this model to defs, test code for each function for this model to
// test, a unified interface to the word-wise function of this model to allc,
// and a table of names, widths, and function pointers to allh. The test code
// computes the CRC of "123456789" (nine bytes), and compares that to the
// provided check value. If the check value does not match the computed CRC,
// then the generated code prints an error to stderr.
static int test_gen(model_t *model, char *name,
                    FILE *defs, FILE *test, FILE *allc, FILE *allh) {
    // write test and all code for bit-wise function
    fprintf(defs,
        "#include \"%s.h\"\n", name);
    fprintf(test,
        "\n"
        "    // %s\n"
        "    init = %s_bit(0, NULL, 0);\n"
        "    blot = init | ~((((uintmax_t)1 << (%d - 1)) << 1) - 1);\n"
        "    if (%s_bit(blot, \"123456789\", 9) != %#"X")\n"
        "        fputs(\"bit-wise mismatch for %s\\n\", stderr), err++;\n"
        "    crc = %s_bit(blot, data + 1, sizeof(data) - 1);\n",
            name, name, model->width, name, model->check, name, name);
    fprintf(allc,
        "\n"
        "#include \"%s.h\"\n"
        "uintmax_t %s(uintmax_t crc, void const *mem, size_t len) {\n"
        "    return %s_word(crc, mem, len);\n"
        "}\n", name, name, name);
    fprintf(allh,
        "    {\"%s\", \"", model->name);
    for (char *p = name + 3; *p; p++)
        if (isalnum(*p))
            putc(*p, allh);
    fprintf(allh,
        "\", %u, %s},\n", model->width, name);

    // write test code for small number of bits function
    if (model->ref)
        fprintf(test,
        "    if (%s_bit(blot, \"\\xda\", 1) !=\n"
        "        %s_rem(%s_rem(blot, 0xda, 3), 0x1b, 5))\n"
        "        fputs(\"small bits mismatch for %s\\n\", stderr), err++;\n",
                name, name, name, name);
    else
        fprintf(test,
        "    if (%s_bit(blot, \"\\xda\", 1) !=\n"
        "        %s_rem(%s_rem(blot, 0xda, 3), 0xd0, 5))\n"
        "        fputs(\"small bits mismatch for %s\\n\", stderr), err++;\n",
                name, name, name, name);

    // write test code for byte-wise function
    fprintf(test,
        "    if (%s_byte(0, NULL, 0) != init ||\n"
        "        %s_byte(blot, \"123456789\", 9) != %#"X" ||\n"
        "        %s_byte(blot, data + 1, sizeof(data) - 1) != crc)\n"
        "        fputs(\"byte-wise mismatch for %s\\n\", stderr), err++;\n",
            name, name, model->check, name, name);

    // write test code for word-wise function
    fprintf(test,
        "    if (%s_word(0, NULL, 0) != init ||\n"
        "        %s_word(blot, \"123456789\", 9) != %#"X" ||\n"
        "        %s_word(blot, data + 1, sizeof(data) - 1) != crc)\n"
        "        fputs(\"word-wise mismatch for %s\\n\", stderr), err++;\n",
            name, name, model->check, name, name);
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
    // for error return
    if (head != NULL)
        *head = NULL;
    if (code != NULL)
        *code = NULL;

    // create the src directory if it does not exist
    int ret = mkdir(src, 0755);
    if (ret && errno != EEXIST)
        return 1;

    // construct the path for the source files, leaving suff pointing to the
    // position for the 'h' or 'c'.
    char path[strlen(src) + 1 + strlen(name) + 2 + 1];
    char *suff = strcpytail(path, src);
    *suff++ = '/';
    suff = strcpytail(suff, name);
    *suff++ = '.';
    suff[1] = 0;

    // create header file
    if (head != NULL) {
        *suff = 'h';
        *head = fopen(path, "wx");
        if (*head == NULL)
            return errno == EEXIST ? 2 : 1;
    }

    // create code file
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

    // all good -- return handles for header and code
    return 0;
}

// Subdirectory for source files.
#define SRC "src"

// Read CRC models from stdin, one per line, and generate C tables and routines
// to compute each one. Each CRC goes into it's own .h and .c source files in
// the "src" subdirectory of the current directory.
int main(void) {
    // determine endianess of this machine (for testing on this machine, we
    // need to match its endianess)
    unsigned little = 1;
    little = *((unsigned char *)(&little));

    // create test source files
    FILE *defs, *test, *allc, *allh;
    if (create_source(SRC, "test_src", &defs, &test) ||
        create_source(SRC, "allcrcs", &allh, &allc)) {
        fputs("could not create test code files -- aborting\n", stderr);
        return 1;
    }
    fputs(
        "#include <stdio.h>\n"
        "#include <stdlib.h>\n"
        "#include <stdint.h>\n"
        "#include <time.h>\n"
        "#include \"test_src.h\"\n"
        "\n"
        "int main(void) {\n"
        "    unsigned char data[31];\n"
        "    {\n"
        "        unsigned max = (unsigned)RAND_MAX + 1;\n"
        "        int shft = 0;\n"
        "        do {\n"
        "            max >>= 1;\n"
        "            shft++;\n"
        "        } while (max > 256);\n"
        "        srand(time(NULL));\n"
        "        for (int i = 0; i < 997; i++)\n"
        "            (void)rand();\n"
        "        size_t n = sizeof(data);\n"
        "        do {\n"
        "            data[--n] = rand() >> shft;\n"
        "        } while (n);\n"
        "    }\n"
        "    uintmax_t init, blot, crc;\n"
        "    int err = 0;\n", test);
    fputs(
        "#include <stdint.h>\n", allc);
    fputs(
        "\n"
        "typedef uintmax_t (*crc_f)(uintmax_t, void const *, size_t);\n"
        "\n"
        "struct {\n"
        "    char const *name;\n"
        "    char const *match;\n"
        "    unsigned short width;\n"
        "    crc_f func;\n"
        "} const all[] = {\n", allh);

    // read each line from stdin, process the CRC description
    char *line = NULL;
    size_t size;
    ptrdiff_t len;
    while ((len = getcleanline(&line, &size, stdin)) != -1) {
        if (len == 0)
            continue;
        model_t model;

        // read the model
        model.name = NULL;
        int ret = read_model(&model, line, 0);
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
            // convert the parameters to form for calculation
            process_model(&model);

            // generate the routine name prefix for this model
            char *name = crc_name(&model);
            if (name == NULL) {
                fputs("out of memory -- aborting\n", stderr);
                break;
            }

            // generate the code
            FILE *head, *code;
            int ret = create_source(SRC, name, &head, &code);
            if (ret)
                fprintf(stderr, "%s/%s.[ch] %s -- skipping\n", SRC, name,
                        errno == 1 ? "create error" : "exists");
            else {
                crc_gen(&model, name, little, INTMAX_BITS, head, code);
                test_gen(&model, name, defs, test, allc, allh);
                fclose(code);
                fclose(head);
            }
            free(name);
        }
        free(model.name);
    }
    free(line);

    fputs(
        "    {\"\", \"\", 0, NULL}\n"
        "};\n", allh);
    fclose(allh);
    fclose(allc);
    fputs(
        "\n"
        "    // done\n"
        "    fputs(err ? \"** verification failed\\n\" :\n"
        "                \"-- all good\\n\", stderr);\n"
        "    return 0;\n"
        "}\n", test);
    fclose(test);
    fclose(defs);
    return 0;
}
