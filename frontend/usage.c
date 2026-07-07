/*
 *      Command line usage and help text
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

#include <stdio.h>
#include <string.h>

#include "usage.h"
#include "version.h"

#if defined _ALLOW_INTERNAL_OPTIONS
#define INTERNAL_OPTS 1
#else
#define INTERNAL_OPTS 0
#endif

#if (INTERNAL_OPTS!=0)
#define DEV_HELP(a) a
#else
#define DEV_HELP(a)
#endif

#ifdef HAVE_ICONV
#define ID3TAGS_EXTENDED
#endif

static int const lame_alpha_version_enabled = LAME_ALPHA_VERSION;
static int const internal_opts_enabled = INTERNAL_OPTS;

static void
wait_for(FILE * const fp, int lessmode)
{
    if (lessmode) {
        fflush(fp);
        getchar();
    }
    else {
        fprintf(fp, "\n");
    }
    fprintf(fp, "\n");
}

static void
display_bitrate(FILE * const fp, const char *const version, const int d, const int indx)
{
    int     i;
    int nBitrates = 14;
    if (d == 4)
        nBitrates = 8;

    fprintf(fp,
            "\nMPEG-%-3s layer III sample frequencies (kHz):  %2d  %2d  %g\n"
            "bitrates (kbps):", version, 32 / d, 48 / d, 44.1 / d);
    for (i = 1; i <= nBitrates; i++)
        fprintf(fp, " %2i", lame_get_bitrate(indx, i));
    fprintf(fp, "\n");
}

int
frontend_version_print(FILE * const fp)
{
    const char *b = get_lame_os_bitness();
    const char *v = get_lame_version();
    const char *u = get_lame_url();
    const size_t lenb = strlen(b);
    const size_t lenv = strlen(v);
    const size_t lenu = strlen(u);
    const size_t lw = 80;
    const size_t sw = 16;

    if (lw >= lenb + lenv + lenu + sw || lw < lenu + 2)
        if (lenb > 0)
            fprintf(fp, "LAME %s version %s (%s)\n\n", b, v, u);
        else
            fprintf(fp, "LAME version %s (%s)\n\n", v, u);
    else {
        int const n_white_spaces = (int)((lenu + 2) > lw ? 0 : lw - 2 - lenu);
        if (lenb > 0)
            fprintf(fp, "LAME %s version %s\n%*s(%s)\n\n", b, v, n_white_spaces, "", u);
        else
            fprintf(fp, "LAME version %s\n%*s(%s)\n\n", v, n_white_spaces, "", u);
    }
    if (lame_alpha_version_enabled)
        fprintf(fp, "warning: alpha versions should be used for testing only\n\n");

    return 0;
}

int
frontend_print_license(FILE * const fp)
{
    frontend_version_print(fp);
    fprintf(fp,
            "Copyright (c) 1999-2011 by The LAME Project\n"
            "Copyright (c) 1999,2000,2001 by Mark Taylor\n"
            "Copyright (c) 1998 by Michael Cheng\n" "\n");
    fprintf(fp,
            "This library is free software; you can redistribute it and/or\n"
            "modify it under the terms of the GNU Library General Public\n"
            "License as published by the Free Software Foundation; either\n"
            "version 2 of the License, or (at your option) any later version.\n"
            "\n");
    fprintf(fp,
            "This library is distributed in the hope that it will be useful,\n"
            "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
            "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU\n"
            "Library General Public License for more details.\n"
            "\n");
    fprintf(fp,
            "You should have received a copy of the GNU Library General Public\n"
            "License along with this program. If not, see\n"
            "<http://www.gnu.org/licenses/>.\n");
    return 0;
}

int
usage(FILE * const fp, const char *ProgramName)
{
    frontend_version_print(fp);
    fprintf(fp,
            "usage: %s [options] <infile> [outfile]\n"
            "\n"
            "    <infile> and/or <outfile> can be \"-\", which means stdin/stdout.\n"
            "\n"
            "Try:\n"
            "     \"%s --help\"           for general usage information\n"
            " or:\n"
            "     \"%s --preset help\"    for information on suggested predefined settings\n"
            " or:\n"
            "     \"%s --longhelp\"\n"
            "  or \"%s -?\"              for a complete options list\n\n",
            ProgramName, ProgramName, ProgramName, ProgramName, ProgramName);
    return 0;
}

int
short_help(const lame_global_flags * gfp, FILE * const fp, const char *ProgramName)
{
    frontend_version_print(fp);
    fprintf(fp,
            "usage: %s [options] <infile> [outfile]\n"
            "\n"
            "    <infile> and/or <outfile> can be \"-\", which means stdin/stdout.\n"
            "\n" "RECOMMENDED:\n" "    lame -V2 input.wav output.mp3\n" "\n", ProgramName);
    fprintf(fp,
            "OPTIONS:\n"
            "    -b bitrate      set the bitrate, default 128 kbps\n"
            "    -h              higher quality, but a little slower.\n"
            "    -f              fast mode (lower quality)\n"
            "    -V n            quality setting for VBR.  default n=%i\n"
            "                    0=high quality,bigger files. 9.999=smaller files\n",
            lame_get_VBR_q(gfp));
    fprintf(fp,
            "    --preset type   type must be \"medium\", \"standard\", \"extreme\", \"insane\",\n"
            "                    or a value for an average desired bitrate and depending\n"
            "                    on the value specified, appropriate quality settings will\n"
            "                    be used.\n"
            "                    \"--preset help\" gives more info on these\n" "\n");
    fprintf(fp,
            "    --help id3      ID3 tagging related options\n" "\n"
            DEV_HELP(
            "    --help dev      developer options\n" "\n"
            )
            "    --longhelp      full list of options\n" "\n"
            "    --license       print License information\n\n"
            );

    return 0;
}

void
frontend_help_id3tag(FILE * const fp)
{
    fprintf(fp,
            "  ID3 tag options:\n"
            "    --tt <title>    audio/song title (max 30 chars for version 1 tag)\n"
            "    --ta <artist>   audio/song artist (max 30 chars for version 1 tag)\n"
            "    --tl <album>    audio/song album (max 30 chars for version 1 tag)\n"
            "    --ty <year>     audio/song year of issue (1 to 9999)\n"
            "    --tc <comment>  user-defined text (max 30 chars for v1 tag, 28 for v1.1)\n");
    fprintf(fp,
            "    --tn <track[/total]>   audio/song track number and (optionally) the total\n"
            "                           number of tracks on the original recording. (track\n"
            "                           and total each 1 to 255. just the track number\n"
            "                           creates v1.1 tag, providing a total forces v2.0).\n");
    fprintf(fp,
            "    --tg <genre>    audio/song genre (name or number in list)\n"
            "    --ti <file>     audio/song albumArt (jpeg/png/gif file, v2.3 tag)\n"
            "    --tv <id=value> user-defined frame specified by id and value (v2.3 tag)\n"
            "                    syntax: --tv \"TXXX=description=content\"\n"
            );
    fprintf(fp,
            "    --add-id3v2     force addition of version 2 tag\n"
            "    --id3v1-only    add only a version 1 tag\n"
            "    --id3v2-only    add only a version 2 tag\n"
#ifdef ID3TAGS_EXTENDED
            "    --id3v2-utf8    add following options in unicode UTF-8 text encoding\n"
            "    --id3v2-utf16   add following options in unicode UTF-16 text encoding\n"
            "    --id3v2-latin1  add following options in latin-1 text encoding\n"
#endif
            "    --space-id3v1   pad version 1 tag with spaces instead of nulls\n"
            "    --pad-id3v2     same as '--pad-id3v2-size 128'\n"
            "    --pad-id3v2-size <value> adds version 2 tag, pad with extra <value> bytes\n"
            "    --genre-list    print alphabetically sorted ID3 genre list and exit\n"
            "    --ignore-tag-errors  ignore errors in values passed for tags\n" "\n"
            );
    fprintf(fp,
            "    Note: A version 2 tag will NOT be added unless one of the input fields\n"
            "    won't fit in a version 1 tag (e.g. the title string is longer than 30\n"
            "    characters), or the '--add-id3v2' or '--id3v2-only' options are used,\n"
            "    or output is redirected to stdout.\n"
            );
}

void
frontend_help_developer_switches(FILE * const fp)
{
    if (!internal_opts_enabled) {
        fprintf(fp,
                "    Note: Almost all of the following switches aren't available in this build!\n\n"
                );
    }
    fprintf(fp,
            "  ATH related:\n"
            "    --noath         turns ATH down to a flat noise floor\n"
            "    --athshort      ignore GPSYCHO for short blocks, use ATH only\n"
            "    --athonly       ignore GPSYCHO completely, use ATH only\n"
            "    --athtype n     selects between different ATH types [0-4]\n"
            "    --athlower x    lowers ATH by x dB\n"
            );
    fprintf(fp,
            "    --athaa-type n  ATH auto adjust: 0 'no' else 'loudness based'\n"
            "    --athaa-sensitivity x  activation offset in -/+ dB for ATH auto-adjustment\n"
            "\n");
    fprintf(fp,
            "  PSY related:\n"
            "    --short         use short blocks when appropriate\n"
            "    --noshort       do not use short blocks\n"
            "    --allshort      use only short blocks\n"
            );
    fprintf(fp,
            "(1) --temporal-masking x   x=0 disables, x=1 enables temporal masking effect\n"
            "    --nssafejoint   M/S switching criterion\n"
            "    --nsmsfix <arg> M/S switching tuning [effective 0-3.5]\n"
            "(2) --interch x     adjust inter-channel masking ratio\n"
            "    --ns-bass x     adjust masking for sfbs  0 -  6 (long)  0 -  5 (short)\n"
            "    --ns-alto x     adjust masking for sfbs  7 - 13 (long)  6 - 10 (short)\n"
            "    --ns-treble x   adjust masking for sfbs 14 - 21 (long) 11 - 12 (short)\n"
            );
    fprintf(fp,
            "    --ns-sfb21 x    change ns-treble by x dB for sfb21\n"
            "    --shortthreshold x,y  short block switching threshold,\n"
            "                          x for L/R/M channel, y for S channel\n"
            "    -Z [n]          always do calculate short block maskings\n");
    fprintf(fp,
            "  Noise Shaping related:\n"
            "(1) --substep n     use pseudo substep noise shaping method types 0-2\n"
            "(1) -X n[,m]        selects between different noise measurements\n"
            "                    n for long block, m for short. if m is omitted, m = n\n"
            " 1: CBR, ABR and VBR-old encoding modes only\n"
            " 2: ignored\n"
           );
}

int
long_help(const lame_global_flags * gfp, FILE * const fp, const char *ProgramName, int lessmode)
{
    frontend_version_print(fp);
    fprintf(fp,
            "usage: %s [options] <infile> [outfile]\n"
            "\n"
            "    <infile> and/or <outfile> can be \"-\", which means stdin/stdout.\n"
            "\n" "RECOMMENDED:\n" "    lame -V2 input.wav output.mp3\n" "\n", ProgramName);
    fprintf(fp,
            "OPTIONS:\n"
            "  Input options:\n"
            "    --scale <arg>   scale input (multiply PCM data) by <arg>\n"
            "    --scale-l <arg> scale channel 0 (left) input (multiply PCM data) by <arg>\n"
            "    --scale-r <arg> scale channel 1 (right) input (multiply PCM data) by <arg>\n"
            "    --swap-channel  swap L/R channels\n"
            "    --ignorelength  ignore file length in WAV header\n"
            "    --gain <arg>    apply Gain adjustment in decibels, range -20.0 to +12.0\n"
           );
#if (defined HAVE_MPG123 || defined AMIGA_MPEGA)
    fprintf(fp,
            "    --mp1input      input file is a MPEG Layer I   file\n"
            "    --mp2input      input file is a MPEG Layer II  file\n"
            "    --mp3input      input file is a MPEG Layer III file\n"
           );
#endif
    fprintf(fp,
            "    --nogap <file1> <file2> <...>\n"
            "                    gapless encoding for a set of contiguous files\n"
            "    --nogapout <dir>\n"
            "                    output dir for gapless encoding (must precede --nogap)\n"
            "    --nogaptags     allow the use of VBR tags in gapless encoding\n"
            "    --out-dir <dir> output dir, must exist\n"
           );
    fprintf(fp,
            "\n"
            "  Input options for RAW PCM:\n"
            "    -r              input is raw pcm\n"
            "    -s sfreq        sampling frequency of input file (kHz) - default 44.1 kHz\n"
            "    --signed        input is signed (default)\n"
            "    --unsigned      input is unsigned\n"
            "    --bitwidth w    input bit width is w (default 16)\n"
            "    -x              force byte-swapping of input\n"
            "    --little-endian input is little-endian (default)\n"
            "    --big-endian    input is big-endian\n"
            "    -a              downmix from stereo to mono file for mono encoding\n"
           );

    wait_for(fp, lessmode);
    fprintf(fp,
            "  Operational options:\n"
            "    -m <mode>       (j)oint, (s)imple, (f)orce, (d)ual-mono, (m)ono (l)eft (r)ight\n"
            "                    default is (j)\n"
            "                    joint  = Uses the best possible of MS and LR stereo\n"
            "                    simple = force LR stereo on all frames\n"
            "                    force  = force MS stereo on all frames.\n"
           );
    fprintf(fp,
            "    --preset type   type must be \"medium\", \"standard\", \"extreme\", \"insane\",\n"
            "                    or a value for an average desired bitrate and depending\n"
            "                    on the value specified, appropriate quality settings will\n"
            "                    be used.\n"
            "                    \"--preset help\" gives more info on these\n"
            "    --comp  <arg>   choose bitrate to achieve a compression ratio of <arg>\n");
    fprintf(fp, "    --replaygain-fast   compute RG fast but slightly inaccurately (default)\n"
#ifdef DECODE_ON_THE_FLY
            "    --replaygain-accurate   compute RG more accurately and find the peak sample\n"
#endif
            "    --noreplaygain  disable ReplayGain analysis\n"
#ifdef DECODE_ON_THE_FLY
            "    --clipdetect    enable --replaygain-accurate and print a message whether\n"
            "                    clipping occurs and how far the waveform is from full scale\n"
#endif
        );
    fprintf(fp,
            "    --flush         flush output stream as soon as possible\n"
            "    --freeformat    produce a free format bitstream\n"
            "    --decode        input=mp3 file, output=wav\n"
            "    -t              disable writing wav header when using --decode\n");

    wait_for(fp, lessmode);
    fprintf(fp,
            "  Verbosity:\n"
            "    --disptime <arg>print progress report every arg seconds\n"
            "    -S              don't print progress report, VBR histograms\n"
            "    --nohist        disable VBR histogram display\n"
            "    --quiet         don't print anything on screen\n"
            "    --silent        don't print anything on screen, but fatal errors\n"
            "    --brief         print more useful information\n"
            "    --verbose       print a lot of useful information\n" "\n");
    fprintf(fp,
            "  Noise shaping & psycho acoustic algorithms:\n"
            "    -q <arg>        <arg> = 0...9.  Default  -q 3 \n"
            "                    -q 0:  Highest quality, very slow \n"
            "                    -q 9:  Poor quality, but fast \n"
            "    -h              Same as -q 2.   \n"
            "    -f              Same as -q 7.   Fast, ok quality\n");

    wait_for(fp, lessmode);
    fprintf(fp,
            "  CBR (constant bitrate, the default) options:\n"
            "    -b <bitrate>    set the bitrate in kbps, default 128 kbps\n"
            "    --cbr           enforce use of constant bitrate\n"
            "\n"
            "  ABR options:\n"
            "    --abr <bitrate> specify average bitrate desired (instead of quality)\n" "\n");
    fprintf(fp,
            "  VBR options:\n"
            "    -V n            quality setting for VBR.  default n=%i\n"
            "                    0=high quality,bigger files. 9=smaller files\n"
            "    -v              the same as -V 4\n"
            "    --vbr-old       use old variable bitrate (VBR) routine\n"
            "    --vbr-new       use new variable bitrate (VBR) routine (default)\n"
            "    -Y              lets LAME ignore noise in sfb21, like in CBR\n"
            "                    (Default for V3 to V9.999)\n"
            ,
            lame_get_VBR_q(gfp));
    fprintf(fp,
            "    -b <bitrate>    specify minimum allowed bitrate, default  32 kbps\n"
            "    -B <bitrate>    specify maximum allowed bitrate, default 320 kbps\n"
            "    -F              strictly enforce the -b option, for use with players that\n"
            "                    do not support low bitrate mp3\n"
            "    -t              disable writing LAME Tag\n"
            "    -T              enable and force writing LAME Tag\n");

    wait_for(fp, lessmode);
    DEV_HELP(
        frontend_help_developer_switches(fp);
        wait_for(fp, lessmode);
    )

    fprintf(fp,
            "  MP3 header/stream options:\n"
            "    -c              mark as copyright\n"
            "    -o              mark as non-original\n"
            "    -p              error protection.  adds 16 bit checksum to every frame\n"
            "                    (the checksum is computed correctly)\n"
            "    --nores         disable the bit reservoir\n"
            "    --strictly-enforce-ISO   comply as much as possible to ISO MPEG spec\n");
    fprintf(fp,
            "    --buffer-constraint <constraint> available values for constraint:\n"
            "                                     default, strict, maximum\n"
            "\n"
            );
    fprintf(fp,
            "  Filter options:\n"
            "  --lowpass <freq>        frequency(kHz), lowpass filter cutoff above freq\n"
            "  --lowpass-width <freq>  frequency(kHz) - default 15%% of lowpass freq\n"
            "  --highpass <freq>       frequency(kHz), highpass filter cutoff below freq\n"
            "  --highpass-width <freq> frequency(kHz) - default 15%% of highpass freq\n");
    fprintf(fp,
            "  --resample <sfreq>  sampling frequency of output file(kHz)- default=automatic\n");

    wait_for(fp, lessmode);
    frontend_help_id3tag(fp);
    fprintf(fp,
            "\nMisc:\n    --license       print License information\n\n"
        );

#if defined(HAVE_NASM)
    wait_for(fp, lessmode);
    fprintf(fp,
            "  Platform specific:\n"
            "    --noasm <instructions> disable assembly optimizations for mmx/3dnow/sse\n");
    wait_for(fp, lessmode);
#endif

    display_bitrates(fp);

    return 0;
}

int
display_bitrates(FILE * const fp)
{
    display_bitrate(fp, "1", 1, 1);
    display_bitrate(fp, "2", 2, 0);
    display_bitrate(fp, "2.5", 4, 0);
    fprintf(fp, "\n");
    fflush(fp);
    return 0;
}

void
frontend_presets_longinfo(FILE * const msgfp)
{
    fprintf(msgfp,
            "\n"
            "The --preset switches are aliases over LAME settings.\n"
            "\n" "\n");
    fprintf(msgfp,
            "To activate these presets:\n"
            "\n" "   For VBR modes (generally highest quality):\n" "\n");
    fprintf(msgfp,
            "     --preset medium      This preset should provide near transparency to most\n"
            "                          people on most music.\n"
            "\n"
            "     --preset standard    This preset should generally be transparent to most\n"
            "                          people on most music and is already quite high\n"
            "                          in quality.\n" "\n");
    fprintf(msgfp,
            "     --preset extreme     If you have extremely good hearing and similar\n"
            "                          equipment, this preset will generally provide\n"
            "                          slightly higher quality than the \"standard\" mode.\n"
            "                          In lamer, this preset keeps a conservative VBR\n"
            "                          floor by default so active music does not fall\n"
            "                          into obviously low-rate frames unless you\n"
            "                          explicitly lower the floor with -b.\n" "\n");
    fprintf(msgfp,
            "   For CBR 320kbps (highest quality possible from the --preset switches):\n"
            "\n"
            "     --preset insane      This preset will usually be overkill for most people\n"
            "                          and most situations, but if you must have the\n"
            "                          absolute highest quality with no regard to filesize,\n"
            "                          this is the way to go.\n" "\n");
    fprintf(msgfp,
            "   For ABR modes (high quality per given bitrate but not as high as VBR):\n"
            "\n"
            "     --preset <kbps>      Using this preset will usually give you good quality\n"
            "                          at a specified bitrate. Depending on the bitrate\n"
            "                          entered, this preset will determine the optimal\n"
            "                          settings for that particular situation. For example:\n"
            "                          \"--preset 185\" activates this preset and uses 185\n"
            "                          as an average kbps.\n" "\n");
    fprintf(msgfp,
            "   \"cbr\"  - If you use the ABR mode (read above) with a significant\n"
            "            bitrate such as 80, 96, 112, 128, 160, 192, 224, 256, 320,\n"
            "            you can use the \"cbr\" option to force CBR mode encoding\n"
            "            instead of the standard abr mode. ABR does provide higher\n"
            "            quality but CBR may be useful in situations such as when\n"
            "            streaming an mp3 over the internet may be important.\n" "\n");
    fprintf(msgfp,
            "    For example:\n"
            "\n"
            "    --preset standard <input file> <output file>\n"
            " or --preset cbr 192 <input file> <output file>\n"
            " or --preset 172 <input file> <output file>\n"
            " or --preset extreme <input file> <output file>\n" "\n" "\n");
    fprintf(msgfp,
            "A few aliases are also available for ABR mode:\n"
            "phone => 16kbps/mono        phon+/lw/mw-eu/sw => 24kbps/mono\n"
            "mw-us => 40kbps/mono        voice => 56kbps/mono\n"
            "fm/radio/tape => 112kbps    hifi => 160kbps\n"
            "cd => 192kbps               studio => 256kbps\n");
}
