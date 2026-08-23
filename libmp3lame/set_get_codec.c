/* -*- mode: C; mode: fold -*- */
/*
 * set/get functions for codec format and bitrate options
 *
 * Copyright (c) 2001-2005 Alexander Leidinger
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
#include "bitstream.h" /* because of compute_flushbits */

#include "set_get.h"
#include "lame_global_flags.h"

/*
 * input stream description
 */

/********************************************************************
 * quantization/noise shaping
 ***********************************************************************/

/* Disable the bit reservoir. For testing only. */
int lame_set_disable_reservoir(lame_global_flags* gfp, int disable_reservoir) {
    if (is_lame_global_flags_valid(gfp)) {
        /* default = 0 (disabled) */

        /* enforce disable/enable meaning, if we need more than two values
           we need to switch to an enum to have an apropriate representation
           of the possible meanings of the value */
        if (0 > disable_reservoir || 1 < disable_reservoir)
            return -1;
        gfp->disable_reservoir = disable_reservoir;
        return 0;
    }
    return -1;
}

int lame_get_disable_reservoir(const lame_global_flags* gfp) {
    if (is_lame_global_flags_valid(gfp)) {
        assert(0 <= gfp->disable_reservoir && 1 >= gfp->disable_reservoir);
        return gfp->disable_reservoir;
    }
    return 0;
}

int lame_set_experimentalX(lame_global_flags* gfp, int experimentalX) {
    if (is_lame_global_flags_valid(gfp)) {
        lame_set_quant_comp(gfp, experimentalX);
        lame_set_quant_comp_short(gfp, experimentalX);
        return 0;
    }
    return -1;
}

int lame_get_experimentalX(const lame_global_flags* gfp) {
    return lame_get_quant_comp(gfp);
}

/* Select a different "best quantization" function. default = 0 */
int lame_set_quant_comp(lame_global_flags* gfp, int quant_type) {
    if (is_lame_global_flags_valid(gfp)) {
        gfp->quant_comp = quant_type;
        return 0;
    }
    return -1;
}

int lame_get_quant_comp(const lame_global_flags* gfp) {
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->quant_comp;
    }
    return 0;
}

/* Select a different "best quantization" function. default = 0 */
int lame_set_quant_comp_short(lame_global_flags* gfp, int quant_type) {
    if (is_lame_global_flags_valid(gfp)) {
        gfp->quant_comp_short = quant_type;
        return 0;
    }
    return -1;
}

int lame_get_quant_comp_short(const lame_global_flags* gfp) {
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->quant_comp_short;
    }
    return 0;
}

/* Another experimental option. For testing only. */
int lame_set_experimentalY(lame_global_flags* gfp, int experimentalY) {
    if (is_lame_global_flags_valid(gfp)) {
        gfp->experimentalY = experimentalY;
        return 0;
    }
    return -1;
}

int lame_get_experimentalY(const lame_global_flags* gfp) {
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->experimentalY;
    }
    return 0;
}

int lame_set_experimentalZ(lame_global_flags* gfp, int experimentalZ) {
    if (is_lame_global_flags_valid(gfp)) {
        gfp->experimentalZ = experimentalZ;
        return 0;
    }
    return -1;
}

int lame_get_experimentalZ(const lame_global_flags* gfp) {
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->experimentalZ;
    }
    return 0;
}

int lame_set_experimental_short_transient_redistribute(lame_global_flags* gfp, int experimental_short_transient_redistribute) {
    if (is_lame_global_flags_valid(gfp)) {
        gfp->experimental_short_transient_redistribute = experimental_short_transient_redistribute;
        return 0;
    }
    return -1;
}

int lame_get_experimental_short_transient_redistribute(const lame_global_flags* gfp) {
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->experimental_short_transient_redistribute;
    }
    return 0;
}

int lame_set_safe_short_transient_redistribute(lame_global_flags* gfp, int safe_short_transient_redistribute) {
    if (is_lame_global_flags_valid(gfp)) {
        gfp->safe_short_transient_redistribute = safe_short_transient_redistribute;
        return 0;
    }
    return -1;
}

int lame_get_safe_short_transient_redistribute(const lame_global_flags* gfp) {
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->safe_short_transient_redistribute;
    }
    return 0;
}

/* Naoki's psycho acoustic model. */
int lame_set_exp_nspsytune(lame_global_flags* gfp, int exp_nspsytune) {
    if (is_lame_global_flags_valid(gfp)) {
        /* default = 0 (disabled) */
        gfp->exp_nspsytune = exp_nspsytune;
        return 0;
    }
    return -1;
}

int lame_get_exp_nspsytune(const lame_global_flags* gfp) {
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->exp_nspsytune;
    }
    return 0;
}

/********************************************************************
 * VBR control
 ***********************************************************************/

/* Types of VBR.  default = vbr_off = CBR */
int lame_set_VBR(lame_global_flags* gfp, vbr_mode VBR) {
    if (is_lame_global_flags_valid(gfp)) {
        int vbr_q = VBR;
        if (0 > vbr_q || vbr_max_indicator <= vbr_q)
            return -1; /* Unknown VBR mode! */
        gfp->VBR = VBR;
        return 0;
    }
    return -1;
}

vbr_mode lame_get_VBR(const lame_global_flags* gfp) {
    if (is_lame_global_flags_valid(gfp)) {
        assert(gfp->VBR < vbr_max_indicator);
        return gfp->VBR;
    }
    return vbr_off;
}

/*
 * VBR quality level.
 *  0 = highest
 *  9 = lowest
 */
int lame_set_VBR_q(lame_global_flags* gfp, int VBR_q) {
    if (is_lame_global_flags_valid(gfp)) {
        int ret = 0;

        if (0 > VBR_q) {
            ret = -1; /* Unknown VBR quality level! */
            VBR_q = 0;
        }
        if (9 < VBR_q) {
            ret = -1;
            VBR_q = 9;
        }
        gfp->VBR_q = VBR_q;
        gfp->VBR_q_frac = 0;
        return ret;
    }
    return -1;
}

int lame_get_VBR_q(const lame_global_flags* gfp) {
    if (is_lame_global_flags_valid(gfp)) {
        assert(0 <= gfp->VBR_q && 10 > gfp->VBR_q);
        return gfp->VBR_q;
    }
    return 0;
}

int lame_set_VBR_quality(lame_global_flags* gfp, float VBR_q) {
    if (is_lame_global_flags_valid(gfp)) {
        int ret = 0;

        if (0 > VBR_q) {
            ret = -1; /* Unknown VBR quality level! */
            VBR_q = 0;
        }
        if (9.999 < VBR_q) {
            ret = -1;
            VBR_q = 9.999;
        }

        gfp->VBR_q = (int)VBR_q;
        gfp->VBR_q_frac = VBR_q - gfp->VBR_q;

        return ret;
    }
    return -1;
}

float lame_get_VBR_quality(const lame_global_flags* gfp) {
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->VBR_q + gfp->VBR_q_frac;
    }
    return 0;
}

/* Ignored except for VBR = vbr_abr (ABR mode) */
int lame_set_VBR_mean_bitrate_kbps(lame_global_flags* gfp, int VBR_mean_bitrate_kbps) {
    if (is_lame_global_flags_valid(gfp)) {
        gfp->VBR_mean_bitrate_kbps = VBR_mean_bitrate_kbps;
        return 0;
    }
    return -1;
}

int lame_get_VBR_mean_bitrate_kbps(const lame_global_flags* gfp) {
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->VBR_mean_bitrate_kbps;
    }
    return 0;
}

int lame_set_VBR_min_bitrate_kbps(lame_global_flags* gfp, int VBR_min_bitrate_kbps) {
    if (is_lame_global_flags_valid(gfp)) {
        gfp->VBR_min_bitrate_kbps = VBR_min_bitrate_kbps;
        return 0;
    }
    return -1;
}

int lame_get_VBR_min_bitrate_kbps(const lame_global_flags* gfp) {
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->VBR_min_bitrate_kbps;
    }
    return 0;
}

int lame_set_VBR_max_bitrate_kbps(lame_global_flags* gfp, int VBR_max_bitrate_kbps) {
    if (is_lame_global_flags_valid(gfp)) {
        gfp->VBR_max_bitrate_kbps = VBR_max_bitrate_kbps;
        return 0;
    }
    return -1;
}

int lame_get_VBR_max_bitrate_kbps(const lame_global_flags* gfp) {
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->VBR_max_bitrate_kbps;
    }
    return 0;
}

/*
 * Strictly enforce VBR_min_bitrate.
 * Normally it will be violated for analog silence.
 */
int lame_set_VBR_hard_min(lame_global_flags* gfp, int VBR_hard_min) {
    if (is_lame_global_flags_valid(gfp)) {
        /* default = 0 (disabled) */

        /* enforce disable/enable meaning, if we need more than two values
           we need to switch to an enum to have an apropriate representation
           of the possible meanings of the value */
        if (0 > VBR_hard_min || 1 < VBR_hard_min)
            return -1;

        gfp->VBR_hard_min = VBR_hard_min;

        return 0;
    }
    return -1;
}

int lame_get_VBR_hard_min(const lame_global_flags* gfp) {
    if (is_lame_global_flags_valid(gfp)) {
        assert(0 <= gfp->VBR_hard_min && 1 >= gfp->VBR_hard_min);
        return gfp->VBR_hard_min;
    }
    return 0;
}

/********************************************************************
 * Filtering control
 ***********************************************************************/

/*
 * Freqency in Hz to apply lowpass.
 *   0 = default = lame chooses
 *  -1 = disabled
 */
int lame_set_lowpassfreq(lame_global_flags* gfp, int lowpassfreq) {
    if (is_lame_global_flags_valid(gfp)) {
        gfp->lowpassfreq = lowpassfreq;
        return 0;
    }
    return -1;
}

int lame_get_lowpassfreq(const lame_global_flags* gfp) {
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->lowpassfreq;
    }
    return 0;
}

/*
 * Width of transition band (in Hz).
 *  default = one polyphase filter band
 */
int lame_set_lowpasswidth(lame_global_flags* gfp, int lowpasswidth) {
    if (is_lame_global_flags_valid(gfp)) {
        gfp->lowpasswidth = lowpasswidth;
        return 0;
    }
    return -1;
}

int lame_get_lowpasswidth(const lame_global_flags* gfp) {
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->lowpasswidth;
    }
    return 0;
}

/*
 * Frequency in Hz to apply highpass.
 *   0 = default = lame chooses
 *  -1 = disabled
 */
int lame_set_highpassfreq(lame_global_flags* gfp, int highpassfreq) {
    if (is_lame_global_flags_valid(gfp)) {
        gfp->highpassfreq = highpassfreq;
        return 0;
    }
    return -1;
}

int lame_get_highpassfreq(const lame_global_flags* gfp) {
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->highpassfreq;
    }
    return 0;
}

/*
 * Width of transition band (in Hz).
 *  default = one polyphase filter band
 */
int lame_set_highpasswidth(lame_global_flags* gfp, int highpasswidth) {
    if (is_lame_global_flags_valid(gfp)) {
        gfp->highpasswidth = highpasswidth;
        return 0;
    }
    return -1;
}

int lame_get_highpasswidth(const lame_global_flags* gfp) {
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->highpasswidth;
    }
    return 0;
}
