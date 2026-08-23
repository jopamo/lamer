/* -*- mode: C; mode: fold -*- */
/*
 *      LAME MP3 encoding engine
 *
 *      Copyright (c) 1999-2000 Mark Taylor
 *      Copyright (c) 2000-2005 Takehiro Tominaga
 *      Copyright (c) 2000-2019 Robert Hegemann
 *      Copyright (c) 2000-2005 Gabriel Bouvigne
 *      Copyright (c) 2000-2004 Alexander Leidinger
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "lame.h"
#include "machine.h"

#include "encoder.h"
#include "util.h"
#include "lame_global_flags.h"
#include "version.h"
#include "tables.h"

#ifndef LAME_ENABLE_SAFE_TRANSIENT_REDIST_DEFAULT
#define LAME_ENABLE_SAFE_TRANSIENT_REDIST_DEFAULT 1
#endif

#if defined(__FreeBSD__) && !defined(__alpha__)
#include <floatingpoint.h>
#endif
#ifdef __riscos__
#include "asmstuff.h"
#endif

#ifdef __sun__
/* woraround for SunOS 4.x, it has SEEK_* defined here */
#include <unistd.h>
#endif

int is_lame_global_flags_valid(const lame_global_flags* gfp) {
    if (gfp == NULL)
        return 0;
    if (gfp->class_id != LAME_ID)
        return 0;
    return 1;
}

int is_lame_internal_flags_valid(const lame_internal_flags* gfc) {
    if (gfc == NULL)
        return 0;
    if (gfc->class_id != LAME_ID)
        return 0;
    if (gfc->lame_init_params_successful <= 0)
        return 0;
    return 1;
}

static void concatSep(char* dest, char const* sep, char const* str) {
    if (*dest != 0)
        strcat(dest, sep);
    strcat(dest, str);
}

/*
 *  print_config
 *
 *  Prints some selected information about the coding parameters via
 *  the macro command MSGF(), which is currently mapped to lame_errorf
 *  (reports via a error function?), which is a printf-like function
 *  for <stderr>.
 */

void lame_print_config(const lame_global_flags* gfp) {
    lame_internal_flags const* const gfc = gfp->internal_flags;
    SessionConfig_t const* const cfg = &gfc->cfg;
    double const out_samplerate = cfg->samplerate_out;
    double const in_samplerate = cfg->samplerate_in;

    MSGF(gfc, "LAME %s %s (%s)\n", get_lame_version(), get_lame_os_bitness(), get_lame_url());

#if (LAME_ALPHA_VERSION)
    MSGF(gfc, "warning: alpha versions should be used for testing only\n");
#endif
    if (gfc->CPU_features.MMX || gfc->CPU_features.AMD_3DNow || gfc->CPU_features.SSE || gfc->CPU_features.SSE2) {
        char text[256] = {0};
        int fft_asm_used = 0;
#ifdef HAVE_NASM
        if (gfc->CPU_features.AMD_3DNow) {
            fft_asm_used = 1;
        }
        else if (gfc->CPU_features.SSE) {
            fft_asm_used = 2;
        }
#else
#if defined(HAVE_XMMINTRIN_H) && defined(MIN_ARCH_SSE)
        {
            fft_asm_used = 3;
        }
#endif
#endif
        if (gfc->CPU_features.MMX) {
#ifdef MMX_choose_table
            concatSep(text, ", ", "MMX (ASM used)");
#else
            concatSep(text, ", ", "MMX");
#endif
        }
        if (gfc->CPU_features.AMD_3DNow) {
            concatSep(text, ", ", (fft_asm_used == 1) ? "3DNow! (ASM used)" : "3DNow!");
        }
        if (gfc->CPU_features.SSE) {
#if defined(HAVE_XMMINTRIN_H)
            concatSep(text, ", ", "SSE (ASM used)");
#else
            concatSep(text, ", ", (fft_asm_used == 2) ? "SSE (ASM used)" : "SSE");
#endif
        }
        if (gfc->CPU_features.SSE2) {
            concatSep(text, ", ", (fft_asm_used == 3) ? "SSE2 (ASM used)" : "SSE2");
        }
        MSGF(gfc, "CPU features: %s\n", text);
    }

    if (cfg->channels_in == 2 && cfg->channels_out == 1 /* mono */) {
        MSGF(gfc, "Autoconverting from stereo to mono. Setting encoding to mono mode.\n");
    }

    if (isResamplingNecessary(cfg)) {
        MSGF(gfc, "Resampling:  input %g kHz  output %g kHz\n", 1.e-3 * in_samplerate, 1.e-3 * out_samplerate);
    }

    if (cfg->highpass2 > 0.)
        MSGF(gfc,
             "Using polyphase highpass filter, transition band: %5.0f Hz - %5.0f "
             "Hz\n",
             0.5 * cfg->highpass1 * out_samplerate, 0.5 * cfg->highpass2 * out_samplerate);
    if (0. < cfg->lowpass1 || 0. < cfg->lowpass2) {
        MSGF(gfc,
             "Using polyphase lowpass filter, transition band: %5.0f Hz - %5.0f "
             "Hz\n",
             0.5 * cfg->lowpass1 * out_samplerate, 0.5 * cfg->lowpass2 * out_samplerate);
    }
    else {
        MSGF(gfc, "polyphase lowpass filter disabled\n");
    }

    if (cfg->free_format) {
        MSGF(gfc, "Warning: many decoders cannot handle free format bitstreams\n");
        if (cfg->avg_bitrate > 320) {
            MSGF(gfc,
                 "Warning: many decoders cannot handle free format bitrates "
                 ">320 kbps (see documentation)\n");
        }
    }
}

/**     rh:
 *      some pretty printing is very welcome at this point!
 *      so, if someone is willing to do so, please do it!
 *      add more, if you see more...
 */
void lame_print_internals(const lame_global_flags* gfp) {
    lame_internal_flags const* const gfc = gfp->internal_flags;
    SessionConfig_t const* const cfg = &gfc->cfg;
    const char* pc = "";

    /*  compiler/processor optimizations, operational, etc.
     */
    MSGF(gfc, "\nmisc:\n\n");

    MSGF(gfc, "\tscaling: %g\n", gfp->scale);
    MSGF(gfc, "\tch0 (left) scaling: %g\n", gfp->scale_left);
    MSGF(gfc, "\tch1 (right) scaling: %g\n", gfp->scale_right);
    switch (cfg->use_best_huffman) {
        default:
            pc = "normal";
            break;
        case 1:
            pc = "best (outside loop)";
            break;
        case 2:
            pc = "best (inside loop, slow)";
            break;
    }
    MSGF(gfc, "\thuffman search: %s\n", pc);
    MSGF(gfc, "\texperimental Y=%d\n", gfp->experimentalY);
    MSGF(gfc, "\t...\n");

    /*  everything controlling the stream format
     */
    MSGF(gfc, "\nstream format:\n\n");
    switch (cfg->version) {
        case 0:
            pc = "2.5";
            break;
        case 1:
            pc = "1";
            break;
        case 2:
            pc = "2";
            break;
        default:
            pc = "?";
            break;
    }
    MSGF(gfc, "\tMPEG-%s Layer 3\n", pc);
    switch (cfg->mode) {
        case JOINT_STEREO:
            pc = "joint stereo";
            break;
        case STEREO:
            pc = "stereo";
            break;
        case DUAL_CHANNEL:
            pc = "dual channel";
            break;
        case MONO:
            pc = "mono";
            break;
        case NOT_SET:
            pc = "not set (error)";
            break;
        default:
            pc = "unknown (error)";
            break;
    }
    MSGF(gfc, "\t%d channel - %s\n", cfg->channels_out, pc);

    switch (cfg->vbr) {
        case vbr_off:
            pc = "off";
            break;
        default:
            pc = "all";
            break;
    }
    MSGF(gfc, "\tpadding: %s\n", pc);

    if (vbr_default == cfg->vbr)
        pc = "(default)";
    else if (cfg->free_format)
        pc = "(free format)";
    else
        pc = "";
    switch (cfg->vbr) {
        case vbr_off:
            MSGF(gfc, "\tconstant bitrate - CBR %s\n", pc);
            break;
        case vbr_abr:
            MSGF(gfc, "\tvariable bitrate - ABR %s\n", pc);
            break;
        case vbr_rh:
            MSGF(gfc, "\tvariable bitrate - VBR rh %s\n", pc);
            break;
        case vbr_mtrh:
            MSGF(gfc, "\tvariable bitrate - VBR mtrh %s\n", pc);
            break;
        default:
            MSGF(gfc, "\t ?? oops, some new one ?? \n");
            break;
    }
    MSGF(gfc, "\t...\n");

    /*  everything controlling psychoacoustic settings, like ATH, etc.
     */
    MSGF(gfc, "\npsychoacoustic:\n\n");

    switch (cfg->short_blocks) {
        default:
        case short_block_not_set:
            pc = "?";
            break;
        case short_block_allowed:
            pc = "allowed";
            break;
        case short_block_coupled:
            pc = "channel coupled";
            break;
        case short_block_dispensed:
            pc = "dispensed";
            break;
        case short_block_forced:
            pc = "forced";
            break;
    }
    MSGF(gfc, "\tusing short blocks: %s\n", pc);
    MSGF(gfc, "\tsubblock gain: %d\n", cfg->subblock_gain);
    MSGF(gfc, "\tadjust masking: %g dB\n", gfc->sv_qnt.mask_adjust);
    MSGF(gfc, "\tadjust masking short: %g dB\n", gfc->sv_qnt.mask_adjust_short);
    MSGF(gfc, "\tquantization comparison: %d\n", cfg->quant_comp);
    MSGF(gfc, "\t ^ comparison short blocks: %d\n", cfg->quant_comp_short);
    MSGF(gfc, "\tnoise shaping: %d\n", cfg->noise_shaping);
    MSGF(gfc, "\t ^ amplification: %d\n", cfg->noise_shaping_amp);
    MSGF(gfc, "\t ^ stopping: %d\n", cfg->noise_shaping_stop);

    pc = "using";
    if (cfg->ATHshort)
        pc = "the only masking for short blocks";
    if (cfg->ATHonly)
        pc = "the only masking";
    if (cfg->noATH)
        pc = "not used";
    MSGF(gfc, "\tATH: %s\n", pc);
    MSGF(gfc, "\t ^ type: %d\n", cfg->ATHtype);
    MSGF(gfc, "\t ^ shape: %g%s\n", cfg->ATHcurve, " (only for type 4)");
    MSGF(gfc, "\t ^ level adjustement: %g dB\n", cfg->ATH_offset_db);
    MSGF(gfc, "\t ^ adjust type: %d\n", gfc->ATH->use_adjust);
    MSGF(gfc, "\t ^ adjust sensitivity power: %f\n", gfc->ATH->aa_sensitivity_p);

    MSGF(gfc, "\texperimental psy tunings by Naoki Shibata\n");
    MSGF(gfc, "\t   adjust masking bass=%g dB, alto=%g dB, treble=%g dB, sfb21=%g dB\n", 10 * log10(gfc->sv_qnt.longfact[0]), 10 * log10(gfc->sv_qnt.longfact[7]), 10 * log10(gfc->sv_qnt.longfact[14]),
         10 * log10(gfc->sv_qnt.longfact[21]));

    pc = cfg->use_temporal_masking_effect ? "yes" : "no";
    MSGF(gfc, "\tusing temporal masking effect: %s\n", pc);
    MSGF(gfc, "\tinterchannel masking ratio: %g\n", cfg->interChRatio);
    MSGF(gfc, "\t...\n");

    /*  that's all ?
     */
    MSGF(gfc, "\n");
    return;
}

static int lame_init_internal_flags(lame_internal_flags* gfc) {
    if (NULL == gfc)
        return -1;

    gfc->cfg.vbr_min_bitrate_index = 1;  /* not  0 ????? */
    gfc->cfg.vbr_max_bitrate_index = 13; /* not 14 ????? */
    gfc->sv_qnt.OldValue[0] = 180;
    gfc->sv_qnt.OldValue[1] = 180;
    gfc->sv_qnt.CurrentStep[0] = 4;
    gfc->sv_qnt.CurrentStep[1] = 4;
    gfc->sv_qnt.masking_lower = 1;

    /* The reason for
     *       int mf_samples_to_encode = ENCDELAY + POSTDELAY;
     * ENCDELAY = internal encoder delay.  And then we have to add POSTDELAY=288
     * because of the 50% MDCT overlap.  A 576 MDCT granule decodes to
     * 1152 samples.  To synthesize the 576 samples centered under this granule
     * we need the previous granule for the first 288 samples (no problem), and
     * the next granule for the next 288 samples (not possible if this is last
     * granule).  So we need to pad with 288 samples to make sure we can
     * encode the 576 samples we are interested in.
     */
    gfc->sv_enc.mf_samples_to_encode = ENCDELAY + POSTDELAY;
    gfc->sv_enc.mf_size = ENCDELAY - MDCTDELAY; /* we pad input with this many 0's */
    gfc->ov_enc.encoder_padding = 0;
    gfc->ov_enc.encoder_delay = ENCDELAY;

    gfc->ATH = lame_calloc(ATH_t, 1);
    if (NULL == gfc->ATH)
        return -2; /* maybe error codes should be enumerated in lame.h ?? */
    return 0;
}

/* initialize mp3 encoder defaults */
static int lame_init_defaults(lame_global_flags* gfp) {
    disable_FPE(); /* disable floating point exceptions */

    memset(gfp, 0, sizeof(lame_global_flags));

    gfp->class_id = LAME_ID;

    /* Global flags.  set defaults here for non-zero values */
    /* see lame.h for description */
    /* set integer values to -1 to mean that LAME will compute the
     * best value, UNLESS the calling program as set it
     * (and the value is no longer -1)
     */
    gfp->strict_ISO = MDB_MAXIMUM;

    gfp->mode = NOT_SET;
    gfp->original = 1;
    gfp->samplerate_in = 44100;
    gfp->num_channels = 2;
    gfp->num_samples = MAX_U_32_NUM;

    gfp->quality = -1;
    gfp->short_blocks = short_block_not_set;
    gfp->subblock_gain = -1;

    gfp->lowpassfreq = 0;
    gfp->highpassfreq = 0;
    gfp->lowpasswidth = -1;
    gfp->highpasswidth = -1;

    gfp->VBR = vbr_off;
    gfp->VBR_q = 4;
    gfp->VBR_mean_bitrate_kbps = 128;
    gfp->VBR_min_bitrate_kbps = 0;
    gfp->VBR_max_bitrate_kbps = 0;
    gfp->VBR_hard_min = 0;

    gfp->quant_comp = -1;
    gfp->quant_comp_short = -1;
    gfp->safe_short_transient_redistribute = LAME_ENABLE_SAFE_TRANSIENT_REDIST_DEFAULT;

    gfp->msfix = -1;

    gfp->attackthre = -1;
    gfp->attackthre_s = -1;

    gfp->scale = 1;
    gfp->scale_left = 1;
    gfp->scale_right = 1;

    gfp->ATHcurve = -1;
    gfp->ATHtype = -1; /* default = -1 = set in lame_init_params */
    /* 2 = equal loudness curve */
    gfp->athaa_sensitivity = 0.0; /* no offset */
    gfp->athaa_type = -1;
    gfp->useTemporal = -1;
    gfp->interChRatio = -1;

    gfp->asm_optimizations.mmx = 1;
    gfp->asm_optimizations.amd3dnow = 1;
    gfp->asm_optimizations.sse = 1;

    gfp->preset = 0;

    gfp->report.debugf = &lame_report_def;
    gfp->report.errorf = &lame_report_def;
    gfp->report.msgf = &lame_report_def;

    gfp->internal_flags = lame_calloc(lame_internal_flags, 1);

    if (lame_init_internal_flags(gfp->internal_flags) < 0) {
        freegfc(gfp->internal_flags);
        gfp->internal_flags = 0;
        return -1;
    }
    return 0;
}

lame_global_flags* lame_init(void) {
    lame_global_flags* gfp;
    int ret;

    init_log_table();

    gfp = lame_calloc(lame_global_flags, 1);
    if (gfp == NULL)
        return NULL;

    ret = lame_init_defaults(gfp);
    if (ret != 0) {
        free(gfp);
        return NULL;
    }

    gfp->lame_allocated_gfp = 1;
    return gfp;
}

/***********************************************************************
 *
 *  some simple statistics
 *
 *  Robert Hegemann 2000-10-11
 *
 ***********************************************************************/

/*  histogram of used bitrate indexes:
 *  One has to weight them to calculate the average bitrate in kbps
 *
 *  bitrate indices:
 *  there are 14 possible bitrate indices, 0 has the special meaning
 *  "free format" which is not possible to mix with VBR and 15 is forbidden
 *  anyway.
 *
 *  stereo modes:
 *  0: LR   number of left-right encoded frames
 *  1: LR-I number of left-right and intensity encoded frames
 *  2: MS   number of mid-side encoded frames
 *  3: MS-I number of mid-side and intensity encoded frames
 *
 *  4: number of encoded frames
 *
 */

void lame_bitrate_kbps(const lame_global_flags* gfp, int bitrate_kbps[14]) {
    if (is_lame_global_flags_valid(gfp)) {
        lame_internal_flags const* const gfc = gfp->internal_flags;
        if (is_lame_internal_flags_valid(gfc)) {
            SessionConfig_t const* const cfg = &gfc->cfg;
            int i;
            if (cfg->free_format) {
                for (i = 0; i < 14; i++)
                    bitrate_kbps[i] = -1;
                bitrate_kbps[0] = cfg->avg_bitrate;
            }
            else {
                for (i = 0; i < 14; i++)
                    bitrate_kbps[i] = bitrate_table[cfg->version][i + 1];
            }
        }
    }
}

void lame_bitrate_hist(const lame_global_flags* gfp, int bitrate_count[14]) {
    if (is_lame_global_flags_valid(gfp)) {
        lame_internal_flags const* const gfc = gfp->internal_flags;
        if (is_lame_internal_flags_valid(gfc)) {
            SessionConfig_t const* const cfg = &gfc->cfg;
            EncResult_t const* const eov = &gfc->ov_enc;
            int i;

            if (cfg->free_format) {
                for (i = 0; i < 14; i++) {
                    bitrate_count[i] = 0;
                }
                bitrate_count[0] = eov->bitrate_channelmode_hist[0][4];
            }
            else {
                for (i = 0; i < 14; i++) {
                    bitrate_count[i] = eov->bitrate_channelmode_hist[i + 1][4];
                }
            }
        }
    }
}

void lame_stereo_mode_hist(const lame_global_flags* gfp, int stmode_count[4]) {
    if (is_lame_global_flags_valid(gfp)) {
        lame_internal_flags const* const gfc = gfp->internal_flags;
        if (is_lame_internal_flags_valid(gfc)) {
            EncResult_t const* const eov = &gfc->ov_enc;
            int i;

            for (i = 0; i < 4; i++) {
                stmode_count[i] = eov->bitrate_channelmode_hist[15][i];
            }
        }
    }
}

void lame_bitrate_stereo_mode_hist(const lame_global_flags* gfp, int bitrate_stmode_count[14][4]) {
    if (is_lame_global_flags_valid(gfp)) {
        lame_internal_flags const* const gfc = gfp->internal_flags;
        if (is_lame_internal_flags_valid(gfc)) {
            SessionConfig_t const* const cfg = &gfc->cfg;
            EncResult_t const* const eov = &gfc->ov_enc;
            int i;
            int j;

            if (cfg->free_format) {
                for (j = 0; j < 14; j++)
                    for (i = 0; i < 4; i++) {
                        bitrate_stmode_count[j][i] = 0;
                    }
                for (i = 0; i < 4; i++) {
                    bitrate_stmode_count[0][i] = eov->bitrate_channelmode_hist[0][i];
                }
            }
            else {
                for (j = 0; j < 14; j++) {
                    for (i = 0; i < 4; i++) {
                        bitrate_stmode_count[j][i] = eov->bitrate_channelmode_hist[j + 1][i];
                    }
                }
            }
        }
    }
}

void lame_block_type_hist(const lame_global_flags* gfp, int btype_count[6]) {
    if (is_lame_global_flags_valid(gfp)) {
        lame_internal_flags const* const gfc = gfp->internal_flags;
        if (is_lame_internal_flags_valid(gfc)) {
            EncResult_t const* const eov = &gfc->ov_enc;
            int i;

            for (i = 0; i < 6; ++i) {
                btype_count[i] = eov->bitrate_blocktype_hist[15][i];
            }
        }
    }
}

void lame_bitrate_block_type_hist(const lame_global_flags* gfp, int bitrate_btype_count[14][6]) {
    if (is_lame_global_flags_valid(gfp)) {
        lame_internal_flags const* const gfc = gfp->internal_flags;
        if (is_lame_internal_flags_valid(gfc)) {
            SessionConfig_t const* const cfg = &gfc->cfg;
            EncResult_t const* const eov = &gfc->ov_enc;
            int i, j;

            if (cfg->free_format) {
                for (j = 0; j < 14; ++j) {
                    for (i = 0; i < 6; ++i) {
                        bitrate_btype_count[j][i] = 0;
                    }
                }
                for (i = 0; i < 6; ++i) {
                    bitrate_btype_count[0][i] = eov->bitrate_blocktype_hist[0][i];
                }
            }
            else {
                for (j = 0; j < 14; ++j) {
                    for (i = 0; i < 6; ++i) {
                        bitrate_btype_count[j][i] = eov->bitrate_blocktype_hist[j + 1][i];
                    }
                }
            }
        }
    }
}

/* end of lame.c */
