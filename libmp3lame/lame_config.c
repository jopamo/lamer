/* -*- mode: C; mode: fold -*- */
/*
 * LAME MP3 encoding engine
 *
 * Configuration planning for the public encoder lifecycle.
 *
 * Copyright (c) 1999-2000 Mark Taylor
 * Copyright (c) 2000-2005 Takehiro Tominaga
 * Copyright (c) 2000-2019 Robert Hegemann
 * Copyright (c) 2000-2005 Gabriel Bouvigne
 * Copyright (c) 2000-2004 Alexander Leidinger
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

#include "lame.h"
#include "machine.h"
#include "encoder.h"
#include "util.h"
#include "lame_global_flags.h"
#include "gain_analysis.h"
#include "bitstream.h"
#include "quantize_pvt.h"
#include "set_get.h"
#include "psymodel.h"
#include "steadyprotect.h"
#include "tables.h"

#ifndef LAME_ENABLE_SAFE_TRANSIENT_REDIST_DEFAULT
#define LAME_ENABLE_SAFE_TRANSIENT_REDIST_DEFAULT 1
#endif

#define LAME_DEFAULT_QUALITY 3

static  FLOAT
filter_coef(FLOAT x)
{
    if (x > 1.0)
        return 0.0;
    if (x <= 0.0)
        return 1.0;

    return cos(PI / 2 * x);
}

static int
default_v0_vbr_min_bitrate_kbps(SessionConfig_t const *cfg)
{
    /* V0 should stay meaningfully top-end VBR without wasting bits on silence
       or trivially easy material. Scale the default floor with channel count
       and output bandwidth, and still allow explicit -b / API overrides. */
    if (cfg->channels_out == 1) {
        if (cfg->samplerate_out < 16000) {
            return 16;
        }
        if (cfg->samplerate_out < 32000) {
            return 32;
        }
        return 64;
    }

    if (cfg->samplerate_out < 16000) {
        return 32;
    }
    if (cfg->samplerate_out < 32000) {
        return 64;
    }
    return 128;
}

static void
apply_default_vbr_floor(lame_global_flags *gfp, SessionConfig_t const *cfg)
{
    int floor;

    if (gfp->VBR_min_bitrate_kbps != 0) {
        return;
    }
    if (gfp->preset != V0) {
        return;
    }

    floor = default_v0_vbr_min_bitrate_kbps(cfg);
    if (gfp->VBR_max_bitrate_kbps > 0 && floor > gfp->VBR_max_bitrate_kbps) {
        floor = gfp->VBR_max_bitrate_kbps;
    }
    if (floor > 0) {
        gfp->VBR_min_bitrate_kbps = floor;
    }
}

static void
lame_init_params_ppflt(lame_internal_flags * gfc)
{
    SessionConfig_t *const cfg = &gfc->cfg;
    
    /***************************************************************/
    /* compute info needed for polyphase filter (filter type==0, default) */
    /***************************************************************/

    int     band, maxband, minband;
    FLOAT   freq;
    int     lowpass_band = 32;
    int     highpass_band = -1;

    if (cfg->lowpass1 > 0) {
        minband = 999;
        for (band = 0; band <= 31; band++) {
            freq = band / 31.0;
            /* this band and above will be zeroed: */
            if (freq >= cfg->lowpass2) {
                lowpass_band = Min(lowpass_band, band);
            }
            if (cfg->lowpass1 < freq && freq < cfg->lowpass2) {
                minband = Min(minband, band);
            }
        }

        /* compute the *actual* transition band implemented by
         * the polyphase filter */
        if (minband == 999) {
            cfg->lowpass1 = (lowpass_band - .75) / 31.0;
        }
        else {
            cfg->lowpass1 = (minband - .75) / 31.0;
        }
        cfg->lowpass2 = lowpass_band / 31.0;
    }

    /* make sure highpass filter is within 90% of what the effective
     * highpass frequency will be */
    if (cfg->highpass2 > 0) {
        if (cfg->highpass2 < .9 * (.75 / 31.0)) {
            cfg->highpass1 = 0;
            cfg->highpass2 = 0;
            MSGF(gfc, "Warning: highpass filter disabled.  " "highpass frequency too small\n");
        }
    }

    if (cfg->highpass2 > 0) {
        maxband = -1;
        for (band = 0; band <= 31; band++) {
            freq = band / 31.0;
            /* this band and below will be zereod */
            if (freq <= cfg->highpass1) {
                highpass_band = Max(highpass_band, band);
            }
            if (cfg->highpass1 < freq && freq < cfg->highpass2) {
                maxband = Max(maxband, band);
            }
        }
        /* compute the *actual* transition band implemented by
         * the polyphase filter */
        cfg->highpass1 = highpass_band / 31.0;
        if (maxband == -1) {
            cfg->highpass2 = (highpass_band + .75) / 31.0;
        }
        else {
            cfg->highpass2 = (maxband + .75) / 31.0;
        }
    }

    for (band = 0; band < 32; band++) {
        FLOAT fc1, fc2;
        freq = band / 31.0f;
        if (cfg->highpass2 > cfg->highpass1) {
            fc1 = filter_coef((cfg->highpass2 - freq) / (cfg->highpass2 - cfg->highpass1 + 1e-20));
        }
        else {
            fc1 = 1.0f;
        }
        if (cfg->lowpass2 > cfg->lowpass1) {
            fc2 = filter_coef((freq - cfg->lowpass1)  / (cfg->lowpass2 - cfg->lowpass1 + 1e-20));
        }
        else {
            fc2 = 1.0f;
        }
        gfc->sv_enc.amp_filter[band] = fc1 * fc2;
    }
}


static void
optimum_bandwidth(double *const lowerlimit, double *const upperlimit, const unsigned bitrate)
{
/*
 *  Input:
 *      bitrate     total bitrate in kbps
 *
 *   Output:
 *      lowerlimit: best lowpass frequency limit for input filter in Hz
 *      upperlimit: best highpass frequency limit for input filter in Hz
 */
    int     table_index;

    typedef struct {
        int     bitrate;     /* only indicative value */
        int     lowpass;
    } band_pass_t;

    const band_pass_t freq_map[] = {
        {8, 2000},
        {16, 3700},
        {24, 3900},
        {32, 5500},
        {40, 7000},
        {48, 7500},
        {56, 10000},
        {64, 11000},
        {80, 13500},
        {96, 15100},
        {112, 15600},
        {128, 17000},
        {160, 17500},
        {192, 18600},
        {224, 19400},
        {256, 19700},
        {320, 20500}
    };


    table_index = nearestBitrateFullIndex(bitrate);

    (void) freq_map[table_index].bitrate;
    *lowerlimit = freq_map[table_index].lowpass;


/*
 *  Now we try to choose a good high pass filtering frequency.
 *  This value is currently not used.
 *    For fu < 16 kHz:  sqrt(fu*fl) = 560 Hz
 *    For fu = 18 kHz:  no high pass filtering
 *  This gives:
 *
 *   2 kHz => 160 Hz
 *   3 kHz => 107 Hz
 *   4 kHz =>  80 Hz
 *   8 kHz =>  40 Hz
 *  16 kHz =>  20 Hz
 *  17 kHz =>  10 Hz
 *  18 kHz =>   0 Hz
 *
 *  These are ad hoc values and these can be optimized if a high pass is available.
 */
/*    if (f_low <= 16000)
        f_high = 16000. * 20. / f_low;
    else if (f_low <= 18000)
        f_high = 180. - 0.01 * f_low;
    else
        f_high = 0.;*/

    /*
     *  When we sometimes have a good highpass filter, we can add the highpass
     *  frequency to the lowpass frequency
     */

    /*if (upperlimit != NULL)
     *upperlimit = f_high;*/
    (void) upperlimit;
}


static int
optimum_samplefreq(int lowpassfreq, int input_samplefreq)
{
/*
 * Rules:
 *  - if possible, sfb21 should NOT be used
 *
 */
    int     suggested_samplefreq = 44100;

    if (input_samplefreq >= 48000)
        suggested_samplefreq = 48000;
    else if (input_samplefreq >= 44100)
        suggested_samplefreq = 44100;
    else if (input_samplefreq >= 32000)
        suggested_samplefreq = 32000;
    else if (input_samplefreq >= 24000)
        suggested_samplefreq = 24000;
    else if (input_samplefreq >= 22050)
        suggested_samplefreq = 22050;
    else if (input_samplefreq >= 16000)
        suggested_samplefreq = 16000;
    else if (input_samplefreq >= 12000)
        suggested_samplefreq = 12000;
    else if (input_samplefreq >= 11025)
        suggested_samplefreq = 11025;
    else if (input_samplefreq >= 8000)
        suggested_samplefreq = 8000;

    if (lowpassfreq == -1)
        return suggested_samplefreq;

    if (lowpassfreq <= 15960)
        suggested_samplefreq = 44100;
    if (lowpassfreq <= 15250)
        suggested_samplefreq = 32000;
    if (lowpassfreq <= 11220)
        suggested_samplefreq = 24000;
    if (lowpassfreq <= 9970)
        suggested_samplefreq = 22050;
    if (lowpassfreq <= 7230)
        suggested_samplefreq = 16000;
    if (lowpassfreq <= 5420)
        suggested_samplefreq = 12000;
    if (lowpassfreq <= 4510)
        suggested_samplefreq = 11025;
    if (lowpassfreq <= 3970)
        suggested_samplefreq = 8000;

    if (input_samplefreq < suggested_samplefreq) {
        /* choose a valid MPEG sample frequency above the input sample frequency
           to avoid SFB21/12 bitrate bloat
           rh 061115
         */
        if (input_samplefreq > 44100) {
            return 48000;
        }
        if (input_samplefreq > 32000) {
            return 44100;
        }
        if (input_samplefreq > 24000) {
            return 32000;
        }
        if (input_samplefreq > 22050) {
            return 24000;
        }
        if (input_samplefreq > 16000) {
            return 22050;
        }
        if (input_samplefreq > 12000) {
            return 16000;
        }
        if (input_samplefreq > 11025) {
            return 12000;
        }
        if (input_samplefreq > 8000) {
            return 11025;
        }
        return 8000;
    }
    return suggested_samplefreq;
}





/* set internal feature flags.  USER should not access these since
 * some combinations will produce strange results */
static void
lame_init_qval(lame_global_flags * gfp)
{
    lame_internal_flags *const gfc = gfp->internal_flags;
    SessionConfig_t *const cfg = &gfc->cfg;

    switch (gfp->quality) {
    default:
    case 9:            /* no psymodel, no noise shaping */
        cfg->noise_shaping = 0;
        cfg->noise_shaping_amp = 0;
        cfg->noise_shaping_stop = 0;
        cfg->use_best_huffman = 0;
        cfg->full_outer_loop = 0;
        break;

    case 8:
        gfp->quality = 7;
        /*lint --fallthrough */
    case 7:            /* use psymodel (for short block and m/s switching), but no noise shapping */
        cfg->noise_shaping = 0;
        cfg->noise_shaping_amp = 0;
        cfg->noise_shaping_stop = 0;
        cfg->use_best_huffman = 0;
        cfg->full_outer_loop = 0;
        if (cfg->vbr == vbr_mtrh) {
            cfg->full_outer_loop  = -1;
        }
        break;

    case 6:
        if (cfg->noise_shaping == 0)
            cfg->noise_shaping = 1;
        cfg->noise_shaping_amp = 0;
        cfg->noise_shaping_stop = 0;
        if (cfg->subblock_gain == -1)
            cfg->subblock_gain = 1;
        cfg->use_best_huffman = 0;
        cfg->full_outer_loop = 0;
        break;

    case 5:
        if (cfg->noise_shaping == 0)
            cfg->noise_shaping = 1;
        cfg->noise_shaping_amp = 0;
        cfg->noise_shaping_stop = 0;
        if (cfg->subblock_gain == -1)
            cfg->subblock_gain = 1;
        cfg->use_best_huffman = 0;
        cfg->full_outer_loop = 0;
        break;

    case 4:
        if (cfg->noise_shaping == 0)
            cfg->noise_shaping = 1;
        cfg->noise_shaping_amp = 0;
        cfg->noise_shaping_stop = 0;
        if (cfg->subblock_gain == -1)
            cfg->subblock_gain = 1;
        cfg->use_best_huffman = 1;
        cfg->full_outer_loop = 0;
        break;

    case 3:
        if (cfg->noise_shaping == 0)
            cfg->noise_shaping = 1;
        cfg->noise_shaping_amp = 1;
        cfg->noise_shaping_stop = 1;
        if (cfg->subblock_gain == -1)
            cfg->subblock_gain = 1;
        cfg->use_best_huffman = 1;
        cfg->full_outer_loop = 0;
        break;

    case 2:
        if (cfg->noise_shaping == 0)
            cfg->noise_shaping = 1;
        if (gfc->sv_qnt.substep_shaping == 0)
            gfc->sv_qnt.substep_shaping = 2;
        cfg->noise_shaping_amp = 1;
        cfg->noise_shaping_stop = 1;
        if (cfg->subblock_gain == -1)
            cfg->subblock_gain = 1;
        cfg->use_best_huffman = 1; /* inner loop */
        cfg->full_outer_loop = 0;
        break;

    case 1:
        if (cfg->noise_shaping == 0)
            cfg->noise_shaping = 1;
        if (gfc->sv_qnt.substep_shaping == 0)
            gfc->sv_qnt.substep_shaping = 2;
        cfg->noise_shaping_amp = 2;
        cfg->noise_shaping_stop = 1;
        if (cfg->subblock_gain == -1)
            cfg->subblock_gain = 1;
        cfg->use_best_huffman = 1;
        cfg->full_outer_loop = 0;
        break;

    case 0:
        if (cfg->noise_shaping == 0)
            cfg->noise_shaping = 1;
        if (gfc->sv_qnt.substep_shaping == 0)
            gfc->sv_qnt.substep_shaping = 2;
        cfg->noise_shaping_amp = 2;
        cfg->noise_shaping_stop = 1;
        if (cfg->subblock_gain == -1)
            cfg->subblock_gain = 1;
        cfg->use_best_huffman = 1; /*type 2 disabled because of it slowness,
                                      in favor of full outer loop search */
        cfg->full_outer_loop = 1;
        break;
    }

}



static double
linear_int(double a, double b, double m)
{
    return a + m * (b - a);
}



/********************************************************************
 *   initialize internal params based on data in gf
 *   (globalflags struct filled in by calling program)
 *
 *  OUTLINE:
 *
 * We first have some complex code to determine bitrate,
 * output samplerate and mode.  It is complicated by the fact
 * that we allow the user to set some or all of these parameters,
 * and need to determine best possible values for the rest of them:
 *
 *  1. set some CPU related flags
 *  2. check if we are mono->mono, stereo->mono or stereo->stereo
 *  3.  compute bitrate and output samplerate:
 *          user may have set compression ratio
 *          user may have set a bitrate
 *          user may have set a output samplerate
 *  4. set some options which depend on output samplerate
 *  5. compute the actual compression ratio
 *  6. set mode based on compression ratio
 *
 *  The remaining code is much simpler - it just sets options
 *  based on the mode & compression ratio:
 *
 *   set allow_diff_short based on mode
 *   select lowpass filter based on compression ratio & mode
 *   set the bitrate index, and min/max bitrates for VBR modes
 *   disable VBR tag if it is not appropriate
 *   initialize the bitstream
 *   initialize scalefac_band data
 *   set sideinfo_len (based on channels, CRC, out_samplerate)
 *   write an id3v2 tag into the bitstream
 *   write VBR tag into the bitstream
 *   set mpeg1/2 flag
 *   estimate the number of frames (based on a lot of data)
 *
 *   now we set more flags:
 *   nspsytune:
 *      see code
 *   VBR modes
 *      see code
 *   CBR/ABR
 *      see code
 *
 *  Finally, we set the algorithm flags based on the gfp->quality value
 *  lame_init_qval(gfp);
 *
 ********************************************************************/
int
lame_init_params(lame_global_flags * gfp)
{

    int     i;
    int     j;
    lame_internal_flags *gfc;
    SessionConfig_t *cfg;

    if (!is_lame_global_flags_valid(gfp)) 
        return -1;

    gfc = gfp->internal_flags;
    if (gfc == 0) 
        return -1;

    if (is_lame_internal_flags_valid(gfc))
        return -1; /* already initialized */

    /* start updating lame internal flags */
    gfc->class_id = LAME_ID;
    gfc->lame_init_params_successful = 0; /* will be set to one, when we get through until the end */

    if (gfp->samplerate_in < 1)
        return -1; /* input sample rate makes no sense */
    if (gfp->num_channels < 1 || 2 < gfp->num_channels)
        return -1; /* number of input channels makes no sense */
    if (gfp->samplerate_out != 0) {
        int   v=0;
        if (SmpFrqIndex(gfp->samplerate_out, &v) < 0)
            return -1; /* output sample rate makes no sense */
    }

    cfg = &gfc->cfg;

    cfg->enforce_min_bitrate = gfp->VBR_hard_min;
    cfg->analysis = gfp->analysis;
    if (cfg->analysis)
        gfp->write_lame_tag = 0;

    /* some file options not allowed if output is: not specified or stdout */
    if (gfc->pinfo != NULL)
        gfp->write_lame_tag = 0; /* disable Xing VBR tag */

    /* report functions */
    gfc->report_msg = gfp->report.msgf;
    gfc->report_dbg = gfp->report.debugf;
    gfc->report_err = gfp->report.errorf;
    steady_tonal_stats_init_if_requested(gfc);

    if (gfp->asm_optimizations.amd3dnow)
        gfc->CPU_features.AMD_3DNow = has_3DNow();
    else
        gfc->CPU_features.AMD_3DNow = 0;

    if (gfp->asm_optimizations.mmx)
        gfc->CPU_features.MMX = has_MMX();
    else
        gfc->CPU_features.MMX = 0;

    if (gfp->asm_optimizations.sse) {
        gfc->CPU_features.SSE = has_SSE();
        gfc->CPU_features.SSE2 = has_SSE2();
    }
    else {
        gfc->CPU_features.SSE = 0;
        gfc->CPU_features.SSE2 = 0;
    }

    lamer_dsp_init(&gfc->dsp);


    cfg->vbr = gfp->VBR;
    cfg->error_protection = gfp->error_protection;
    cfg->copyright = gfp->copyright;
    cfg->original = gfp->original;
    cfg->extension = gfp->extension;
    cfg->emphasis = gfp->emphasis;

    cfg->channels_in = gfp->num_channels;
    if (cfg->channels_in == 1)
        gfp->mode = MONO;
    cfg->channels_out = (gfp->mode == MONO) ? 1 : 2;
    if (gfp->mode != JOINT_STEREO)
        gfp->force_ms = 0; /* forced mid/side stereo for j-stereo only */
    cfg->force_ms = gfp->force_ms;

    if (cfg->vbr == vbr_off && gfp->VBR_mean_bitrate_kbps != 128 && gfp->brate == 0)
        gfp->brate = gfp->VBR_mean_bitrate_kbps;

    switch (cfg->vbr) {
    case vbr_off:
    case vbr_mtrh:
        /* these modes can handle free format condition */
        break;
    default:
        gfp->free_format = 0; /* mode can't be mixed with free format */
        break;
    }

    cfg->free_format = gfp->free_format;

    if (cfg->vbr == vbr_off && gfp->brate == 0) {
        /* no bitrate or compression ratio specified, use 11.025 */
        if (EQ(gfp->compression_ratio, 0))
            gfp->compression_ratio = 11.025; /* rate to compress a CD down to exactly 128000 bps */
    }

    /* find bitrate if user specify a compression ratio */
    if (cfg->vbr == vbr_off && gfp->compression_ratio > 0) {

        if (gfp->samplerate_out == 0)
            gfp->samplerate_out = map2MP3Frequency((int) (0.97 * gfp->samplerate_in)); /* round up with a margin of 3% */

        /* choose a bitrate for the output samplerate which achieves
         * specified compression ratio
         */
        gfp->brate = gfp->samplerate_out * 16 * cfg->channels_out / (1.e3 * gfp->compression_ratio);

        /* we need the version for the bitrate table look up */
        cfg->samplerate_index = SmpFrqIndex(gfp->samplerate_out, &cfg->version);
        assert(cfg->samplerate_index >=0);

        if (!cfg->free_format) /* for non Free Format find the nearest allowed bitrate */
            gfp->brate = FindNearestBitrate(gfp->brate, cfg->version, gfp->samplerate_out);
    }
    if (gfp->samplerate_out) {
        if (gfp->samplerate_out < 16000) {
            gfp->VBR_mean_bitrate_kbps = Max(gfp->VBR_mean_bitrate_kbps, 8);
            gfp->VBR_mean_bitrate_kbps = Min(gfp->VBR_mean_bitrate_kbps, 64);
        }
        else if (gfp->samplerate_out < 32000) {
            gfp->VBR_mean_bitrate_kbps = Max(gfp->VBR_mean_bitrate_kbps, 8);
            gfp->VBR_mean_bitrate_kbps = Min(gfp->VBR_mean_bitrate_kbps, 160);
        }
        else {
            gfp->VBR_mean_bitrate_kbps = Max(gfp->VBR_mean_bitrate_kbps, 32);
            gfp->VBR_mean_bitrate_kbps = Min(gfp->VBR_mean_bitrate_kbps, 320);
        }
    }
    /* WORK IN PROGRESS */
    /* mapping VBR scale to internal VBR quality settings */
    if (gfp->samplerate_out == 0 && cfg->vbr == vbr_mtrh) {
        float const qval = gfp->VBR_q + gfp->VBR_q_frac;
        struct q_map { int sr_a; float qa, qb, ta, tb; int lp; };
        struct q_map const m[9]
        = { {48000, 0.0,6.5,  0.0,6.5, 23700}
          , {44100, 0.0,6.5,  0.0,6.5, 21780}
          , {32000, 6.5,8.0,  5.2,6.5, 15800}
          , {24000, 8.0,8.5,  5.2,6.0, 11850}
          , {22050, 8.5,9.01, 5.2,6.5, 10892}
          , {16000, 9.01,9.4, 4.9,6.5,  7903}
          , {12000, 9.4,9.6,  4.5,6.0,  5928}
          , {11025, 9.6,9.9,  5.1,6.5,  5446}
          , { 8000, 9.9,10.,  4.9,6.5,  3952}
        };
        for (i = 2; i < 9; ++i) {
            if (gfp->samplerate_in == m[i].sr_a) {
                if (qval < m[i].qa) {
                    double d = qval / m[i].qa;
                    d = d * m[i].ta;
                    gfp->VBR_q = (int)d;
                    gfp->VBR_q_frac = d - gfp->VBR_q;
                }
            }
            if (gfp->samplerate_in >= m[i].sr_a) {
                if (m[i].qa <= qval && qval < m[i].qb) {
                    float const q_ = m[i].qb-m[i].qa;
                    float const t_ = m[i].tb-m[i].ta;
                    double d = m[i].ta + t_ * (qval-m[i].qa) / q_;
                    gfp->VBR_q = (int)d;
                    gfp->VBR_q_frac = d - gfp->VBR_q;
                    gfp->samplerate_out = m[i].sr_a;
                    if (gfp->lowpassfreq == 0) {
                        gfp->lowpassfreq = -1;
                    }
                    break;
                }
            }
        }
    }

    /****************************************************************/
    /* if a filter has not been enabled, see if we should add one: */
    /****************************************************************/
    if (gfp->lowpassfreq == 0) {
        double  lowpass = 16000;
        double  highpass;

        switch (cfg->vbr) {
        case vbr_off:{
                optimum_bandwidth(&lowpass, &highpass, gfp->brate);
                break;
            }
        case vbr_abr:{
                optimum_bandwidth(&lowpass, &highpass, gfp->VBR_mean_bitrate_kbps);
                break;
            }
        case vbr_rh:{
                int const x[11] = {
                    19500, 19000, 18600, 18000, 17500, 16000, 15600, 14900, 12500, 10000, 3950
                };
                if (0 <= gfp->VBR_q && gfp->VBR_q <= 9) {
                    double  a = x[gfp->VBR_q], b = x[gfp->VBR_q + 1], m = gfp->VBR_q_frac;
                    lowpass = linear_int(a, b, m);
                }
                else {
                    lowpass = 19500;
                }
                break;
            }
        case vbr_mtrh:{
                int const x[11] = {
                    24000, 19500, 18500, 18000, 17500, 17000, 16500, 15600, 15200, 7230, 3950
                };
                if (0 <= gfp->VBR_q && gfp->VBR_q <= 9) {
                    double  a = x[gfp->VBR_q], b = x[gfp->VBR_q + 1], m = gfp->VBR_q_frac;
                    lowpass = linear_int(a, b, m);
                }
                else {
                    lowpass = 21500;
                }
                break;
            }
        default:{
                int const x[11] = {
                    19500, 19000, 18500, 18000, 17500, 16500, 15500, 14500, 12500, 9500, 3950
                };
                if (0 <= gfp->VBR_q && gfp->VBR_q <= 9) {
                    double  a = x[gfp->VBR_q], b = x[gfp->VBR_q + 1], m = gfp->VBR_q_frac;
                    lowpass = linear_int(a, b, m);
                }
                else {
                    lowpass = 19500;
                }
            }
        }

        if (gfp->mode == MONO && (cfg->vbr == vbr_off || cfg->vbr == vbr_abr))
            lowpass *= 1.5;

        gfp->lowpassfreq = lowpass;
    }

    if (gfp->samplerate_out == 0) {
        if (2 * gfp->lowpassfreq > gfp->samplerate_in) {
            gfp->lowpassfreq = gfp->samplerate_in / 2;
        }
        gfp->samplerate_out = optimum_samplefreq((int) gfp->lowpassfreq, gfp->samplerate_in);
    }
    if (cfg->vbr == vbr_mtrh) {
        gfp->lowpassfreq = Min(24000, gfp->lowpassfreq);
    }
    else {
        gfp->lowpassfreq = Min(20500, gfp->lowpassfreq);
    }
    gfp->lowpassfreq = Min(gfp->samplerate_out / 2, gfp->lowpassfreq);

    if (cfg->vbr == vbr_off) {
        gfp->compression_ratio = gfp->samplerate_out * 16 * cfg->channels_out / (1.e3 * gfp->brate);
    }
    if (cfg->vbr == vbr_abr) {
        gfp->compression_ratio =
            gfp->samplerate_out * 16 * cfg->channels_out / (1.e3 * gfp->VBR_mean_bitrate_kbps);
    }

    cfg->disable_reservoir = gfp->disable_reservoir;
    cfg->lowpassfreq = gfp->lowpassfreq;
    cfg->highpassfreq = gfp->highpassfreq;
    cfg->samplerate_in = gfp->samplerate_in;
    cfg->samplerate_out = gfp->samplerate_out;
    cfg->mode_gr = cfg->samplerate_out <= 24000 ? 1 : 2; /* Number of granules per frame */


    /*
     *  sample freq       bitrate     compression ratio
     *     [kHz]      [kbps/channel]   for 16 bit input
     *     44.1            56               12.6
     *     44.1            64               11.025
     *     44.1            80                8.82
     *     22.05           24               14.7
     *     22.05           32               11.025
     *     22.05           40                8.82
     *     16              16               16.0
     *     16              24               10.667
     *
     */
    /*
     *  For VBR, take a guess at the compression_ratio.
     *  For example:
     *
     *    VBR_q    compression     like
     *     -        4.4         320 kbps/44 kHz
     *   0...1      5.5         256 kbps/44 kHz
     *     2        7.3         192 kbps/44 kHz
     *     4        8.8         160 kbps/44 kHz
     *     6       11           128 kbps/44 kHz
     *     9       14.7          96 kbps
     *
     *  for lower bitrates, downsample with --resample
     */

    switch (cfg->vbr) {
    case vbr_rh:
    case vbr_mtrh:
        {
            /*numbers are a bit strange, but they determine the lowpass value */
            FLOAT const cmp[] = { 5.7, 6.5, 7.3, 8.2, 10, 11.9, 13, 14, 15, 16.5 };
            gfp->compression_ratio = cmp[gfp->VBR_q];
        }
        break;
    case vbr_abr:
        gfp->compression_ratio =
            cfg->samplerate_out * 16 * cfg->channels_out / (1.e3 * gfp->VBR_mean_bitrate_kbps);
        break;
    default:
        gfp->compression_ratio = cfg->samplerate_out * 16 * cfg->channels_out / (1.e3 * gfp->brate);
        break;
    }


    /* mode = -1 (not set by user) or
     * mode = MONO (because of only 1 input channel).
     * If mode has not been set, then select J-STEREO
     */
    if (gfp->mode == NOT_SET) {
        gfp->mode = JOINT_STEREO;
    }

    cfg->mode = gfp->mode;


    /* apply user driven high pass filter */
    if (cfg->highpassfreq > 0) {
        cfg->highpass1 = 2. * cfg->highpassfreq;

        if (gfp->highpasswidth >= 0)
            cfg->highpass2 = 2. * (cfg->highpassfreq + gfp->highpasswidth);
        else            /* 0% above on default */
            cfg->highpass2 = (1 + 0.00) * 2. * cfg->highpassfreq;

        cfg->highpass1 /= cfg->samplerate_out;
        cfg->highpass2 /= cfg->samplerate_out;
    }
    else {
        cfg->highpass1 = 0;
        cfg->highpass2 = 0;
    }
    /* apply user driven low pass filter */
    cfg->lowpass1 = 0;
    cfg->lowpass2 = 0;
    if (cfg->lowpassfreq > 0 && cfg->lowpassfreq < (cfg->samplerate_out / 2) ) {
        cfg->lowpass2 = 2. * cfg->lowpassfreq;
        if (gfp->lowpasswidth >= 0) {
            cfg->lowpass1 = 2. * (cfg->lowpassfreq - gfp->lowpasswidth);
            if (cfg->lowpass1 < 0) /* has to be >= 0 */
                cfg->lowpass1 = 0;
        }
        else {          /* 0% below on default */
            cfg->lowpass1 = (1 - 0.00) * 2. * cfg->lowpassfreq;
        }
        cfg->lowpass1 /= cfg->samplerate_out;
        cfg->lowpass2 /= cfg->samplerate_out;
    }




  /**********************************************************************/
    /* compute info needed for polyphase filter (filter type==0, default) */
  /**********************************************************************/
    lame_init_params_ppflt(gfc);


  /*******************************************************
   * samplerate and bitrate index
   *******************************************************/
    cfg->samplerate_index = SmpFrqIndex(cfg->samplerate_out, &cfg->version);
    assert(cfg->samplerate_index >= 0);

    if (cfg->vbr == vbr_off) {
        if (cfg->free_format) {
            gfc->ov_enc.bitrate_index = 0;
        }
        else {
            gfp->brate = FindNearestBitrate(gfp->brate, cfg->version, cfg->samplerate_out);
            gfc->ov_enc.bitrate_index = BitrateIndex(gfp->brate, cfg->version, cfg->samplerate_out);
            if (gfc->ov_enc.bitrate_index <= 0) {
                /* This never happens, because of preceding FindNearestBitrate!
                 * But, set a sane value, just in case
                 */
                assert(0);
                gfc->ov_enc.bitrate_index = 8;
            }
        }
    }
    else {
        gfc->ov_enc.bitrate_index = 1;
    }

    init_bit_stream_w(gfc);

    j = cfg->samplerate_index + (3 * cfg->version) + 6 * (cfg->samplerate_out < 16000);
    for (i = 0; i < SBMAX_l + 1; i++)
        gfc->scalefac_band.l[i] = sfBandIndex[j].l[i];

    for (i = 0; i < PSFB21 + 1; i++) {
        int const size = (gfc->scalefac_band.l[22] - gfc->scalefac_band.l[21]) / PSFB21;
        int const start = gfc->scalefac_band.l[21] + i * size;
        gfc->scalefac_band.psfb21[i] = start;
    }
    gfc->scalefac_band.psfb21[PSFB21] = 576;

    for (i = 0; i < SBMAX_s + 1; i++)
        gfc->scalefac_band.s[i] = sfBandIndex[j].s[i];

    for (i = 0; i < PSFB12 + 1; i++) {
        int const size = (gfc->scalefac_band.s[13] - gfc->scalefac_band.s[12]) / PSFB12;
        int const start = gfc->scalefac_band.s[12] + i * size;
        gfc->scalefac_band.psfb12[i] = start;
    }
    gfc->scalefac_band.psfb12[PSFB12] = 192;

    /* determine the mean bitrate for main data */
    if (cfg->mode_gr == 2) /* MPEG 1 */
        cfg->sideinfo_len = (cfg->channels_out == 1) ? 4 + 17 : 4 + 32;
    else                /* MPEG 2 */
        cfg->sideinfo_len = (cfg->channels_out == 1) ? 4 + 9 : 4 + 17;

    if (cfg->error_protection)
        cfg->sideinfo_len += 2;

    {
        int     k;

        for (k = 0; k < 19; k++)
            gfc->sv_enc.pefirbuf[k] = 700 * cfg->mode_gr * cfg->channels_out;

        if (gfp->ATHtype == -1)
            gfp->ATHtype = 4;
    }

    assert(gfp->VBR_q <= 9);
    assert(gfp->VBR_q >= 0);

    switch (cfg->vbr) {

    case vbr_mtrh:{
            if (gfp->strict_ISO < 0) {
                gfp->strict_ISO = MDB_MAXIMUM;
            }
            if (gfp->useTemporal < 0) {
                gfp->useTemporal = 0; /* off by default for this VBR mode */
            }

            (void) apply_preset(gfp, 500 - (gfp->VBR_q * 10), 0);
            /*  The newer VBR code supports only a limited
               subset of quality levels:
               9-5=5 are the same, uses x^3/4 quantization
               4-0=0 are the same  5 plus best huffman divide code
             */
            if (gfp->quality < 0)
                gfp->quality = LAME_DEFAULT_QUALITY;
            if (gfp->quality < 5)
                gfp->quality = 0;
            if (gfp->quality > 7)
                gfp->quality = 7;

            /*  sfb21 extra only with MPEG-1 at higher sampling rates
             */
            if (gfp->experimentalY)
                gfc->sv_qnt.sfb21_extra = 0;
            else
                gfc->sv_qnt.sfb21_extra = (cfg->samplerate_out > 44000);

            break;

        }
    case vbr_rh:{

            (void) apply_preset(gfp, 500 - (gfp->VBR_q * 10), 0);

            /*  sfb21 extra only with MPEG-1 at higher sampling rates
             */
            if (gfp->experimentalY)
                gfc->sv_qnt.sfb21_extra = 0;
            else
                gfc->sv_qnt.sfb21_extra = (cfg->samplerate_out > 44000);

            /*  VBR needs at least the output of GPSYCHO,
             *  so we have to garantee that by setting a minimum
             *  quality level, actually level 6 does it.
             *  down to level 6
             */
            if (gfp->quality > 6)
                gfp->quality = 6;


            if (gfp->quality < 0)
                gfp->quality = LAME_DEFAULT_QUALITY;

            break;
        }

    default:           /* cbr/abr */  {

            /*  no sfb21 extra with CBR code
             */
            gfc->sv_qnt.sfb21_extra = 0;

            if (gfp->quality < 0)
                gfp->quality = LAME_DEFAULT_QUALITY;


            if (cfg->vbr == vbr_off)
                (void) lame_set_VBR_mean_bitrate_kbps(gfp, gfp->brate);
            /* second, set parameters depending on bitrate */
            (void) apply_preset(gfp, gfp->VBR_mean_bitrate_kbps, 0);
            gfp->VBR = cfg->vbr;

            break;
        }
    }

    /*initialize default values common for all modes */

    gfc->sv_qnt.mask_adjust = gfp->maskingadjust;
    gfc->sv_qnt.mask_adjust_short = gfp->maskingadjust_short;

    /*  just another daily changing developer switch  */
    if (gfp->tune) {
        gfc->sv_qnt.mask_adjust += gfp->tune_value_a;
        gfc->sv_qnt.mask_adjust_short += gfp->tune_value_a;
    }


    if (cfg->vbr != vbr_off) { /* choose a min/max bitrate for VBR */
        apply_default_vbr_floor(gfp, cfg);
        /* if the user didn't specify VBR_max_bitrate: */
        cfg->vbr_min_bitrate_index = 1; /* default: allow   8 kbps (MPEG-2) or  32 kbps (MPEG-1) */
        cfg->vbr_max_bitrate_index = 14; /* default: allow 160 kbps (MPEG-2) or 320 kbps (MPEG-1) */
        if (cfg->samplerate_out < 16000)
            cfg->vbr_max_bitrate_index = 8; /* default: allow 64 kbps (MPEG-2.5) */
        if (gfp->VBR_min_bitrate_kbps) {
            gfp->VBR_min_bitrate_kbps =
                FindNearestBitrate(gfp->VBR_min_bitrate_kbps, cfg->version, cfg->samplerate_out);
            cfg->vbr_min_bitrate_index =
                BitrateIndex(gfp->VBR_min_bitrate_kbps, cfg->version, cfg->samplerate_out);
            if (cfg->vbr_min_bitrate_index < 0) {
                /* This never happens, because of preceding FindNearestBitrate!
                 * But, set a sane value, just in case
                 */
                assert(0);
                cfg->vbr_min_bitrate_index = 1;
            }
        }
        if (gfp->VBR_max_bitrate_kbps) {
            gfp->VBR_max_bitrate_kbps =
                FindNearestBitrate(gfp->VBR_max_bitrate_kbps, cfg->version, cfg->samplerate_out);
            cfg->vbr_max_bitrate_index =
                BitrateIndex(gfp->VBR_max_bitrate_kbps, cfg->version, cfg->samplerate_out);
            if (cfg->vbr_max_bitrate_index < 0) {
                /* This never happens, because of preceding FindNearestBitrate!
                 * But, set a sane value, just in case
                 */
                assert(0);
                cfg->vbr_max_bitrate_index = cfg->samplerate_out < 16000 ? 8 : 14;
            }
        }
        gfp->VBR_min_bitrate_kbps = bitrate_table[cfg->version][cfg->vbr_min_bitrate_index];
        gfp->VBR_max_bitrate_kbps = bitrate_table[cfg->version][cfg->vbr_max_bitrate_index];
        gfp->VBR_mean_bitrate_kbps =
            Min(bitrate_table[cfg->version][cfg->vbr_max_bitrate_index],
                gfp->VBR_mean_bitrate_kbps);
        gfp->VBR_mean_bitrate_kbps =
            Max(bitrate_table[cfg->version][cfg->vbr_min_bitrate_index],
                gfp->VBR_mean_bitrate_kbps);
    }

    cfg->preset = gfp->preset;
    cfg->write_lame_tag = gfp->write_lame_tag;
    gfc->sv_qnt.substep_shaping = gfp->substep_shaping;
    cfg->noise_shaping = gfp->noise_shaping;
    cfg->subblock_gain = gfp->subblock_gain;
    cfg->use_best_huffman = gfp->use_best_huffman;
    cfg->avg_bitrate = gfp->brate;
    cfg->vbr_avg_bitrate_kbps = gfp->VBR_mean_bitrate_kbps;
    cfg->compression_ratio = gfp->compression_ratio;

    /* initialize internal qval settings */
    lame_init_qval(gfp);


    /*  automatic ATH adjustment on
     */
    if (gfp->athaa_type < 0)
        gfc->ATH->use_adjust = 3;
    else
        gfc->ATH->use_adjust = gfp->athaa_type;


    /* initialize internal adaptive ATH settings  -jd */
    gfc->ATH->aa_sensitivity_p = pow(10.0, gfp->athaa_sensitivity / -10.0);


    if (gfp->short_blocks == short_block_not_set) {
        gfp->short_blocks = short_block_allowed;
    }

    /*Note Jan/2003: Many hardware decoders cannot handle short blocks in regular
       stereo mode unless they are coupled (same type in both channels)
       it is a rare event (1 frame per min. or so) that LAME would use
       uncoupled short blocks, so lets turn them off until we decide
       how to handle this.  No other encoders allow uncoupled short blocks,
       even though it is in the standard.  */
    /* rh 20040217: coupling makes no sense for mono and dual-mono streams
     */
    if (gfp->short_blocks == short_block_allowed
        && (cfg->mode == JOINT_STEREO || cfg->mode == STEREO)) {
        gfp->short_blocks = short_block_coupled;
    }

    cfg->short_blocks = gfp->short_blocks;


    if (lame_get_quant_comp(gfp) < 0)
        (void) lame_set_quant_comp(gfp, 1);
    if (lame_get_quant_comp_short(gfp) < 0)
        (void) lame_set_quant_comp_short(gfp, 0);

    if (lame_get_msfix(gfp) < 0)
        lame_set_msfix(gfp, 0);

    /* select psychoacoustic model */
    (void) lame_set_exp_nspsytune(gfp, lame_get_exp_nspsytune(gfp) | 1);

    if (gfp->ATHtype < 0)
        gfp->ATHtype = 4;

    if (gfp->ATHcurve < 0)
        gfp->ATHcurve = 4;

    if (gfp->interChRatio < 0)
        gfp->interChRatio = 0;

    if (gfp->useTemporal < 0)
        gfp->useTemporal = 1; /* on by default */


    cfg->interChRatio = gfp->interChRatio;
    cfg->msfix = gfp->msfix;
    cfg->ATH_offset_db = 0-gfp->ATH_lower_db;
    cfg->ATH_offset_factor = powf(10.f, cfg->ATH_offset_db * 0.1f);
    cfg->ATHcurve = gfp->ATHcurve;
    cfg->ATHtype = gfp->ATHtype;
    cfg->ATHonly = gfp->ATHonly;
    cfg->ATHshort = gfp->ATHshort;
    cfg->noATH = gfp->noATH;

    cfg->quant_comp = gfp->quant_comp;
    cfg->quant_comp_short = gfp->quant_comp_short;

    cfg->use_temporal_masking_effect = gfp->useTemporal;
    if (cfg->mode == JOINT_STEREO) {
        cfg->use_safe_joint_stereo = gfp->exp_nspsytune & 2;
    }
    else {
        cfg->use_safe_joint_stereo = 0;
    }
    {
        cfg->adjust_bass_db = (gfp->exp_nspsytune >> 2) & 63;
        if (cfg->adjust_bass_db >= 32.f)
            cfg->adjust_bass_db -= 64.f;
        cfg->adjust_bass_db *= 0.25f;

        cfg->adjust_alto_db = (gfp->exp_nspsytune >> 8) & 63;
        if (cfg->adjust_alto_db >= 32.f)
            cfg->adjust_alto_db -= 64.f;
        cfg->adjust_alto_db *= 0.25f;

        cfg->adjust_treble_db = (gfp->exp_nspsytune >> 14) & 63;
        if (cfg->adjust_treble_db >= 32.f)
            cfg->adjust_treble_db -= 64.f;
        cfg->adjust_treble_db *= 0.25f;

        /*  to be compatible with Naoki's original code, the next 6 bits
         *  define only the amount of changing treble for sfb21 */
        cfg->adjust_sfb21_db = (gfp->exp_nspsytune >> 20) & 63;
        if (cfg->adjust_sfb21_db >= 32.f)
            cfg->adjust_sfb21_db -= 64.f;
        cfg->adjust_sfb21_db *= 0.25f;
        cfg->adjust_sfb21_db += cfg->adjust_treble_db;
    }

    /* Setting up the PCM input data transform matrix, to apply 
     * user defined re-scaling, and or two-to-one channel downmix.
     */
    {
        FLOAT   m[2][2] = { {1.0f, 0.0f}, {0.0f, 1.0f} };

        /* user selected scaling of the samples */
        m[0][0] *= gfp->scale;
        m[0][1] *= gfp->scale;
        m[1][0] *= gfp->scale;
        m[1][1] *= gfp->scale;
        /* user selected scaling of the channel 0 (left) samples */
        m[0][0] *= gfp->scale_left;
        m[0][1] *= gfp->scale_left;
        /* user selected scaling of the channel 1 (right) samples */
        m[1][0] *= gfp->scale_right;
        m[1][1] *= gfp->scale_right;
        /* Downsample to Mono if 2 channels in and 1 channel out */
        if (cfg->channels_in == 2 && cfg->channels_out == 1) {
            m[0][0] = 0.5f * (m[0][0] + m[1][0]);
            m[0][1] = 0.5f * (m[0][1] + m[1][1]);
            m[1][0] = 0;
            m[1][1] = 0;
        }
        cfg->pcm_transform[0][0] = m[0][0];
        cfg->pcm_transform[0][1] = m[0][1];
        cfg->pcm_transform[1][0] = m[1][0];
        cfg->pcm_transform[1][1] = m[1][1];
    }

    /* padding method as described in
     * "MPEG-Layer3 / Bitstream Syntax and Decoding"
     * by Martin Sieler, Ralph Sperschneider
     *
     * note: there is no padding for the very first frame
     *
     * Robert Hegemann 2000-06-22
     */
    gfc->sv_enc.slot_lag = gfc->sv_enc.frac_SpF = 0;
    if (cfg->vbr == vbr_off)
        gfc->sv_enc.slot_lag = gfc->sv_enc.frac_SpF
            = ((cfg->version + 1) * 72000L * cfg->avg_bitrate) % cfg->samplerate_out;

    (void) lame_init_bitstream(gfp);

    iteration_init(gfc);
    (void) psymodel_init(gfp);

    cfg->buffer_constraint = get_max_frame_buffer_size_by_constraint(cfg, gfp->strict_ISO);


    cfg->findReplayGain = gfp->findReplayGain;
    if (cfg->findReplayGain) {
        if (InitGainAnalysis(gfc->sv_rpg.rgdata, cfg->samplerate_out) == INIT_GAIN_ANALYSIS_ERROR) {
            /* Actually this never happens, our samplerates are the ones RG accepts!
             * But just in case, turn RG off
             */
            assert(0);
            cfg->findReplayGain = 0;
        }
    }

    /* updating lame internal flags finished successful */
    gfc->lame_init_params_successful = 1;
    return 0;
}
