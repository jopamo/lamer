/* -*- mode: C; mode: fold -*- */
/*
 * set/get functions for filtering and psychoacoustic options
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

/*
 * psycho acoustics and other arguments which you should not change
 * unless you know what you are doing
 */

/* Adjust masking values. */
int lame_set_maskingadjust(lame_global_flags* gfp, float adjust) {
    if (is_lame_global_flags_valid(gfp)) {
        gfp->maskingadjust = adjust;
        return 0;
    }
    return -1;
}

float lame_get_maskingadjust(const lame_global_flags* gfp) {
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->maskingadjust;
    }
    return 0;
}

int lame_set_maskingadjust_short(lame_global_flags* gfp, float adjust) {
    if (is_lame_global_flags_valid(gfp)) {
        gfp->maskingadjust_short = adjust;
        return 0;
    }
    return -1;
}

float lame_get_maskingadjust_short(const lame_global_flags* gfp) {
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->maskingadjust_short;
    }
    return 0;
}

/* Only use ATH for masking. */
int lame_set_ATHonly(lame_global_flags* gfp, int ATHonly) {
    if (is_lame_global_flags_valid(gfp)) {
        gfp->ATHonly = ATHonly;
        return 0;
    }
    return -1;
}

int lame_get_ATHonly(const lame_global_flags* gfp) {
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->ATHonly;
    }
    return 0;
}

/* Only use ATH for short blocks. */
int lame_set_ATHshort(lame_global_flags* gfp, int ATHshort) {
    if (is_lame_global_flags_valid(gfp)) {
        gfp->ATHshort = ATHshort;
        return 0;
    }
    return -1;
}

int lame_get_ATHshort(const lame_global_flags* gfp) {
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->ATHshort;
    }
    return 0;
}

/* Disable ATH. */
int lame_set_noATH(lame_global_flags* gfp, int noATH) {
    if (is_lame_global_flags_valid(gfp)) {
        gfp->noATH = noATH;
        return 0;
    }
    return -1;
}

int lame_get_noATH(const lame_global_flags* gfp) {
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->noATH;
    }
    return 0;
}

/* Select ATH formula. */
int lame_set_ATHtype(lame_global_flags* gfp, int ATHtype) {
    if (is_lame_global_flags_valid(gfp)) {
        /* XXX: ATHtype should be converted to an enum. */
        gfp->ATHtype = ATHtype;
        return 0;
    }
    return -1;
}

int lame_get_ATHtype(const lame_global_flags* gfp) {
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->ATHtype;
    }
    return 0;
}

/* Select ATH formula 4 shape. */
int lame_set_ATHcurve(lame_global_flags* gfp, float ATHcurve) {
    if (is_lame_global_flags_valid(gfp)) {
        gfp->ATHcurve = ATHcurve;
        return 0;
    }
    return -1;
}

float lame_get_ATHcurve(const lame_global_flags* gfp) {
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->ATHcurve;
    }
    return 0;
}

/* Lower ATH by this many db. */
int lame_set_ATHlower(lame_global_flags* gfp, float ATHlower) {
    if (is_lame_global_flags_valid(gfp)) {
        gfp->ATH_lower_db = ATHlower;
        return 0;
    }
    return -1;
}

float lame_get_ATHlower(const lame_global_flags* gfp) {
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->ATH_lower_db;
    }
    return 0;
}

/* Select ATH adaptive adjustment scheme. */
int lame_set_athaa_type(lame_global_flags* gfp, int athaa_type) {
    if (is_lame_global_flags_valid(gfp)) {
        gfp->athaa_type = athaa_type;
        return 0;
    }
    return -1;
}

int lame_get_athaa_type(const lame_global_flags* gfp) {
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->athaa_type;
    }
    return 0;
}

/* Adjust (in dB) the point below which adaptive ATH level adjustment occurs. */
int lame_set_athaa_sensitivity(lame_global_flags* gfp, float athaa_sensitivity) {
    if (is_lame_global_flags_valid(gfp)) {
        gfp->athaa_sensitivity = athaa_sensitivity;
        return 0;
    }
    return -1;
}

float lame_get_athaa_sensitivity(const lame_global_flags* gfp) {
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->athaa_sensitivity;
    }
    return 0;
}

/*
 * Allow blocktypes to differ between channels.
 * default:
 *  0 for jstereo => block types coupled
 *  1 for stereo  => block types may differ
 */
int lame_set_allow_diff_short(lame_global_flags* gfp, int allow_diff_short) {
    if (is_lame_global_flags_valid(gfp)) {
        gfp->short_blocks = allow_diff_short ? short_block_allowed : short_block_coupled;
        return 0;
    }
    return -1;
}

int lame_get_allow_diff_short(const lame_global_flags* gfp) {
    if (is_lame_global_flags_valid(gfp)) {
        if (gfp->short_blocks == short_block_allowed)
            return 1; /* short blocks allowed to differ */
        else
            return 0; /* not set, dispensed, forced or coupled */
    }
    return 0;
}

/* Use temporal masking effect */
int lame_set_useTemporal(lame_global_flags* gfp, int useTemporal) {
    if (is_lame_global_flags_valid(gfp)) {
        /* default = 1 (enabled) */

        /* enforce disable/enable meaning, if we need more than two values
           we need to switch to an enum to have an apropriate representation
           of the possible meanings of the value */
        if (0 <= useTemporal && useTemporal <= 1) {
            gfp->useTemporal = useTemporal;
            return 0;
        }
    }
    return -1;
}

int lame_get_useTemporal(const lame_global_flags* gfp) {
    if (is_lame_global_flags_valid(gfp)) {
        assert(0 <= gfp->useTemporal && 1 >= gfp->useTemporal);
        return gfp->useTemporal;
    }
    return 0;
}

/* Use inter-channel masking effect */
int lame_set_interChRatio(lame_global_flags* gfp, float ratio) {
    if (is_lame_global_flags_valid(gfp)) {
        /* default = 0.0 (no inter-channel maskin) */
        if (0 <= ratio && ratio <= 1.0) {
            gfp->interChRatio = ratio;
            return 0;
        }
    }
    return -1;
}

float lame_get_interChRatio(const lame_global_flags* gfp) {
    if (is_lame_global_flags_valid(gfp)) {
        assert((0 <= gfp->interChRatio && gfp->interChRatio <= 1.0) || EQ(gfp->interChRatio, -1));
        return gfp->interChRatio;
    }
    return 0;
}

/* Use pseudo substep shaping method */
int lame_set_substep(lame_global_flags* gfp, int method) {
    if (is_lame_global_flags_valid(gfp)) {
        /* default = 0.0 (no substep noise shaping) */
        if (0 <= method && method <= 7) {
            gfp->substep_shaping = method;
            return 0;
        }
    }
    return -1;
}

int lame_get_substep(const lame_global_flags* gfp) {
    if (is_lame_global_flags_valid(gfp)) {
        assert(0 <= gfp->substep_shaping && gfp->substep_shaping <= 7);
        return gfp->substep_shaping;
    }
    return 0;
}

/* scalefactors scale */
int lame_set_sfscale(lame_global_flags* gfp, int val) {
    if (is_lame_global_flags_valid(gfp)) {
        gfp->noise_shaping = (val != 0) ? 2 : 1;
        return 0;
    }
    return -1;
}

int lame_get_sfscale(const lame_global_flags* gfp) {
    if (is_lame_global_flags_valid(gfp)) {
        return (gfp->noise_shaping == 2) ? 1 : 0;
    }
    return 0;
}

/* subblock gain */
int lame_set_subblock_gain(lame_global_flags* gfp, int sbgain) {
    if (is_lame_global_flags_valid(gfp)) {
        gfp->subblock_gain = sbgain;
        return 0;
    }
    return -1;
}

int lame_get_subblock_gain(const lame_global_flags* gfp) {
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->subblock_gain;
    }
    return 0;
}

/* Disable short blocks. */
int lame_set_no_short_blocks(lame_global_flags* gfp, int no_short_blocks) {
    if (is_lame_global_flags_valid(gfp)) {
        /* enforce disable/enable meaning, if we need more than two values
           we need to switch to an enum to have an apropriate representation
           of the possible meanings of the value */
        if (0 <= no_short_blocks && no_short_blocks <= 1) {
            gfp->short_blocks = no_short_blocks ? short_block_dispensed : short_block_allowed;
            return 0;
        }
    }
    return -1;
}

int lame_get_no_short_blocks(const lame_global_flags* gfp) {
    if (is_lame_global_flags_valid(gfp)) {
        switch (gfp->short_blocks) {
            default:
            case short_block_not_set:
                return -1;
            case short_block_dispensed:
                return 1;
            case short_block_allowed:
            case short_block_coupled:
            case short_block_forced:
                return 0;
        }
    }
    return -1;
}

/* Force short blocks. */
int lame_set_force_short_blocks(lame_global_flags* gfp, int short_blocks) {
    if (is_lame_global_flags_valid(gfp)) {
        /* enforce disable/enable meaning, if we need more than two values
           we need to switch to an enum to have an apropriate representation
           of the possible meanings of the value */
        if (0 > short_blocks || 1 < short_blocks)
            return -1;

        if (short_blocks == 1)
            gfp->short_blocks = short_block_forced;
        else if (gfp->short_blocks == short_block_forced)
            gfp->short_blocks = short_block_allowed;

        return 0;
    }
    return -1;
}

int lame_get_force_short_blocks(const lame_global_flags* gfp) {
    if (is_lame_global_flags_valid(gfp)) {
        switch (gfp->short_blocks) {
            default:
            case short_block_not_set:
                return -1;
            case short_block_dispensed:
            case short_block_allowed:
            case short_block_coupled:
                return 0;
            case short_block_forced:
                return 1;
        }
    }
    return -1;
}

int lame_set_short_threshold_lrm(lame_global_flags* gfp, float lrm) {
    if (is_lame_global_flags_valid(gfp)) {
        gfp->attackthre = lrm;
        return 0;
    }
    return -1;
}

float lame_get_short_threshold_lrm(const lame_global_flags* gfp) {
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->attackthre;
    }
    return 0;
}

int lame_set_short_threshold_s(lame_global_flags* gfp, float s) {
    if (is_lame_global_flags_valid(gfp)) {
        gfp->attackthre_s = s;
        return 0;
    }
    return -1;
}

float lame_get_short_threshold_s(const lame_global_flags* gfp) {
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->attackthre_s;
    }
    return 0;
}

int lame_set_short_threshold(lame_global_flags* gfp, float lrm, float s) {
    if (is_lame_global_flags_valid(gfp)) {
        lame_set_short_threshold_lrm(gfp, lrm);
        lame_set_short_threshold_s(gfp, s);
        return 0;
    }
    return -1;
}

/*
 * Input PCM is emphased PCM
 * (for instance from one of the rarely emphased CDs).
 *
 * It is STRONGLY not recommended to use this, because psycho does not
 * take it into account, and last but not least many decoders
 * ignore these bits
 */
int lame_set_emphasis(lame_global_flags* gfp, int emphasis) {
    if (is_lame_global_flags_valid(gfp)) {
        /* XXX: emphasis should be converted to an enum */
        if (0 <= emphasis && emphasis < 4) {
            gfp->emphasis = emphasis;
            return 0;
        }
    }
    return -1;
}

int lame_get_emphasis(const lame_global_flags* gfp) {
    if (is_lame_global_flags_valid(gfp)) {
        assert(0 <= gfp->emphasis && gfp->emphasis < 4);
        return gfp->emphasis;
    }
    return 0;
}
