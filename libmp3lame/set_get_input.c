/* -*- mode: C; mode: fold -*- */
/*
 * set/get functions for input and general stream options
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

/* number of samples */
/* it's unlikely for this function to return an error */
int lame_set_num_samples(lame_global_flags* gfp, unsigned long num_samples) {
    if (is_lame_global_flags_valid(gfp)) {
        /* default = 2^32-1 */
        gfp->num_samples = num_samples;
        return 0;
    }
    return -1;
}

unsigned long lame_get_num_samples(const lame_global_flags* gfp) {
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->num_samples;
    }
    return 0;
}

/* input samplerate */
int lame_set_in_samplerate(lame_global_flags* gfp, int in_samplerate) {
    if (is_lame_global_flags_valid(gfp)) {
        if (in_samplerate < 1)
            return -1;
        /* input sample rate in Hz,  default = 44100 Hz */
        gfp->samplerate_in = in_samplerate;
        return 0;
    }
    return -1;
}

int lame_get_in_samplerate(const lame_global_flags* gfp) {
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->samplerate_in;
    }
    return 0;
}

/* number of channels in input stream */
int lame_set_num_channels(lame_global_flags* gfp, int num_channels) {
    if (is_lame_global_flags_valid(gfp)) {
        /* default = 2 */
        if (2 < num_channels || 0 >= num_channels) {
            return -1; /* we don't support more than 2 channels */
        }
        gfp->num_channels = num_channels;
        return 0;
    }
    return -1;
}

int lame_get_num_channels(const lame_global_flags* gfp) {
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->num_channels;
    }
    return 0;
}

/* scale the input by this amount before encoding (not used for decoding) */
int lame_set_scale(lame_global_flags* gfp, float scale) {
    if (is_lame_global_flags_valid(gfp)) {
        /* default = 1 */
        gfp->scale = scale;
        return 0;
    }
    return -1;
}

float lame_get_scale(const lame_global_flags* gfp) {
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->scale;
    }
    return 0;
}

/* scale the channel 0 (left) input by this amount before
   encoding (not used for decoding) */
int lame_set_scale_left(lame_global_flags* gfp, float scale) {
    if (is_lame_global_flags_valid(gfp)) {
        /* default = 1 */
        gfp->scale_left = scale;
        return 0;
    }
    return -1;
}

float lame_get_scale_left(const lame_global_flags* gfp) {
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->scale_left;
    }
    return 0;
}

/* scale the channel 1 (right) input by this amount before
   encoding (not used for decoding) */
int lame_set_scale_right(lame_global_flags* gfp, float scale) {
    if (is_lame_global_flags_valid(gfp)) {
        /* default = 1 */
        gfp->scale_right = scale;
        return 0;
    }
    return -1;
}

float lame_get_scale_right(const lame_global_flags* gfp) {
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->scale_right;
    }
    return 0;
}

/* output sample rate in Hz */
int lame_set_out_samplerate(lame_global_flags* gfp, int out_samplerate) {
    if (is_lame_global_flags_valid(gfp)) {
        /*
         * default = 0: LAME picks best value based on the amount
         *              of compression
         * MPEG only allows:
         *  MPEG1    32, 44.1,   48khz
         *  MPEG2    16, 22.05,  24
         *  MPEG2.5   8, 11.025, 12
         *
         * (not used by decoding routines)
         */
        if (out_samplerate != 0) {
            int v = 0;
            if (SmpFrqIndex(out_samplerate, &v) < 0)
                return -1;
        }
        gfp->samplerate_out = out_samplerate;
        return 0;
    }
    return -1;
}

int lame_get_out_samplerate(const lame_global_flags* gfp) {
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->samplerate_out;
    }
    return 0;
}

/*
 * general control parameters
 */

/* collect data for an MP3 frame analzyer */
int lame_set_analysis(lame_global_flags* gfp, int analysis) {
    if (is_lame_global_flags_valid(gfp)) {
        /* default = 0 */

        /* enforce disable/enable meaning, if we need more than two values
           we need to switch to an enum to have an apropriate representation
           of the possible meanings of the value */
        if (0 > analysis || 1 < analysis)
            return -1;
        gfp->analysis = analysis;
        return 0;
    }
    return -1;
}

int lame_get_analysis(const lame_global_flags* gfp) {
    if (is_lame_global_flags_valid(gfp)) {
        assert(0 <= gfp->analysis && 1 >= gfp->analysis);
        return gfp->analysis;
    }
    return 0;
}

/* write a Xing VBR header frame */
int lame_set_bWriteVbrTag(lame_global_flags* gfp, int bWriteVbrTag) {
    if (is_lame_global_flags_valid(gfp)) {
        /* default = 1 (on) for VBR/ABR modes, 0 (off) for CBR mode */

        /* enforce disable/enable meaning, if we need more than two values
           we need to switch to an enum to have an apropriate representation
           of the possible meanings of the value */
        if (0 > bWriteVbrTag || 1 < bWriteVbrTag)
            return -1;
        gfp->write_lame_tag = bWriteVbrTag;
        return 0;
    }
    return -1;
}

int lame_get_bWriteVbrTag(const lame_global_flags* gfp) {
    if (is_lame_global_flags_valid(gfp)) {
        assert(0 <= gfp->write_lame_tag && 1 >= gfp->write_lame_tag);
        return gfp->write_lame_tag;
    }
    return 0;
}

/*
 * Internal algorithm selection.
 * True quality is determined by the bitrate but this variable will effect
 * quality by selecting expensive or cheap algorithms.
 * quality=0..9.  0=best (very slow).  9=worst.
 * recommended:  3     near-best quality, not too slow
 *               5     good quality, fast
 *               7     ok quality, really fast
 */
int lame_set_quality(lame_global_flags* gfp, int quality) {
    if (is_lame_global_flags_valid(gfp)) {
        if (quality < 0) {
            gfp->quality = 0;
        }
        else if (quality > 9) {
            gfp->quality = 9;
        }
        else {
            gfp->quality = quality;
        }
        return 0;
    }
    return -1;
}

int lame_get_quality(const lame_global_flags* gfp) {
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->quality;
    }
    return 0;
}

/* mode = STEREO, JOINT_STEREO, DUAL_CHANNEL (not supported), MONO */
int lame_set_mode(lame_global_flags* gfp, MPEG_mode mode) {
    if (is_lame_global_flags_valid(gfp)) {
        int mpg_mode = mode;
        /* default: lame chooses based on compression ratio and input channels */
        if (mpg_mode < 0 || MAX_INDICATOR <= mpg_mode)
            return -1; /* Unknown MPEG mode! */
        gfp->mode = mode;
        return 0;
    }
    return -1;
}

MPEG_mode lame_get_mode(const lame_global_flags* gfp) {
    if (is_lame_global_flags_valid(gfp)) {
        assert(gfp->mode < MAX_INDICATOR);
        return gfp->mode;
    }
    return NOT_SET;
}

/*
 * Force M/S for all frames.  For testing only.
 * Requires mode = 1.
 */
int lame_set_force_ms(lame_global_flags* gfp, int force_ms) {
    if (is_lame_global_flags_valid(gfp)) {
        /* default = 0 (disabled) */

        /* enforce disable/enable meaning, if we need more than two values
           we need to switch to an enum to have an apropriate representation
           of the possible meanings of the value */
        if (0 > force_ms || 1 < force_ms)
            return -1;
        gfp->force_ms = force_ms;
        return 0;
    }
    return -1;
}

int lame_get_force_ms(const lame_global_flags* gfp) {
    if (is_lame_global_flags_valid(gfp)) {
        assert(0 <= gfp->force_ms && 1 >= gfp->force_ms);
        return gfp->force_ms;
    }
    return 0;
}

/* Use free_format. */
int lame_set_free_format(lame_global_flags* gfp, int free_format) {
    if (is_lame_global_flags_valid(gfp)) {
        /* default = 0 (disabled) */

        /* enforce disable/enable meaning, if we need more than two values
           we need to switch to an enum to have an apropriate representation
           of the possible meanings of the value */
        if (0 > free_format || 1 < free_format)
            return -1;
        gfp->free_format = free_format;
        return 0;
    }
    return -1;
}

int lame_get_free_format(const lame_global_flags* gfp) {
    if (is_lame_global_flags_valid(gfp)) {
        assert(0 <= gfp->free_format && 1 >= gfp->free_format);
        return gfp->free_format;
    }
    return 0;
}

/* Perform ReplayGain analysis */
int lame_set_findReplayGain(lame_global_flags* gfp, int findReplayGain) {
    if (is_lame_global_flags_valid(gfp)) {
        /* default = 0 (disabled) */

        /* enforce disable/enable meaning, if we need more than two values
           we need to switch to an enum to have an apropriate representation
           of the possible meanings of the value */
        if (0 > findReplayGain || 1 < findReplayGain)
            return -1;
        gfp->findReplayGain = findReplayGain;
        return 0;
    }
    return -1;
}

int lame_get_findReplayGain(const lame_global_flags* gfp) {
    if (is_lame_global_flags_valid(gfp)) {
        assert(0 <= gfp->findReplayGain && 1 >= gfp->findReplayGain);
        return gfp->findReplayGain;
    }
    return 0;
}

/* set and get some gapless encoding flags */

int lame_set_nogap_total(lame_global_flags* gfp, int the_nogap_total) {
    if (is_lame_global_flags_valid(gfp)) {
        gfp->nogap_total = the_nogap_total;
        return 0;
    }
    return -1;
}

int lame_get_nogap_total(const lame_global_flags* gfp) {
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->nogap_total;
    }
    return 0;
}

int lame_set_nogap_currentindex(lame_global_flags* gfp, int the_nogap_index) {
    if (is_lame_global_flags_valid(gfp)) {
        gfp->nogap_current = the_nogap_index;
        return 0;
    }
    return -1;
}

int lame_get_nogap_currentindex(const lame_global_flags* gfp) {
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->nogap_current;
    }
    return 0;
}

/* message handlers */
int lame_set_errorf(lame_global_flags* gfp, void (*func)(const char*, va_list)) {
    if (is_lame_global_flags_valid(gfp)) {
        gfp->report.errorf = func;
        return 0;
    }
    return -1;
}

int lame_set_debugf(lame_global_flags* gfp, void (*func)(const char*, va_list)) {
    if (is_lame_global_flags_valid(gfp)) {
        gfp->report.debugf = func;
        return 0;
    }
    return -1;
}

int lame_set_msgf(lame_global_flags* gfp, void (*func)(const char*, va_list)) {
    if (is_lame_global_flags_valid(gfp)) {
        gfp->report.msgf = func;
        return 0;
    }
    return -1;
}

/*
 * Set one of
 *  - brate
 *  - compression ratio.
 *
 * Default is compression ratio of 11.
 */
int lame_set_brate(lame_global_flags* gfp, int brate) {
    if (is_lame_global_flags_valid(gfp)) {
        gfp->brate = brate;
        if (brate > 320) {
            gfp->disable_reservoir = 1;
        }
        return 0;
    }
    return -1;
}

int lame_get_brate(const lame_global_flags* gfp) {
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->brate;
    }
    return 0;
}

int lame_set_compression_ratio(lame_global_flags* gfp, float compression_ratio) {
    if (is_lame_global_flags_valid(gfp)) {
        gfp->compression_ratio = compression_ratio;
        return 0;
    }
    return -1;
}

float lame_get_compression_ratio(const lame_global_flags* gfp) {
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->compression_ratio;
    }
    return 0;
}

/*
 * frame parameters
 */

/* Mark as copyright protected. */
int lame_set_copyright(lame_global_flags* gfp, int copyright) {
    if (is_lame_global_flags_valid(gfp)) {
        /* default = 0 (disabled) */

        /* enforce disable/enable meaning, if we need more than two values
           we need to switch to an enum to have an apropriate representation
           of the possible meanings of the value */
        if (0 > copyright || 1 < copyright)
            return -1;
        gfp->copyright = copyright;
        return 0;
    }
    return -1;
}

int lame_get_copyright(const lame_global_flags* gfp) {
    if (is_lame_global_flags_valid(gfp)) {
        assert(0 <= gfp->copyright && 1 >= gfp->copyright);
        return gfp->copyright;
    }
    return 0;
}

/* Mark as original. */
int lame_set_original(lame_global_flags* gfp, int original) {
    if (is_lame_global_flags_valid(gfp)) {
        /* default = 1 (enabled) */

        /* enforce disable/enable meaning, if we need more than two values
           we need to switch to an enum to have an apropriate representation
           of the possible meanings of the value */
        if (0 > original || 1 < original)
            return -1;
        gfp->original = original;
        return 0;
    }
    return -1;
}

int lame_get_original(const lame_global_flags* gfp) {
    if (is_lame_global_flags_valid(gfp)) {
        assert(0 <= gfp->original && 1 >= gfp->original);
        return gfp->original;
    }
    return 0;
}

/*
 * error_protection.
 * Use 2 bytes from each frame for CRC checksum.
 */
int lame_set_error_protection(lame_global_flags* gfp, int error_protection) {
    if (is_lame_global_flags_valid(gfp)) {
        /* default = 0 (disabled) */

        /* enforce disable/enable meaning, if we need more than two values
           we need to switch to an enum to have an apropriate representation
           of the possible meanings of the value */
        if (0 > error_protection || 1 < error_protection)
            return -1;
        gfp->error_protection = error_protection;
        return 0;
    }
    return -1;
}

int lame_get_error_protection(const lame_global_flags* gfp) {
    if (is_lame_global_flags_valid(gfp)) {
        assert(0 <= gfp->error_protection && 1 >= gfp->error_protection);
        return gfp->error_protection;
    }
    return 0;
}

/* MP3 'private extension' bit. Meaningless. */
int lame_set_extension(lame_global_flags* gfp, int extension) {
    if (is_lame_global_flags_valid(gfp)) {
        /* default = 0 (disabled) */
        /* enforce disable/enable meaning, if we need more than two values
           we need to switch to an enum to have an apropriate representation
           of the possible meanings of the value */
        if (0 > extension || 1 < extension)
            return -1;
        gfp->extension = extension;
        return 0;
    }
    return -1;
}

int lame_get_extension(const lame_global_flags* gfp) {
    if (is_lame_global_flags_valid(gfp)) {
        assert(0 <= gfp->extension && 1 >= gfp->extension);
        return gfp->extension;
    }
    return 0;
}

/* Enforce strict ISO compliance. */
int lame_set_strict_ISO(lame_global_flags* gfp, int val) {
    if (is_lame_global_flags_valid(gfp)) {
        /* default = 0 (disabled) */
        /* enforce disable/enable meaning, if we need more than two values
           we need to switch to an enum to have an apropriate representation
           of the possible meanings of the value */
        if (val < MDB_DEFAULT || MDB_MAXIMUM < val)
            return -1;
        gfp->strict_ISO = val;
        return 0;
    }
    return -1;
}

int lame_get_strict_ISO(const lame_global_flags* gfp) {
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->strict_ISO;
    }
    return 0;
}
