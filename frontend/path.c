/*
 *      Frontend path derivation helpers
 *
 *      Copyright (c) 1999 Mark Taylor
 *                    2000-2017 Robert Hegemann
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <ctype.h>
#ifdef HAVE_LIMITS_H
# include <limits.h>
#endif
#include <string.h>

#include "console.h"
#include "path.h"

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

#define dimension_of(array) (sizeof(array) / sizeof((array)[0]))
#define SLASH '/'

static int
local_strcasecmp(const char *s1, const char *s2)
{
    unsigned char c1;
    unsigned char c2;

    do {
        c1 = (unsigned char) tolower(*s1);
        c2 = (unsigned char) tolower(*s2);
        if (!c1) {
            break;
        }
        ++s1;
        ++s2;
    } while (c1 == c2);
    return c1 - c2;
}

static size_t
scanPath(char const* s, char const** a, char const** b)
{
    char const* s1 = s;
    char const* s2 = s;
    if (s != 0) {
        for (; *s; ++s) {
            if (*s == SLASH) {
                s2 = s;
            }
        }
    }
    if (a != 0) {
        *a = s1;
    }
    if (b != 0) {
        *b = s2;
    }
    return s2 - s1;
}

static size_t
scanBasename(char const* s, char const** a, char const** b)
{
    char const* s1 = s;
    char const* s2 = s;
    if (s != 0) {
        for (; *s; ++s) {
            switch (*s) {
            case SLASH:
                s1 = s2 = s;
                break;
            case '.':
                s2 = s;
                break;
            }
        }
        if (s2 == s1) {
            s2 = s;
        }
        if (*s1 == SLASH) {
            ++s1;
        }
    }
    if (a != 0) {
        *a = s1;
    }
    if (b != 0) {
        *b = s2;
    }
    return s2 - s1;
}

static int
isCommonSuffix(char const* s_ext)
{
    char const* suffixes[] =
    { ".WAV", ".RAW", ".MP1", ".MP2"
    , ".MP3", ".MPG", ".MPA", ".CDA"
    , ".OGG", ".AIF", ".AIFF", ".AU"
    , ".SND", ".FLAC", ".WV", ".OFR"
    , ".TAK", ".MP4", ".M4A", ".PCM"
    , ".W64"
    };
    size_t i;
    for (i = 0; i < dimension_of(suffixes); ++i) {
        if (local_strcasecmp(s_ext, suffixes[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

int
generateOutPath(char const* inPath, char const* outDir, char const* s_ext, char* outPath)
{
    size_t const max_path = PATH_MAX;
    size_t i = 0;
    int out_dir_used = 0;

    if (outDir != 0 && outDir[0] != 0) {
        out_dir_used = 1;
        while (*outDir) {
            outPath[i++] = *outDir++;
            if (i >= max_path) {
                goto err_generateOutPath;
            }
        }
        if (i > 0 && outPath[i - 1] != SLASH) {
            outPath[i++] = SLASH;
            if (i >= max_path) {
                goto err_generateOutPath;
            }
        }
        outPath[i] = 0;
    }
    else {
        char const* pa;
        char const* pb;
        size_t j;
        size_t n = scanPath(inPath, &pa, &pb);
        if (i + n >= max_path) {
            goto err_generateOutPath;
        }
        for (j = 0; j < n; ++j) {
            outPath[i++] = pa[j];
        }
        if (n > 0) {
            outPath[i++] = SLASH;
            if (i >= max_path) {
                goto err_generateOutPath;
            }
        }
        outPath[i] = 0;
    }
    {
        int replace_suffix = 0;
        char const* na;
        char const* nb;
        size_t j;
        size_t n = scanBasename(inPath, &na, &nb);
        if (i + n >= max_path) {
            goto err_generateOutPath;
        }
        for (j = 0; j < n; ++j) {
            outPath[i++] = na[j];
        }
        outPath[i] = 0;
        if (isCommonSuffix(nb) == 1) {
            replace_suffix = 1;
            if (out_dir_used == 0) {
                if (local_strcasecmp(nb, s_ext) == 0) {
                    replace_suffix = 0;
                }
            }
        }
        if (replace_suffix == 0) {
            while (*nb) {
                outPath[i++] = *nb++;
                if (i >= max_path) {
                    goto err_generateOutPath;
                }
            }
            outPath[i] = 0;
        }
    }
    if (i + 5 >= max_path) {
        goto err_generateOutPath;
    }
    while (*s_ext) {
        outPath[i++] = *s_ext++;
    }
    outPath[i] = 0;
    return 0;

err_generateOutPath:
    error_printf("error: output file name too long\n");
    return 1;
}
