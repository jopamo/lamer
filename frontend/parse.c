/*
 *      Command line parsing related functions
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

/* $Id$ */

/*
 * Command line parsing related functions
 *
 * Copyright (c) 1999 Mark Taylor
 *               2000-2017 Robert Hegemann
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Library General Public License for more details.
 *
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_ICONV
# include <iconv.h>
# ifdef HAVE_LANGINFO_H
#  include <langinfo.h>
#  include <locale.h>
# endif
#endif

#include "lame.h"
#include "parse.h"
#include "main.h"
#include "get_audio.h"
#include "console.h"
#include "path.h"
#include "usage.h"

#ifdef WITH_DMALLOC
# include <dmalloc.h>
#endif

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#if defined _ALLOW_INTERNAL_OPTIONS
#define INTERNAL_OPTS 1
#else
#define INTERNAL_OPTS 0
#endif

#if (INTERNAL_OPTS!=0)
#include "set_get.h"
#else
#define lame_set_tune(a,b) (void)0
#define lame_set_short_threshold(a,b,c) (void)0
#define lame_set_maskingadjust(a,b) (void)0
#define lame_set_maskingadjust_short(a,b) (void)0
#define lame_set_ATHcurve(a,b) (void)0
#define lame_set_preset_notune(a,b) (void)0
#define lame_set_substep(a,b) (void)0
#define lame_set_subblock_gain(a,b) (void)0
#define lame_set_sfscale(a,b) (void)0
#endif

static const int internal_opts_enabled = INTERNAL_OPTS;

/*
 * Compatibility-visible frontend state.  Keep these symbols while lamer
 * remains a drop-in replacement; parser internals should not add new globals.
 */

ReaderConfig global_reader = { sf_unknown, 0, 0, 0, 0 };
WriterConfig global_writer = { 0 };

UiConfig global_ui_config = {0,0,0,0};

DecoderConfig global_decoder;

RawPCMConfig global_raw_pcm =
{ /* in_bitwidth */ 16
, /* in_signed   */ -1
, /* in_endian   */ ByteOrderLittleEndian
};



/* possible text encodings */
typedef enum TextEncoding
{ TENC_RAW     /* bytes will be stored as-is into ID3 tags, which are Latin1 per definition */
, TENC_LATIN1  /* text will be converted from local encoding to Latin1, as ID3 needs it */
, TENC_UTF16   /* text will be converted from local encoding to Unicode (UCS-2), as ID3v2 wants it */
, TENC_UTF8    /* text will be converted from local encoding to UTF-8, as ID3v2.4 wants it */
} TextEncoding;

#ifdef HAVE_ICONV
# define ID3TAGS_EXTENDED

static const char *
current_character_encoding(void)
{
# ifdef HAVE_LANGINFO_H
    const char *codeset = nl_langinfo(CODESET);

    if (codeset != NULL && codeset[0] != '\0')
        return codeset;
# endif

    {
        const char *locale_name = getenv("LC_ALL");
        const char *dot;

        if (locale_name == NULL || locale_name[0] == '\0')
            locale_name = getenv("LC_CTYPE");
        if (locale_name == NULL || locale_name[0] == '\0')
            locale_name = getenv("LANG");
        if (locale_name == NULL || locale_name[0] == '\0')
            return "UTF-8";

        dot = strrchr(locale_name, '.');
        if (dot != NULL && dot[1] != '\0')
            return dot + 1;
    }

    return "UTF-8";
}


static iconv_t
open_iconv(const char *to_code, const char *from_code)
{
    iconv_t cd = iconv_open(to_code, from_code);

    if (cd == (iconv_t) -1) {
        const char *suffix = strstr(to_code, "//TRANSLIT");

        if (suffix != NULL) {
            size_t len = (size_t) (suffix - to_code);
            char plain[64];

            if (len < sizeof(plain)) {
                memcpy(plain, to_code, len);
                plain[len] = '\0';
                cd = iconv_open(plain, from_code);
            }
        }
    }

    return cd;
}


static char *
convert_encoding(const char *src, const char *target_encoding,
                 const unsigned char *prefix, size_t prefix_size)
{
    const char *source_encoding;
    iconv_t cd;
    char *buffer;
    char *input;
    char *output;
    size_t input_left;
    size_t output_left;
    size_t capacity;

    if (src == NULL || target_encoding == NULL)
        return NULL;

    source_encoding = current_character_encoding();
    cd = open_iconv(target_encoding, source_encoding);
    if (cd == (iconv_t) -1)
        return NULL;

    input_left = strlen(src);

    if (input_left > (SIZE_MAX - 32) / 4) {
        iconv_close(cd);
        errno = EOVERFLOW;
        return NULL;
    }

    capacity = input_left * 4 + prefix_size + 32;
    buffer = (char *) calloc(capacity, 1);
    if (buffer == NULL) {
        iconv_close(cd);
        return NULL;
    }

    if (prefix_size != 0)
        memcpy(buffer, prefix, prefix_size);

    input = (char *) src;
    output = buffer + prefix_size;
    output_left = capacity - prefix_size - 4;

    while (input_left != 0) {
        size_t rc = iconv(cd, &input, &input_left, &output, &output_left);

        if (rc != (size_t) -1)
            continue;

        if (errno != E2BIG) {
            free(buffer);
            iconv_close(cd);
            return NULL;
        }

        {
            size_t used = (size_t) (output - buffer);
            size_t new_capacity;
            char *grown;

            if (capacity > SIZE_MAX / 2) {
                free(buffer);
                iconv_close(cd);
                errno = EOVERFLOW;
                return NULL;
            }

            new_capacity = capacity * 2;
            grown = (char *) realloc(buffer, new_capacity);
            if (grown == NULL) {
                free(buffer);
                iconv_close(cd);
                return NULL;
            }

            buffer = grown;
            capacity = new_capacity;
            output = buffer + used;
            output_left = capacity - used - 4;
        }
    }

    /*
     * Flush any stateful output encoding and leave at least four zero bytes.
     * The extra zero bytes safely terminate both single-byte and UTF-16 data.
     */
    for (;;) {
        size_t rc = iconv(cd, NULL, NULL, &output, &output_left);

        if (rc != (size_t) -1)
            break;

        if (errno != E2BIG) {
            free(buffer);
            iconv_close(cd);
            return NULL;
        }

        {
            size_t used = (size_t) (output - buffer);
            size_t new_capacity;
            char *grown;

            if (capacity > SIZE_MAX / 2) {
                free(buffer);
                iconv_close(cd);
                errno = EOVERFLOW;
                return NULL;
            }

            new_capacity = capacity * 2;
            grown = (char *) realloc(buffer, new_capacity);
            if (grown == NULL) {
                free(buffer);
                iconv_close(cd);
                return NULL;
            }

            buffer = grown;
            capacity = new_capacity;
            output = buffer + used;
            output_left = capacity - used - 4;
        }
    }

    iconv_close(cd);
    memset(output, 0, 4);
    return buffer;
}


static char *
to_latin1(const char *src)
{
    return convert_encoding(src, "ISO-8859-1//TRANSLIT", NULL, 0);
}


static char *
to_utf8(const char *src)
{
    return convert_encoding(src, "UTF-8//TRANSLIT", NULL, 0);
}


static char *
to_utf16(const char *src)
{
    static const unsigned char bom[] = { 0xff, 0xfe };

    return convert_encoding(src, "UTF-16LE//TRANSLIT", bom, sizeof(bom));
}
#endif /* HAVE_ICONV */

static int
argument_missing(const char *token, const char *arg)
{
    if (arg != NULL && arg[0] != '\0')
        return 0;

    error_printf("WARNING: argument missing for '%s'\n",
                 token != NULL ? token : "");
    return 1;
}


static int
getDoubleValue(const char *token, const char *arg, double *value)
{
    char *end = NULL;
    double parsed;

    if (argument_missing(token, arg))
        return 0;

    errno = 0;
    parsed = strtod(arg, &end);

    if (end == arg || end == NULL || *end != '\0' ||
        errno == ERANGE || !isfinite(parsed)) {
        error_printf("WARNING: invalid numeric argument '%s' for '%s'\n",
                     arg, token != NULL ? token : "");
        return 0;
    }

    if (value != NULL)
        *value = parsed;

    return 1;
}


static int
getIntValue(const char *token, const char *arg, int *value)
{
    char *end = NULL;
    long parsed;

    if (argument_missing(token, arg))
        return 0;

    errno = 0;
    parsed = strtol(arg, &end, 10);

    if (end == arg || end == NULL || *end != '\0' ||
        errno == ERANGE || parsed < INT_MIN || parsed > INT_MAX) {
        error_printf("WARNING: invalid integer argument '%s' for '%s'\n",
                     arg, token != NULL ? token : "");
        return 0;
    }

    if (value != NULL)
        *value = (int) parsed;

    return 1;
}


static const char *
skip_space(const char *text)
{
    while (text != NULL && isspace((unsigned char) *text))
        ++text;

    return text;
}


static int
parse_int_pair(const char *text, int *first, int *second)
{
    const char *cursor;
    char *end;
    long a;
    long b;

    if (text == NULL || first == NULL || second == NULL)
        return 0;

    cursor = skip_space(text);
    errno = 0;
    a = strtol(cursor, &end, 10);
    if (end == cursor || errno == ERANGE || a < INT_MIN || a > INT_MAX)
        return 0;

    cursor = skip_space(end);
    if (*cursor == '\0') {
        *first = (int) a;
        *second = (int) a;
        return 1;
    }

    if (*cursor++ != ',')
        return 0;

    cursor = skip_space(cursor);
    errno = 0;
    b = strtol(cursor, &end, 10);
    if (end == cursor || errno == ERANGE || b < INT_MIN || b > INT_MAX)
        return 0;

    cursor = skip_space(end);
    if (*cursor != '\0')
        return 0;

    *first = (int) a;
    *second = (int) b;
    return 1;
}


static int
parse_float_pair(const char *text, float *first, float *second)
{
    const char *cursor;
    char *end;
    float a;
    float b;

    if (text == NULL || first == NULL || second == NULL)
        return 0;

    cursor = skip_space(text);
    errno = 0;
    a = strtof(cursor, &end);
    if (end == cursor || errno == ERANGE || !isfinite((double) a))
        return 0;

    cursor = skip_space(end);
    if (*cursor == '\0') {
        *first = a;
        *second = a;
        return 1;
    }

    if (*cursor++ != ',')
        return 0;

    cursor = skip_space(cursor);
    errno = 0;
    b = strtof(cursor, &end);
    if (end == cursor || errno == ERANGE || !isfinite((double) b))
        return 0;

    cursor = skip_space(end);
    if (*cursor != '\0')
        return 0;

    *first = a;
    *second = b;
    return 1;
}


static int
copy_path(char *dest, size_t capacity, const char *src,
          const char *program_name, const char *option_name)
{
    size_t len;

    if (dest == NULL || capacity == 0 || src == NULL)
        return -1;

    len = strlen(src);
    if (len >= capacity) {
        error_printf("%s: %s argument length (%zu) exceeds limit (%zu)\n",
                     program_name != NULL ? program_name : "lamer",
                     option_name != NULL ? option_name : "path",
                     len, capacity - 1);
        return -1;
    }

    memcpy(dest, src, len + 1);
    return 0;
}


static char *
duplicate_string(const char *src)
{
    size_t len;
    char *copy;

    if (src == NULL)
        return NULL;

    len = strlen(src);
    if (len == SIZE_MAX)
        return NULL;

    copy = (char *) malloc(len + 1);
    if (copy != NULL)
        memcpy(copy, src, len + 1);

    return copy;
}


#ifdef ID3TAGS_EXTENDED
static int
set_id3v2tag_utf8(lame_global_flags *gfp, int type, const char *str)
{
    switch (type) {
    case 'a': return id3tag_set_textinfo_utf8(gfp, "TPE1", str);
    case 't': return id3tag_set_textinfo_utf8(gfp, "TIT2", str);
    case 'l': return id3tag_set_textinfo_utf8(gfp, "TALB", str);
    case 'g': return id3tag_set_textinfo_utf8(gfp, "TCON", str);
    case 'c': return id3tag_set_comment_utf8(gfp, NULL, NULL, str);
    case 'n': return id3tag_set_textinfo_utf8(gfp, "TRCK", str);
    case 'y': return id3tag_set_textinfo_utf8(gfp, "TYER", str);
    case 'v': return id3tag_set_fieldvalue_utf8(gfp, str);
    default:  return -3;
    }
}


static int
set_id3v2tag_utf16(lame_global_flags *gfp, int type,
                    const unsigned short *str)
{
    switch (type) {
    case 'a': return id3tag_set_textinfo_utf16(gfp, "TPE1", str);
    case 't': return id3tag_set_textinfo_utf16(gfp, "TIT2", str);
    case 'l': return id3tag_set_textinfo_utf16(gfp, "TALB", str);
    case 'g': return id3tag_set_textinfo_utf16(gfp, "TCON", str);
    case 'c': return id3tag_set_comment_utf16(gfp, NULL, NULL, str);
    case 'n': return id3tag_set_textinfo_utf16(gfp, "TRCK", str);
    case 'y': return id3tag_set_textinfo_utf16(gfp, "TYER", str);
    case 'v': return id3tag_set_fieldvalue_utf16(gfp, str);
    default:  return -3;
    }
}
#endif


static int
set_id3tag(lame_global_flags *gfp, int type, const char *str)
{
    switch (type) {
    case 'a':
        id3tag_set_artist(gfp, str);
        return 0;
    case 't':
        id3tag_set_title(gfp, str);
        return 0;
    case 'l':
        id3tag_set_album(gfp, str);
        return 0;
    case 'g':
        return id3tag_set_genre(gfp, str);
    case 'c':
        id3tag_set_comment(gfp, str);
        return 0;
    case 'n':
        return id3tag_set_track(gfp, str);
    case 'y':
        id3tag_set_year(gfp, str);
        return 0;
    case 'v':
        return id3tag_set_fieldvalue(gfp, str);
    default:
        return -3;
    }
}


static int
id3_tag(lame_global_flags *gfp, int type, TextEncoding encoding,
        const char *str)
{
    void *converted = NULL;
    int result;

    if (gfp == NULL || str == NULL)
        return -3;

    /*
     * Keep the historical behavior of populating ID3v1 in addition to
     * Unicode ID3v2 fields when possible.
     */
    if ((encoding == TENC_UTF16 || encoding == TENC_UTF8) && type != 'v')
        (void) id3_tag(gfp, type, TENC_LATIN1, str);

#ifdef ID3TAGS_EXTENDED
    switch (encoding) {
    case TENC_RAW:
        converted = duplicate_string(str);
        break;
    case TENC_LATIN1:
        converted = to_latin1(str);
        break;
    case TENC_UTF16:
        converted = to_utf16(str);
        break;
    case TENC_UTF8:
        converted = to_utf8(str);
        break;
    default:
        return -3;
    }
#else
    (void) encoding;
    converted = duplicate_string(str);
#endif

    if (converted == NULL) {
        error_printf("Error: could not convert ID3 tag text\n");
        return -3;
    }

#ifdef ID3TAGS_EXTENDED
    switch (encoding) {
    case TENC_UTF16:
        result = set_id3v2tag_utf16(
            gfp, type, (const unsigned short *) converted);
        break;
    case TENC_UTF8:
        result = set_id3v2tag_utf8(gfp, type, (const char *) converted);
        break;
    case TENC_RAW:
    case TENC_LATIN1:
        result = set_id3tag(gfp, type, (const char *) converted);
        break;
    default:
        result = -3;
        break;
    }
#else
    result = set_id3tag(gfp, type, (const char *) converted);
#endif

    free(converted);
    return result;
}


/*  note: for presets it would be better to externalize them in a file.
    suggestion:  lame --preset <file-name> ...
            or:  lame --preset my-setting  ... and my-setting is defined in lame.ini
 */

/*
Note from GB on 08/25/2002:
I am merging --presets and --alt-presets. Old presets are now aliases for
corresponding abr values from old alt-presets. This way we now have a
unified preset system, and I hope than more people will use the new tuned
presets instead of the old unmaintained ones.
*/



/************************************************************************
*
* usage
*
* PURPOSE:  Writes presetting info to #stdout#
*
************************************************************************/


static int
presets_set(lame_t gfp, int fast, int cbr, const char *preset_name, const char *ProgramName)
{
    int     mono = 0;

    if ((strcmp(preset_name, "help") == 0) && (fast < 1)
        && (cbr < 1)) {
        frontend_version_print(stdout);
        frontend_presets_longinfo(stdout);
        return -1;
    }

    /*aliases for compatibility with old presets */

    if (strcmp(preset_name, "phone") == 0) {
        preset_name = "16";
        mono = 1;
    }
    if ((strcmp(preset_name, "phon+") == 0) ||
        (strcmp(preset_name, "lw") == 0) ||
        (strcmp(preset_name, "mw-eu") == 0) || (strcmp(preset_name, "sw") == 0)) {
        preset_name = "24";
        mono = 1;
    }
    if (strcmp(preset_name, "mw-us") == 0) {
        preset_name = "40";
        mono = 1;
    }
    if (strcmp(preset_name, "voice") == 0) {
        preset_name = "56";
        mono = 1;
    }
    if (strcmp(preset_name, "fm") == 0) {
        preset_name = "112";
    }
    if ((strcmp(preset_name, "radio") == 0) || (strcmp(preset_name, "tape") == 0)) {
        preset_name = "112";
    }
    if (strcmp(preset_name, "hifi") == 0) {
        preset_name = "160";
    }
    if (strcmp(preset_name, "cd") == 0) {
        preset_name = "192";
    }
    if (strcmp(preset_name, "studio") == 0) {
        preset_name = "256";
    }

    if (strcmp(preset_name, "medium") == 0) {
        lame_set_VBR_q(gfp, 4);
        lame_set_VBR(gfp, vbr_default);
        return 0;
    }

    if (strcmp(preset_name, "standard") == 0) {
        lame_set_VBR_q(gfp, 2);
        lame_set_VBR(gfp, vbr_default);
        return 0;
    }

    else if (strcmp(preset_name, "extreme") == 0) {
        lame_set_VBR_q(gfp, 0);
        lame_set_VBR(gfp, vbr_default);
        return 0;
    }

    else if ((strcmp(preset_name, "insane") == 0) && (fast < 1)) {

        lame_set_preset(gfp, INSANE);

        return 0;
    }

    /* Generic ABR preset. */
    if (fast < 1) {
        int bitrate = 0;

        if (getIntValue("preset", preset_name, &bitrate)) {
            if (bitrate >= 8 && bitrate <= 320) {
                lame_set_preset(gfp, bitrate);

                if (cbr == 1)
                    lame_set_VBR(gfp, vbr_off);

                if (mono == 1)
                    lame_set_mode(gfp, MONO);

                return 0;
            }

            frontend_version_print(Console_IO.Error_fp);
            error_printf(
                "Error: The bitrate specified is out of the valid range for this preset\n"
                "\n"
                "When using this mode you must enter a value between \"8\" and \"320\"\n"
                "\n"
                "For further information try: \"%s --preset help\"\n",
                ProgramName);
            return -1;
        }
    }

    frontend_version_print(Console_IO.Error_fp);
    error_printf("Error: You did not enter a valid profile and/or options with --preset\n"
                 "\n"
                 "Available profiles are:\n"
                 "\n"
                 "                 medium\n"
                 "                 standard\n"
                 "                 extreme\n"
                 "                 insane\n"
                 "          <cbr> (ABR Mode) - The ABR Mode is implied. To use it,\n"
                 "                             simply specify a bitrate. For example:\n"
                 "                             \"--preset 185\" activates this\n"
                 "                             preset and uses 185 as an average kbps.\n" "\n");
    error_printf("    Some examples:\n"
                 "\n"
                 " or \"%s --preset standard <input file> <output file>\"\n"
                 " or \"%s --preset cbr 192 <input file> <output file>\"\n"
                 " or \"%s --preset 172 <input file> <output file>\"\n"
                 " or \"%s --preset extreme <input file> <output file>\"\n"
                 "\n"
                 "For further information try: \"%s --preset help\"\n", ProgramName, ProgramName,
                 ProgramName, ProgramName, ProgramName);
    return -1;
}

static void
genre_list_handler(int num, const char *name, void *cookie)
{
    (void) cookie;
    console_printf("%3d %s\n", num, name);
}


/************************************************************************
*
* parse_args
*
* PURPOSE:  Sets encoding parameters to the specifications of the
* command line.  Default settings are used for parameters
* not specified in the command line.
*
* If the input file is in WAVE or AIFF format, the sampling frequency is read
* from the AIFF header.
*
* The input and output filenames are read into #inpath# and #outpath#.
*
************************************************************************/

/*
 * Command-line option names are ASCII.  Keep comparisons independent of the
 * process locale instead of routing option parsing through locale-sensitive
 * tolower().
 */
static unsigned char
ascii_lower(unsigned char ch)
{
    if (ch >= (unsigned char) 'A' && ch <= (unsigned char) 'Z')
        return (unsigned char) (ch + ('a' - 'A'));

    return ch;
}


static int
local_strcasecmp(const char *lhs, const char *rhs)
{
    unsigned char a;
    unsigned char b;

    if (lhs == rhs)
        return 0;
    if (lhs == NULL)
        return -1;
    if (rhs == NULL)
        return 1;

    do {
        a = ascii_lower((unsigned char) *lhs++);
        b = ascii_lower((unsigned char) *rhs++);

        if (a != b)
            return (int) a - (int) b;
    } while (a != '\0');

    return 0;
}


static int
local_strncasecmp(const char *lhs, const char *rhs, size_t count)
{
    size_t i;

    if (count == 0 || lhs == rhs)
        return 0;
    if (lhs == NULL)
        return -1;
    if (rhs == NULL)
        return 1;

    for (i = 0; i < count; ++i) {
        unsigned char a = ascii_lower((unsigned char) lhs[i]);
        unsigned char b = ascii_lower((unsigned char) rhs[i]);

        if (a != b)
            return (int) a - (int) b;
        if (a == '\0')
            return 0;
    }

    return 0;
}


/*
 * Preserve the frontend's extension-based input-type detection.  Content
 * probing belongs in the input layer, not in command-line parsing.
 */
static int
filename_to_type(const char *file_name)
{
    const char *ext;

    if (file_name == NULL)
        return sf_unknown;

    ext = strrchr(file_name, '.');
    if (ext == NULL)
        return sf_unknown;

    if (local_strcasecmp(ext, ".mpg") == 0 ||
        local_strcasecmp(ext, ".mp1") == 0 ||
        local_strcasecmp(ext, ".mp2") == 0 ||
        local_strcasecmp(ext, ".mp3") == 0)
        return sf_mp123;

    if (local_strcasecmp(ext, ".wav") == 0)
        return sf_wave;

    if (local_strcasecmp(ext, ".aif") == 0 ||
        local_strcasecmp(ext, ".aiff") == 0)
        return sf_aiff;

    if (local_strcasecmp(ext, ".raw") == 0)
        return sf_raw;

    return sf_unknown;
}

static int
frequency_to_hz(double value, double khz_threshold,
                int threshold_is_inclusive, int *hz)
{
    int value_is_khz;
    double scaled;

    if (hz == NULL || value < 0.0)
        return 0;

    value_is_khz = value < khz_threshold ||
                   (threshold_is_inclusive && value == khz_threshold);
    scaled = value * (value_is_khz ? 1000.0 : 1.0);
    if (!isfinite(scaled) || scaled > (double) INT_MAX - 0.5)
        return 0;

    *hz = (int) (scaled + 0.5);
    return 1;
}


static int
encode_ns_tuning(double value)
{
    int encoded;

    if (value <= -8.0)
        encoded = -32;
    else if (value >= 7.75)
        encoded = 31;
    else
        encoded = (int) (value * 4.0);

    if (encoded < 0)
        encoded += 64;

    return encoded;
}


static int
resample_rate(double freq)
{
    if (freq >= 1.e3)
        freq *= 1.e-3;

    switch ((int) freq) {
    case 8:
        return 8000;
    case 11:
        return 11025;
    case 12:
        return 12000;
    case 16:
        return 16000;
    case 22:
        return 22050;
    case 24:
        return 24000;
    case 32:
        return 32000;
    case 44:
        return 44100;
    case 48:
        return 48000;
    default:
        error_printf("Illegal resample frequency: %.3f kHz\n", freq);
        return 0;
    }
}

static int
set_id3_albumart(lame_t gfp, const char *file_name)
{
    FILE *fp;
    char *albumart = NULL;
    long end_pos;
    size_t size;
    int ret = 0;

    if (file_name == NULL || file_name[0] == '\0')
        return 0;

    fp = lame_fopen(file_name, "rb");
    if (fp == NULL) {
        error_printf("Could not find: '%s'.\n", file_name);
        return 1;
    }

    if (fseek(fp, 0, SEEK_END) != 0 ||
        (end_pos = ftell(fp)) < 0 ||
        fseek(fp, 0, SEEK_SET) != 0) {
        error_printf("Could not determine image size: '%s'.\n", file_name);
        fclose(fp);
        return 3;
    }

    size = (size_t) end_pos;
    if ((long) size != end_pos) {
        error_printf("Album art is too large to read: '%s'.\n", file_name);
        fclose(fp);
        return 2;
    }

    albumart = (char *) malloc(size != 0 ? size : 1);
    if (albumart == NULL) {
        error_printf("Insufficient memory for reading the albumart.\n");
        fclose(fp);
        return 2;
    }

    if (size != 0 && fread(albumart, 1, size, fp) != size) {
        error_printf("Read error: '%s'.\n", file_name);
        ret = 3;
    }
    else if (id3tag_set_albumart(gfp, albumart, size) != 0) {
        error_printf(
            "Unsupported image: '%s'.\n"
            "Specify JPEG/PNG/GIF image\n",
            file_name);
        ret = 4;
    }

    free(albumart);
    fclose(fp);
    return ret;
}


enum id3tag_mode {
    ID3TAG_MODE_DEFAULT,
    ID3TAG_MODE_V1_ONLY,
    ID3TAG_MODE_V2_ONLY
};

static int dev_only_with_arg(char const* str, char const* token, char const* nextArg, int* argIgnored, int* argUsed)
{
    if (0 != local_strcasecmp(token,str)) return 0;
    *argUsed = 1;
    if (internal_opts_enabled) return 1;
    *argIgnored = 1;
    error_printf("WARNING: ignoring developer-only switch --%s %s\n", token, nextArg);
    return 0;
}

static int dev_only_without_arg(char const* str, char const* token, int* argIgnored)
{
    if (0 != local_strcasecmp(token,str)) return 0;
    if (internal_opts_enabled) return 1;
    *argIgnored = 1;
    error_printf("WARNING: ignoring developer-only switch --%s\n", token);
    return 0;
}

/* Long options are parsed explicitly below. */

static int
parse_args_(lame_global_flags *gfp, int argc, char **argv,
            char *inPath, char *outPath, char **nogap_inPath, int *num_nogap)
{
    char outDir[PATH_MAX + 1] = "";
    int input_file = 0;  /* set to 1 if we parse an input file name */
    int i;
    int autoconvert = 0;
    int nogap = 0;
    int nogap_tags = 0;  /* set to 1 to use VBR tags in NOGAP mode */
    const char *ProgramName =
        (argc > 0 && argv != NULL && argv[0] != NULL) ? argv[0] : "lamer";
    int     count_nogap = 0;
    int     noreplaygain = 0; /* is RG explicitly disabled by the user */
    int     id3tag_mode = ID3TAG_MODE_DEFAULT;
    int     ignore_tag_errors = 0;  /* Ignore errors in values passed for tags */

    if (gfp == NULL || argv == NULL || inPath == NULL || outPath == NULL)
        return -1;

#ifdef ID3TAGS_EXTENDED
    enum TextEncoding id3_tenc = TENC_UTF16;
#else
    enum TextEncoding id3_tenc = TENC_LATIN1;
#endif

#if defined(HAVE_ICONV) && defined(HAVE_LANGINFO_H)
    (void) setlocale(LC_CTYPE, "");
#endif
    inPath[0] = '\0';
    outPath[0] = '\0';
    /* turn on display options. user settings may turn them off below */
    global_ui_config.silent = 0; /* default */
    global_ui_config.brhist = 1;
    global_decoder.mp3_delay = 0;
    global_decoder.mp3_delay_set = 0;
    global_decoder.disable_wav_header = 0;
    global_ui_config.print_clipping_info = 0;
    id3tag_init(gfp);

    /* process args */
    for (i = 0; ++i < argc;) {
        char   *token;
        int     argUsed;
        int     argIgnored=0;

        token = argv[i];
        if (*token++ == '-') {
            char   *nextArg = i + 1 < argc ? argv[i + 1] : "";
            argUsed = 0;
            if (!*token) { /* The user wants to use stdin and/or stdout. */
                input_file = 1;
                if (inPath[0] == '\0') {
                    if (copy_path(inPath, PATH_MAX + 1, argv[i],
                                  ProgramName, "input") != 0)
                        return -1;
                }
                else if (outPath[0] == '\0') {
                    if (copy_path(outPath, PATH_MAX + 1, argv[i],
                                  ProgramName, "output") != 0)
                        return -1;
                }
            }
            if (*token == '-') { /* GNU style */
                double  double_value = 0;
                int     int_value = 0;
                token++;

                if (local_strcasecmp(token, "resample") == 0) {
                    argUsed = getDoubleValue(token, nextArg, &double_value);
                    if (argUsed) {
                        int rate = resample_rate(double_value);

                        if (rate == 0)
                            return -1;

                        (void) lame_set_out_samplerate(gfp, rate);
                    }

                } else if (local_strcasecmp(token, "vbr-old") == 0) {
                    lame_set_VBR(gfp, vbr_rh);

                } else if (local_strcasecmp(token, "vbr-new") == 0) {
                    lame_set_VBR(gfp, vbr_mtrh);

                } else if (local_strcasecmp(token, "vbr-mtrh") == 0) {
                    lame_set_VBR(gfp, vbr_mtrh);

                } else if (local_strcasecmp(token, "cbr") == 0) {
                    lame_set_VBR(gfp, vbr_off);

                } else if (local_strcasecmp(token, "abr") == 0) {
                    /* values larger than 8000 are bps (like Fraunhofer), so it's strange to get 320000 bps MP3 when specifying 8000 bps MP3 */
                    argUsed = getIntValue(token, nextArg, &int_value);
                    if (argUsed) {
                        if (int_value >= 8000)
                            int_value = (int) (((long long) int_value + 500) / 1000);
                        if (int_value > 320) {
                            int_value = 320;
                        }
                        if (int_value < 8) {
                            int_value = 8;
                        }
                        lame_set_VBR(gfp, vbr_abr);
                        lame_set_VBR_mean_bitrate_kbps(gfp, int_value);
                    }

                } else if (local_strcasecmp(token, "r3mix") == 0) {
                    lame_set_preset(gfp, R3MIX);

                } else if (local_strcasecmp(token, "bitwidth") == 0) {
                    argUsed = getIntValue(token, nextArg, &int_value);
                    if (argUsed)
                        global_raw_pcm.in_bitwidth = int_value;

                } else if (local_strcasecmp(token, "signed") == 0) {
                    global_raw_pcm.in_signed = 1;

                } else if (local_strcasecmp(token, "unsigned") == 0) {
                    global_raw_pcm.in_signed = 0;

                } else if (local_strcasecmp(token, "little-endian") == 0) {
                    global_raw_pcm.in_endian = ByteOrderLittleEndian;

                } else if (local_strcasecmp(token, "big-endian") == 0) {
                    global_raw_pcm.in_endian = ByteOrderBigEndian;

                } else if (local_strcasecmp(token, "mp1input") == 0) {
                    global_reader.input_format = sf_mp1;

                } else if (local_strcasecmp(token, "mp2input") == 0) {
                    global_reader.input_format = sf_mp2;

                } else if (local_strcasecmp(token, "mp3input") == 0) {
                    global_reader.input_format = sf_mp3;

                } else if (local_strcasecmp(token, "decode") == 0) {
                    (void) lame_set_decode_only(gfp, 1);

                } else if (local_strcasecmp(token, "analysis") == 0) {
                    (void) lame_set_analysis(gfp, 1);

                } else if (local_strcasecmp(token, "experimental-short-transient-redistribute") == 0) {
                    (void) lame_set_experimental_short_transient_redistribute(gfp, 1);

                } else if (local_strcasecmp(token, "flush") == 0) {
                    global_writer.flush_write = 1;

                } else if (local_strcasecmp(token, "decode-mp3delay") == 0) {
                    argUsed = getIntValue(token, nextArg, &int_value);
                    if (argUsed) {
                        global_decoder.mp3_delay = int_value;
                        global_decoder.mp3_delay_set = 1;
                    }

                } else if (local_strcasecmp(token, "nores") == 0) {
                    lame_set_disable_reservoir(gfp, 1);

                } else if (local_strcasecmp(token, "strictly-enforce-ISO") == 0) {
                    lame_set_strict_ISO(gfp, MDB_STRICT_ISO);

                } else if (local_strcasecmp(token, "buffer-constraint") == 0) {
                  argUsed = 1;
                if (strcmp(nextArg, "default") == 0)
                  (void) lame_set_strict_ISO(gfp, MDB_DEFAULT);
                else if (strcmp(nextArg, "strict") == 0)
                  (void) lame_set_strict_ISO(gfp, MDB_STRICT_ISO);
                else if (strcmp(nextArg, "maximum") == 0)
                  (void) lame_set_strict_ISO(gfp, MDB_MAXIMUM);
                else {
                    error_printf("unknown buffer constraint '%s'\n", nextArg);
                    return -1;
                }

                } else if (local_strcasecmp(token, "scale") == 0) {
                    argUsed = getDoubleValue(token, nextArg, &double_value);
                    if (argUsed)
                        (void) lame_set_scale(gfp, (float) double_value);

                } else if (local_strcasecmp(token, "scale-l") == 0) {
                    argUsed = getDoubleValue(token, nextArg, &double_value);
                    if (argUsed)
                        (void) lame_set_scale_left(gfp, (float) double_value);

                } else if (local_strcasecmp(token, "scale-r") == 0) {
                    argUsed = getDoubleValue(token, nextArg, &double_value);
                    if (argUsed)
                        (void) lame_set_scale_right(gfp, (float) double_value);

                } else if (local_strcasecmp(token, "gain") == 0) {
                    argUsed = getDoubleValue(token, nextArg, &double_value);
                    if (argUsed) {
                        double gain = double_value;
                        gain = gain > -20.f ? gain : -20.f;
                        gain = gain < 12.f ? gain : 12.f;
                        gain = pow(10.f, gain*0.05);
                        (void) lame_set_scale(gfp, (float) gain);
                    }

                } else if (local_strcasecmp(token, "noasm") == 0) {
                    if (argument_missing(token, nextArg))
                        return -1;

                    argUsed = 1;
                    if (local_strcasecmp(nextArg, "mmx") == 0)
                        (void) lame_set_asm_optimizations(gfp, MMX, 0);
                    else if (local_strcasecmp(nextArg, "3dnow") == 0)
                        (void) lame_set_asm_optimizations(gfp, AMD_3DNOW, 0);
                    else if (local_strcasecmp(nextArg, "sse") == 0)
                        (void) lame_set_asm_optimizations(gfp, SSE, 0);
                    else {
                        error_printf("%s: unknown --noasm target '%s'\n",
                                     ProgramName, nextArg);
                        return -1;
                    }

                } else if (local_strcasecmp(token, "freeformat") == 0) {
                    lame_set_free_format(gfp, 1);

                } else if (local_strcasecmp(token, "replaygain-fast") == 0) {
                    lame_set_findReplayGain(gfp, 1);

#ifdef DECODE_ON_THE_FLY
                } else if (local_strcasecmp(token, "replaygain-accurate") == 0) {
                    lame_set_decode_on_the_fly(gfp, 1);
                lame_set_findReplayGain(gfp, 1);
#endif

                } else if (local_strcasecmp(token, "noreplaygain") == 0) {
                    noreplaygain = 1;
                lame_set_findReplayGain(gfp, 0);


#ifdef DECODE_ON_THE_FLY
                } else if (local_strcasecmp(token, "clipdetect") == 0) {
                    global_ui_config.print_clipping_info = 1;
                    lame_set_decode_on_the_fly(gfp, 1);
#endif

                } else if (local_strcasecmp(token, "nohist") == 0) {
                    global_ui_config.brhist = 0;

                /* options for ID3 tag */
#ifdef ID3TAGS_EXTENDED
                } else if (local_strcasecmp(token, "id3v2-utf16") == 0 ||
                           local_strcasecmp(token, "id3v2-ucs2") == 0) { /* id3v2-ucs2 for compatibility only */
                    id3_tenc = TENC_UTF16;
                    id3tag_add_v2(gfp);

                } else if (local_strcasecmp(token, "id3v2-utf8") == 0) {
                    id3_tenc = TENC_UTF8;
                    id3tag_add_v2_4_UTF8(gfp);

                } else if (local_strcasecmp(token, "id3v2-latin1") == 0) {
                    id3_tenc = TENC_LATIN1;
                    id3tag_add_v2(gfp);
#endif

                } else if (local_strcasecmp(token, "tt") == 0) {
                    argUsed = 1;
                    id3_tag(gfp, 't', id3_tenc, nextArg);

                } else if (local_strcasecmp(token, "ta") == 0) {
                    argUsed = 1;
                    id3_tag(gfp, 'a', id3_tenc, nextArg);

                } else if (local_strcasecmp(token, "tl") == 0) {
                    argUsed = 1;
                    id3_tag(gfp, 'l', id3_tenc, nextArg);

                } else if (local_strcasecmp(token, "ty") == 0) {
                    argUsed = 1;
                    id3_tag(gfp, 'y', id3_tenc, nextArg);

                } else if (local_strcasecmp(token, "tc") == 0) {
                    argUsed = 1;
                    id3_tag(gfp, 'c', id3_tenc, nextArg);

                } else if (local_strcasecmp(token, "tn") == 0) {
                    int ret = id3_tag(gfp, 'n', id3_tenc, nextArg);
                    argUsed = 1;
                    if (ret != 0) {
                        if (0 == ignore_tag_errors) {
                            if (id3tag_mode == ID3TAG_MODE_V1_ONLY) {
                                if (global_ui_config.silent < 9) {
                                    error_printf("The track number has to be between 1 and 255 for ID3v1.\n");
                                }
                                return -1;
                            }
                            else if (id3tag_mode == ID3TAG_MODE_V2_ONLY) {
                                /* track will be stored as-is in ID3v2 case, so no problem here */
                            }
                            else {
                                if (global_ui_config.silent < 9) {
                                    error_printf("The track number has to be between 1 and 255 for ID3v1, ignored for ID3v1.\n");
                                }
                            }
                        }
                    }

                } else if (local_strcasecmp(token, "tg") == 0) {
                    int ret = 0;
                    argUsed = 1;
                    if (nextArg != 0 && strlen(nextArg) > 0) {
                        ret = id3_tag(gfp, 'g', id3_tenc, nextArg);
                    }
                    if (ret != 0) {
                        if (0 == ignore_tag_errors) {
                            if (ret == -1) {
                                error_printf("Unknown ID3v1 genre number: '%s'.\n", nextArg);
                                return -1;
                            }
                            else if (ret == -2) {
                                if (id3tag_mode == ID3TAG_MODE_V1_ONLY) {
                                    error_printf("Unknown ID3v1 genre: '%s'.\n", nextArg);
                                    return -1;
                                }
                                else if (id3tag_mode == ID3TAG_MODE_V2_ONLY) {
                                    /* genre will be stored as-is in ID3v2 case, so no problem here */
                                }
                                else {
                                    if (global_ui_config.silent < 9) {
                                        error_printf("Unknown ID3v1 genre: '%s'.  Setting ID3v1 genre to 'Other'\n", nextArg);
                                    }
                                }
                            }
                            else {
                                if (global_ui_config.silent < 10)
                                    error_printf("Internal error.\n");
                                return -1;
                            }
                        }
                    }

                } else if (local_strcasecmp(token, "tv") == 0) {
                    argUsed = 1;
                    if (id3_tag(gfp, 'v', id3_tenc, nextArg)) {
                        if (global_ui_config.silent < 9) {
                            error_printf("Invalid field value: '%s'. Ignored\n", nextArg);
                        }
                    }

                } else if (local_strcasecmp(token, "ti") == 0) {
                    argUsed = 1;
                    if (set_id3_albumart(gfp, nextArg) != 0) {
                        if (! ignore_tag_errors) {
                            return -1;
                        }
                    }

                } else if (local_strcasecmp(token, "ignore-tag-errors") == 0) {
                    ignore_tag_errors = 1;

                } else if (local_strcasecmp(token, "add-id3v2") == 0) {
                    id3tag_add_v2(gfp);

                } else if (local_strcasecmp(token, "id3v1-only") == 0) {
                    id3tag_v1_only(gfp);
                    id3tag_mode = ID3TAG_MODE_V1_ONLY;

                } else if (local_strcasecmp(token, "id3v2-only") == 0) {
                    id3tag_v2_only(gfp);
                    id3tag_mode = ID3TAG_MODE_V2_ONLY;

                } else if (local_strcasecmp(token, "space-id3v1") == 0) {
                    id3tag_space_v1(gfp);

                } else if (local_strcasecmp(token, "pad-id3v2") == 0) {
                    id3tag_pad_v2(gfp);

                } else if (local_strcasecmp(token, "pad-id3v2-size") == 0) {
                    argUsed = getIntValue(token, nextArg, &int_value);
                    if (argUsed) {
                        int_value = int_value <= 128000 ? int_value : 128000;
                        int_value = int_value >= 0      ? int_value : 0;
                        id3tag_set_pad(gfp, int_value);
                    }

                } else if (local_strcasecmp(token, "genre-list") == 0) {
                    id3tag_genre_list(genre_list_handler, NULL);
                    return -2;


                } else if (local_strcasecmp(token, "lowpass") == 0) {
                    argUsed = getDoubleValue(token, nextArg, &double_value);
                    if (argUsed) {
                        if (double_value < 0) {
                            lame_set_lowpassfreq(gfp, -1);
                        }
                        else {
                            /* useful are 0.001 kHz...50 kHz, 50 Hz...50000 Hz */
                            if (double_value < 0.001 || double_value > 50000.) {
                                error_printf("Must specify lowpass with --lowpass freq, freq >= 0.001 kHz\n");
                                return -1;
                            }
                            int hz;

                            if (!frequency_to_hz(double_value, 50.0, 0, &hz))
                                return -1;
                            lame_set_lowpassfreq(gfp, hz);
                        }
                    }

                } else if (local_strcasecmp(token, "lowpass-width") == 0) {
                    argUsed = getDoubleValue(token, nextArg, &double_value);
                    if (argUsed) {
                        /* useful are 0.001 kHz...16 kHz, 16 Hz...50000 Hz */
                        if (double_value < 0.001 || double_value > 50000.) {
                            error_printf
                                ("Must specify lowpass width with --lowpass-width freq, freq >= 0.001 kHz\n");
                            return -1;
                        }
                        {
                            int hz;

                            if (!frequency_to_hz(double_value, 16.0, 0, &hz))
                                return -1;
                            lame_set_lowpasswidth(gfp, hz);
                        }
                    }

                } else if (local_strcasecmp(token, "highpass") == 0) {
                    argUsed = getDoubleValue(token, nextArg, &double_value);
                    if (argUsed) {
                        if (double_value < 0.0) {
                            lame_set_highpassfreq(gfp, -1);
                        }
                        else {
                            /* useful are 0.001 kHz...16 kHz, 16 Hz...50000 Hz */
                            if (double_value < 0.001 || double_value > 50000.) {
                                error_printf("Must specify highpass with --highpass freq, freq >= 0.001 kHz\n");
                                return -1;
                            }
                            int hz;

                            if (!frequency_to_hz(double_value, 16.0, 0, &hz))
                                return -1;
                            lame_set_highpassfreq(gfp, hz);
                        }
                    }

                } else if (local_strcasecmp(token, "highpass-width") == 0) {
                    argUsed = getDoubleValue(token, nextArg, &double_value);
                    if (argUsed) {
                        /* useful are 0.001 kHz...16 kHz, 16 Hz...50000 Hz */
                        if (double_value < 0.001 || double_value > 50000.) {
                            error_printf
                                ("Must specify highpass width with --highpass-width freq, freq >= 0.001 kHz\n");
                            return -1;
                        }
                        {
                            int hz;

                            if (!frequency_to_hz(double_value, 16.0, 0, &hz))
                                return -1;
                            lame_set_highpasswidth(gfp, hz);
                        }
                    }

                } else if (local_strcasecmp(token, "comp") == 0) {
                    argUsed = getDoubleValue(token, nextArg, &double_value);
                    if (argUsed) {
                        if (double_value < 1.0) {
                            error_printf("Must specify compression ratio >= 1.0\n");
                            return -1;
                        }
                        else {
                            lame_set_compression_ratio(gfp, (float) double_value);
                        }
                    }

                /* some more GNU-ish options could be added
                 * brief         => few messages on screen (name, status report)
                 * o/output file => specifies output filename
                 * O             => stdout
                 * i/input file  => specifies input filename
                 * I             => stdin
                 */
                } else if (local_strcasecmp(token, "quiet") == 0) {
                    global_ui_config.silent = 10; /* on a scale from 1 to 10 be very silent */

                } else if (local_strcasecmp(token, "silent") == 0) {
                    global_ui_config.silent = 9;

                } else if (local_strcasecmp(token, "brief") == 0) {
                    global_ui_config.silent = -5; /* print few info on screen */

                } else if (local_strcasecmp(token, "verbose") == 0) {
                    global_ui_config.silent = -10; /* print a lot on screen */

                } else if (local_strcasecmp(token, "version") == 0 ||
                           local_strcasecmp(token, "license") == 0) {
                    frontend_print_license(stdout);
                return -2;

                } else if (local_strcasecmp(token, "help") == 0 ||
                           local_strcasecmp(token, "usage") == 0) {
                    if (0 == local_strncasecmp(nextArg, "id3", 3)) {
                        frontend_help_id3tag(stdout);
                    }
                    else if (0 == local_strncasecmp(nextArg, "dev", 3)) {
                        frontend_help_developer_switches(stdout);
                    }
                    else {
                        short_help(gfp, stdout, ProgramName);
                    }
                return -2;

                } else if (local_strcasecmp(token, "longhelp") == 0) {
                    long_help(gfp, stdout, ProgramName, 0 /* lessmode=NO */ );
                return -2;

                } else if (local_strcasecmp(token, "?") == 0) {
#ifdef __unix__
                    FILE   *fp = popen("less -Mqc", "w");
                    long_help(gfp, fp, ProgramName, 0 /* lessmode=NO */ );
                    pclose(fp);
#else
                    long_help(gfp, stdout, ProgramName, 1 /* lessmode=YES */ );
#endif
                return -2;

                } else if (local_strcasecmp(token, "preset") == 0 ||
                           local_strcasecmp(token, "alt-preset") == 0) {
                    argUsed = 1;
                {
                    int     fast = 0, cbr = 0;

                    while ((strcmp(nextArg, "fast") == 0) || (strcmp(nextArg, "cbr") == 0)) {

                        if ((strcmp(nextArg, "fast") == 0) && (fast < 1))
                            fast = 1;
                        if ((strcmp(nextArg, "cbr") == 0) && (cbr < 1))
                            cbr = 1;

                        argUsed++;
                        nextArg = i + argUsed < argc ? argv[i + argUsed] : "";
                    }

                    if (presets_set(gfp, fast, cbr, nextArg, ProgramName) < 0)
                        return -1;
                }

                } else if (local_strcasecmp(token, "disptime") == 0) {
                    argUsed = getDoubleValue(token, nextArg, &double_value);
                    if (argUsed)
                        global_ui_config.update_interval = (float) double_value;

                } else if (local_strcasecmp(token, "nogaptags") == 0) {
                    nogap_tags = 1;

                } else if (local_strcasecmp(token, "nogapout") == 0) {
                    if (argument_missing(token, nextArg) ||
                        copy_path(outPath, PATH_MAX + 1, nextArg,
                                  ProgramName, token) != 0)
                        return -1;
                    argUsed = 1;

                } else if (local_strcasecmp(token, "out-dir") == 0) {
                    if (argument_missing(token, nextArg) ||
                        copy_path(outDir, sizeof(outDir), nextArg,
                                  ProgramName, token) != 0)
                        return -1;
                    argUsed = 1;

                } else if (local_strcasecmp(token, "nogap") == 0) {
                    nogap = 1;

                } else if (local_strcasecmp(token, "swap-channel") == 0) {
                    global_reader.swap_channel = 1;

                } else if (local_strcasecmp(token, "ignorelength") == 0) {
                    global_reader.ignorewavlength = 1;

                } else if (local_strcasecmp(token, "athaa-sensitivity") == 0) {
                    argUsed = getDoubleValue(token, nextArg, &double_value);
                    if (argUsed)
                        lame_set_athaa_sensitivity(gfp, (float) double_value);

                /* ---------------- internal tuning switches ---------------- */

                } else if (dev_only_without_arg("noshort", token, &argIgnored)) {
                    (void) lame_set_no_short_blocks(gfp, 1);

                } else if (dev_only_without_arg("short", token, &argIgnored)) {
                    (void) lame_set_no_short_blocks(gfp, 0);

                } else if (dev_only_without_arg("allshort", token, &argIgnored)) {
                    (void) lame_set_force_short_blocks(gfp, 1);

                } else if (dev_only_without_arg("notemp", token, &argIgnored)) {
                    (void) lame_set_useTemporal(gfp, 0);

                } else if (dev_only_with_arg("interch", token, nextArg, &argIgnored, &argUsed)) {
                    argUsed = getDoubleValue(token, nextArg, &double_value);
                    if (argUsed)
                        (void) lame_set_interChRatio(gfp, (float) double_value);

                } else if (dev_only_with_arg("temporal-masking", token, nextArg, &argIgnored, &argUsed)) {
                    argUsed = getIntValue(token, nextArg, &int_value);
                    if (argUsed)
                        (void) lame_set_useTemporal(gfp, int_value ? 1 : 0);

                } else if (dev_only_without_arg("nspsytune", token, &argIgnored)) {
                    ;

                } else if (dev_only_without_arg("nssafejoint", token, &argIgnored)) {
                    lame_set_exp_nspsytune(gfp, lame_get_exp_nspsytune(gfp) | 2);

                } else if (dev_only_with_arg("nsmsfix", token, nextArg, &argIgnored, &argUsed)) {
                    argUsed = getDoubleValue(token, nextArg, &double_value);
                    if (argUsed)
                        (void) lame_set_msfix(gfp, double_value);

                } else if (dev_only_with_arg("ns-bass", token, nextArg, &argIgnored, &argUsed)) {
                    argUsed = getDoubleValue(token, nextArg, &double_value);
                    if (argUsed) {
                        int k = encode_ns_tuning(double_value);

                        lame_set_exp_nspsytune(
                            gfp, lame_get_exp_nspsytune(gfp) | (k << 2));
                    }

                } else if (dev_only_with_arg("ns-alto", token, nextArg, &argIgnored, &argUsed)) {
                    argUsed = getDoubleValue(token, nextArg, &double_value);
                    if (argUsed) {
                        int k = encode_ns_tuning(double_value);

                        lame_set_exp_nspsytune(
                            gfp, lame_get_exp_nspsytune(gfp) | (k << 8));
                    }

                } else if (dev_only_with_arg("ns-treble", token, nextArg, &argIgnored, &argUsed)) {
                    argUsed = getDoubleValue(token, nextArg, &double_value);
                    if (argUsed) {
                        int k = encode_ns_tuning(double_value);

                        lame_set_exp_nspsytune(
                            gfp, lame_get_exp_nspsytune(gfp) | (k << 14));
                    }

                } else if (dev_only_with_arg("ns-sfb21", token, nextArg, &argIgnored, &argUsed)) {
                    /* Preserve Naoki's original packed ns-sfb21 encoding. */
                    argUsed = getDoubleValue(token, nextArg, &double_value);
                    if (argUsed) {
                        int k = encode_ns_tuning(double_value);

                        lame_set_exp_nspsytune(
                            gfp, lame_get_exp_nspsytune(gfp) | (k << 20));
                    }

                } else if (dev_only_with_arg("tune", token, nextArg, &argIgnored, &argUsed)) { /*without helptext */
                    argUsed = getDoubleValue(token, nextArg, &double_value);
                    if (argUsed)
                        lame_set_tune(gfp, (float) double_value);

                } else if (dev_only_with_arg("shortthreshold", token, nextArg, &argIgnored, &argUsed)) {
                    float x;
                    float y;

                    if (!parse_float_pair(nextArg, &x, &y)) {
                        error_printf("invalid --shortthreshold value '%s'\n",
                                     nextArg);
                        return -1;
                    }

                    (void) lame_set_short_threshold(gfp, x, y);
                } else if (dev_only_with_arg("maskingadjust", token, nextArg, &argIgnored, &argUsed)) { /*without helptext */
                    argUsed = getDoubleValue(token, nextArg, &double_value);
                    if (argUsed)
                        (void) lame_set_maskingadjust(gfp, (float) double_value);

                } else if (dev_only_with_arg("maskingadjustshort", token, nextArg, &argIgnored, &argUsed)) { /*without helptext */
                    argUsed = getDoubleValue(token, nextArg, &double_value);
                    if (argUsed)
                        (void) lame_set_maskingadjust_short(gfp, (float) double_value);

                } else if (dev_only_with_arg("athcurve", token, nextArg, &argIgnored, &argUsed)) { /*without helptext */
                    argUsed = getDoubleValue(token, nextArg, &double_value);
                    if (argUsed)
                        (void) lame_set_ATHcurve(gfp, (float) double_value);

                } else if (dev_only_without_arg("no-preset-tune", token, &argIgnored)) { /*without helptext */
                    (void) lame_set_preset_notune(gfp, 0);

                } else if (dev_only_with_arg("substep", token, nextArg, &argIgnored, &argUsed)) {
                    argUsed = getIntValue(token, nextArg, &int_value);
                    if (argUsed)
                        (void) lame_set_substep(gfp, int_value);

                } else if (dev_only_with_arg("sbgain", token, nextArg, &argIgnored, &argUsed)) { /*without helptext */
                    argUsed = getIntValue(token, nextArg, &int_value);
                    if (argUsed)
                        (void) lame_set_subblock_gain(gfp, int_value);

                } else if (dev_only_without_arg("sfscale", token, &argIgnored)) { /*without helptext */
                    (void) lame_set_sfscale(gfp, 1);

                } else if (dev_only_without_arg("noath", token, &argIgnored)) {
                    (void) lame_set_noATH(gfp, 1);

                } else if (dev_only_without_arg("athonly", token, &argIgnored)) {
                    (void) lame_set_ATHonly(gfp, 1);

                } else if (dev_only_without_arg("athshort", token, &argIgnored)) {
                    (void) lame_set_ATHshort(gfp, 1);

                } else if (dev_only_with_arg("athlower", token, nextArg, &argIgnored, &argUsed)) {
                    argUsed = getDoubleValue(token, nextArg, &double_value);
                    if (argUsed)
                        (void) lame_set_ATHlower(gfp, (float) double_value);

                } else if (dev_only_with_arg("athtype", token, nextArg, &argIgnored, &argUsed)) {
                    argUsed = getIntValue(token, nextArg, &int_value);
                    if (argUsed)
                        (void) lame_set_ATHtype(gfp, int_value);

                } else if (dev_only_with_arg("athaa-type", token, nextArg, &argIgnored, &argUsed)) { /*  switch for developing, no DOCU */
                    /* once was 1:Gaby, 2:Robert, 3:Jon, else:off */
                    argUsed = getIntValue(token, nextArg, &int_value);
                    if (argUsed)
                        (void) lame_set_athaa_type(gfp, int_value); /* now: 0:off else:Jon */

                } else if (dev_only_with_arg("debug-file", token, nextArg, &argIgnored, &argUsed)) { /* switch for developing, no DOCU */
                    if (argument_missing(token, nextArg))
                        return -1;
                    set_debug_file(nextArg);

                } else {
                    if (!argIgnored) {
                        error_printf("%s: unrecognized option --%s\n", ProgramName, token);
                        return -1;
                    }
                    argIgnored = 0;
                }
                i += argUsed;

            }
            else {
                char    c;
                while ((c = *token++) != '\0') {
                    double double_value = 0;
                    int int_value = 0;
                    char const *arg = *token ? token : nextArg;
                    switch (c) {
                    case 'm':
                        argUsed = 1;

                        switch (*arg) {
                        case 's':
                            (void) lame_set_mode(gfp, STEREO);
                            break;
                        case 'd':
                            (void) lame_set_mode(gfp, DUAL_CHANNEL);
                            break;
                        case 'f':
                            lame_set_force_ms(gfp, 1);
                            (void) lame_set_mode(gfp, JOINT_STEREO);
                            break;
                        case 'j':
                            lame_set_force_ms(gfp, 0);
                            (void) lame_set_mode(gfp, JOINT_STEREO);
                            break;
                        case 'm':
                            (void) lame_set_mode(gfp, MONO);
                            break;
                        case 'l':
                            (void) lame_set_mode(gfp, MONO);
                            (void) lame_set_scale_left(gfp, 2);
                            (void) lame_set_scale_right(gfp, 0);
                            break;
                        case 'r':
                            (void) lame_set_mode(gfp, MONO);
                            (void) lame_set_scale_left(gfp, 0);
                            (void) lame_set_scale_right(gfp, 2);
                            break;
                        case 'a': /* same as 'j' ??? */
                            lame_set_force_ms(gfp, 0);
                            (void) lame_set_mode(gfp, JOINT_STEREO);
                            break;
                        default:
                            error_printf("%s: -m mode must be s/d/f/j/m/l/r not %s\n", ProgramName,
                                         arg);
                            return -1;
                        }
                        break;

                    case 'V':
                        argUsed = getDoubleValue("V", arg, &double_value);
                        if (argUsed) {
                            /* to change VBR default look in lame.h */
                            if (lame_get_VBR(gfp) == vbr_off)
                                lame_set_VBR(gfp, vbr_default);
                            lame_set_VBR_quality(gfp, (float) double_value);
                        }
                        break;
                    case 'v':
                        /* to change VBR default look in lame.h */
                        if (lame_get_VBR(gfp) == vbr_off)
                            lame_set_VBR(gfp, vbr_default);
                        break;

                    case 'q':
                        argUsed = getIntValue("q", arg, &int_value);
                        if (argUsed)
                            (void) lame_set_quality(gfp, int_value);
                        break;
                    case 'f':
                        (void) lame_set_quality(gfp, 7);
                        break;
                    case 'h':
                        (void) lame_set_quality(gfp, 2);
                        break;

                    case 's':
                        argUsed = getDoubleValue("s", arg, &double_value);
                        if (argUsed) {
                            int hz;

                            if (!frequency_to_hz(double_value, 192.0, 1, &hz)) {
                                error_printf("%s: invalid input sample rate '%s'\n",
                                             ProgramName, arg);
                                return -1;
                            }

                            global_reader.input_samplerate = hz;
                            (void) lame_set_in_samplerate(gfp, hz);
                        }
                        break;
                    case 'b':
                        argUsed = getIntValue("b", arg, &int_value);
                        if (argUsed) {
                            lame_set_brate(gfp, int_value);
                            lame_set_VBR_min_bitrate_kbps(gfp, lame_get_brate(gfp));
                        }
                        break;
                    case 'B':
                        argUsed = getIntValue("B", arg, &int_value);
                        if (argUsed) {
                            lame_set_VBR_max_bitrate_kbps(gfp, int_value);
                        }
                        break;
                    case 'F':
                        lame_set_VBR_hard_min(gfp, 1);
                        break;
                    case 't': /* dont write VBR tag */
                        (void) lame_set_bWriteVbrTag(gfp, 0);
                        global_decoder.disable_wav_header = 1;
                        break;
                    case 'T': /* do write VBR tag */
                        (void) lame_set_bWriteVbrTag(gfp, 1);
                        nogap_tags = 1;
                        global_decoder.disable_wav_header = 0;
                        break;
                    case 'r': /* force raw pcm input file */
#if defined(LIBSNDFILE)
                        error_printf
                            ("WARNING: libsndfile may ignore -r and perform fseek's on the input.\n"
                             "Compile without libsndfile if this is a problem.\n");
#endif
                        global_reader.input_format = sf_raw;
                        break;
                    case 'x': /* force byte swapping */
                        global_reader.swapbytes = 1;
                        break;
                    case 'p': /* (jo) error_protection: add crc16 information to stream */
                        lame_set_error_protection(gfp, 1);
                        break;
                    case 'a': /* autoconvert input file from stereo to mono - for mono mp3 encoding */
                        autoconvert = 1;
                        (void) lame_set_mode(gfp, MONO);
                        break;
                    case 'S':
                        global_ui_config.silent = 5;
                        break;
                    case 'X':
                        /*  experimental switch -X:
                            the differnt types of quant compare are tough
                            to communicate to endusers, so they shouldn't
                            bother to toy around with them
                         */
                        {
                            int x;
                            int y;

                            if (!parse_int_pair(arg, &x, &y)) {
                                error_printf("%s: invalid -X value '%s'\n",
                                             ProgramName, arg);
                                return -1;
                            }

                            argUsed = 1;
                            if (internal_opts_enabled) {
                                lame_set_quant_comp(gfp, x);
                                lame_set_quant_comp_short(gfp, y);
                            }
                        }
                        break;
                    case 'Y':
                        lame_set_experimentalY(gfp, 1);
                        break;
                    case 'Z':
                        /*  experimental switch -Z:
                         */
                        {
                            int n = 1;

                            argUsed = getIntValue("Z", arg, &n);
                            if (argUsed)
                                lame_set_experimentalZ(gfp, n);
                        }
                        break;
                    case 'c':
                        lame_set_copyright(gfp, 1);
                        break;
                    case 'o':
                        lame_set_original(gfp, 0);
                        break;

                    case '?':
                        long_help(gfp, stdout, ProgramName, 0 /* LESSMODE=NO */ );
                        return -1;

                    default:
                        error_printf("%s: unrecognized option -%c\n", ProgramName, c);
                        return -1;
                    }
                    if (argUsed) {
                        if (arg == token)
                            token = ""; /* no more from token */
                        else
                            ++i; /* skip arg we used */
                        arg = "";
                        argUsed = 0;
                    }
                }
            }
        }
        else {
            if (nogap) {
                if (num_nogap != NULL && nogap_inPath != NULL &&
                    count_nogap < *num_nogap &&
                    nogap_inPath[count_nogap] != NULL) {
                    if (copy_path(nogap_inPath[count_nogap], PATH_MAX + 1,
                                  argv[i], ProgramName, "nogap input") != 0)
                        return -1;
                    ++count_nogap;
                    input_file = 1;
                }
                else {
                    /* sorry, calling program did not allocate enough space */
                    error_printf
                        ("Error: 'nogap option'.  Calling program does not allow nogap option, or\n"
                         "you have exceeded maximum number of input files for the nogap option\n");
                    if (num_nogap) {
                        *num_nogap = -1;
                    }
                    return -1;
                }
            }
            else {
                /* normal options:   inputfile  [outputfile], and
                   either one can be a '-' for stdin/stdout */
                if (inPath[0] == '\0') {
                    if (copy_path(inPath, PATH_MAX + 1, argv[i],
                                  ProgramName, "input") != 0)
                        return -1;
                    input_file = 1;
                }
                else {
                    if (outPath[0] == '\0') {
                        if (copy_path(outPath, PATH_MAX + 1, argv[i],
                                      ProgramName, "output") != 0)
                            return -1;
                    }
                    else {
                        error_printf("%s: excess arg %s\n", ProgramName, argv[i]);
                        return -1;
                    }
                }
            }
        }
    }                   /* loop over args */

    if (!input_file) {
        usage(Console_IO.Console_fp, ProgramName);
        return -1;
    }

    if (lame_get_decode_only(gfp) && count_nogap > 0) {
        error_printf("combination of nogap and decode not supported!\n");
        return -1;
    }

    if (inPath[0] == '-') {
        if (global_ui_config.silent == 0) { /* user didn't overrule default behaviour */
            global_ui_config.silent = 1;
        }
    }

    if (outPath[0] == '\0') { /* no explicit output dir or file */
        if (count_nogap > 0) { /* in case of nogap encode */
            if (copy_path(outPath, PATH_MAX + 1, outDir,
                          ProgramName, "out-dir") != 0)
                return -1;
        }
        else if (inPath[0] == '-') {
            /* if input is stdin, default output is stdout */
            strcpy(outPath, "-");
        }
        else {
            char const* s_ext = lame_get_decode_only(gfp) ? ".wav" : ".mp3";
            if (generateOutPath(inPath, outDir, s_ext, outPath) != 0) {
                return -1;
            }
        }
    }

    /* RG is enabled by default */
    if (!noreplaygain)
        lame_set_findReplayGain(gfp, 1);

    /* disable VBR tags with nogap unless the VBR tags are forced */
    if (nogap && lame_get_bWriteVbrTag(gfp) && nogap_tags == 0) {
        console_printf("Note: Disabling VBR Xing/Info tag since it interferes with --nogap\n");
        lame_set_bWriteVbrTag(gfp, 0);
    }

    /* some file options not allowed with stdout */
    if (outPath[0] == '-') {
        (void) lame_set_bWriteVbrTag(gfp, 0); /* turn off VBR tag */
    }

    /* if user did not explicitly specify input is mp3, check file name */
    if (global_reader.input_format == sf_unknown)
        global_reader.input_format = filename_to_type(inPath);

#if !(defined AMIGA_MPEGA || defined HAVE_MPG123)
    if (is_mpeg_file_format(global_reader.input_format)) {
        error_printf("Error: libmp3lame not compiled with mpg123 *decoding* support \n");
        return -1;
    }
#endif

    /* default guess for number of channels */
    if (autoconvert)
        (void) lame_set_num_channels(gfp, 2);
    else if (MONO == lame_get_mode(gfp))
        (void) lame_set_num_channels(gfp, 1);
    else
        (void) lame_set_num_channels(gfp, 2);

    if (lame_get_free_format(gfp)) {
        if (lame_get_brate(gfp) < 8 || lame_get_brate(gfp) > 640) {
            error_printf("For free format, specify a bitrate between 8 and 640 kbps\n");
            error_printf("with the -b <bitrate> option\n");
            return -1;
        }
    }
    if (num_nogap != NULL)
        *num_nogap = count_nogap;
    return 0;
}

static int
string_to_argv(char *str, char **argv, size_t capacity)
{
    size_t argc = 0;
    char *read;
    char *write;

    if (str == NULL)
        return 0;

    if (argv == NULL || capacity == 0)
        return -1;

    argv[argc++] = (char *) "lamer";
    read = str;
    write = str;

    while (*read != '\0') {
        int quoted = 0;

        while (isspace((unsigned char) *read))
            ++read;

        if (*read == '\0')
            break;

        if (argc >= capacity) {
            error_printf("LAMEOPT contains too many arguments\n");
            return -1;
        }

        argv[argc++] = write;

        while (*read != '\0') {
            unsigned char ch = (unsigned char) *read++;

            if (quoted) {
                if (ch == '"') {
                    quoted = 0;
                    continue;
                }

                if (ch == '\\' &&
                    (*read == '"' || *read == '\\'))
                    ch = (unsigned char) *read++;

                *write++ = (char) ch;
                continue;
            }

            if (ch == '"') {
                quoted = 1;
                continue;
            }

            if (isspace(ch))
                break;

            if (ch == '\\' && *read != '\0')
                ch = (unsigned char) *read++;

            *write++ = (char) ch;
        }

        if (quoted) {
            error_printf("LAMEOPT contains an unterminated quoted argument\n");
            return -1;
        }

        *write++ = '\0';
    }

    return (int) argc;
}


static int
merge_argv(int argc, char **argv, int env_argc,
           char **merged_argv, size_t capacity)
{
    size_t merged_argc;
    int i;

    if (merged_argv == NULL || capacity == 0 || argc < 0 || env_argc < 0)
        return -1;

    merged_argc = (size_t) env_argc;

    if (argc > 0 && argv != NULL) {
        merged_argv[0] = argv[0];

        if (merged_argc == 0)
            merged_argc = 1;
    }

    for (i = 1; i < argc; ++i) {
        if (merged_argc >= capacity) {
            error_printf("too many command-line arguments\n");
            return -1;
        }

        merged_argv[merged_argc++] = argv[i];
    }

    return (int) merged_argc;
}


#ifdef DEBUG
static void
dump_argv(int argc, char** argv)
{
    int     i;
    for (i = 0; i < argc; ++i) {
        printf("%d: \"%s\"\n",i,argv[i]);
    }
}
#endif


int
parse_args(lame_t gfp, int argc, char **argv,
           char *const inPath, char *const outPath,
           char **nogap_inPath, int *num_nogap)
{
    char *merged_argv[512] = { NULL };
    char *env_options;
    int env_argc;
    int merged_argc;
    int ret;

    if (argc < 0 || (argc > 0 && argv == NULL))
        return -1;

    env_options = lame_getenv("LAMEOPT");

    env_argc = string_to_argv(
        env_options, merged_argv, ARRAY_SIZE(merged_argv));

    if (env_argc < 0) {
        free(env_options);
        return -1;
    }

    merged_argc = merge_argv(
        argc, argv, env_argc, merged_argv, ARRAY_SIZE(merged_argv));

    if (merged_argc < 0) {
        free(env_options);
        return -1;
    }

#ifdef DEBUG
    dump_argv(merged_argc, merged_argv);
#endif

    ret = parse_args_(gfp, merged_argc, merged_argv,
                      inPath, outPath, nogap_inPath, num_nogap);

    free(env_options);
    return ret;
}

/* end of parse.c */
