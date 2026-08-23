/* -*- mode: C; mode: fold -*- */
/*
 * LAME MP3 encoding engine
 *
 * Stream flushing, tags, and encoder teardown.
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
#include "steadyprotect.h"
#include "VbrTag.h"
#include "id3tag.h"

#include "lame_internal.h"

static void
save_gain_values(lame_internal_flags * gfc)
{
    SessionConfig_t const *const cfg = &gfc->cfg;
    RpgStateVar_t const *const rsv = &gfc->sv_rpg;
    RpgResult_t *const rov = &gfc->ov_rpg;
    /* save the ReplayGain value */
    if (cfg->findReplayGain) {
        FLOAT const RadioGain = (FLOAT) GetTitleGain(rsv->rgdata);
        if (NEQ(RadioGain, GAIN_NOT_ENOUGH_SAMPLES)) {
            rov->RadioGain = (int) floor(RadioGain * 10.0 + 0.5); /* round to nearest */
        }
        else {
            rov->RadioGain = 0;
        }
    }

    /* find the gain and scale change required for no clipping */
    if (cfg->findPeakSample) {
        rov->noclipGainChange = (int) ceil(log10(rov->PeakSample / 32767.0) * 20.0 * 10.0); /* round up */

        if (rov->noclipGainChange > 0) { /* clipping occurs */
            rov->noclipScale = floor((32767.0f / rov->PeakSample) * 100.0f) / 100.0f; /* round down */
        }
        else            /* no clipping */
            rov->noclipScale = -1.0f;
    }
}



/*****************************************************************
 Flush mp3 buffer, pad with ancillary data so last frame is complete.
 Reset reservoir size to 0
 but keep all PCM samples and MDCT data in memory
 This option is used to break a large file into several mp3 files
 that when concatenated together will decode with no gaps
 Because we set the reservoir=0, they will also decode seperately
 with no errors.
*********************************************************************/
int
lame_encode_flush_nogap(lame_global_flags * gfp, unsigned char *mp3buffer, int mp3buffer_size)
{
    int     rc = -3;
    if (is_lame_global_flags_valid(gfp)) {
        lame_internal_flags *const gfc = gfp->internal_flags;
        if (is_lame_internal_flags_valid(gfc)) {
            flush_bitstream(gfc);
            /* if user specifed buffer size = 0, dont check size */
            if (mp3buffer_size == 0)
                mp3buffer_size = INT_MAX;
            rc = copy_buffer(gfc, mp3buffer, mp3buffer_size, 1);
            save_gain_values(gfc);
        }
    }
    return rc;
}


/* called by lame_init_params.  You can also call this after flush_nogap
   if you want to write new id3v2 and Xing VBR tags into the bitstream */
int
lame_init_bitstream(lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        lame_internal_flags *const gfc = gfp->internal_flags;
        if (gfc != 0) {
            gfc->ov_enc.frame_number = 0;

            if (gfp->write_id3tag_automatic) {
                (void) id3tag_write_v2(gfp);
            }
            /* initialize optional histogram data */
            memset(gfc->ov_enc.bitrate_channelmode_hist, 0,
                   sizeof(gfc->ov_enc.bitrate_channelmode_hist));
            memset(gfc->ov_enc.bitrate_blocktype_hist, 0,
                   sizeof(gfc->ov_enc.bitrate_blocktype_hist));

            gfc->ov_rpg.PeakSample = 0.0;

            /* Write initial VBR Header to bitstream and init VBR data */
            if (gfc->cfg.write_lame_tag)
                (void) InitVbrTag(gfp);


            return 0;
        }
    }
    return -3;
}

static int
calc_mp3buffer_size_remaining( int mp3buffer_size, int mp3count)
{
    /* if user specifed buffer size = 0, dont check size */
    if (mp3buffer_size == 0)
        return INT_MAX;
    else if (mp3buffer_size > 0 && mp3count >= 0 ) {
        int const mp3buffer_size_remaining = mp3buffer_size - mp3count;
        if (mp3buffer_size_remaining > 0) {
            return mp3buffer_size_remaining;
        }
        assert(mp3buffer_size_remaining >= 0);
        /* we reached the end of the output buffer, set to -1
           because we have to distinguish this case from the case above,
           where the caller did not specify any buffer size
         */
        return -1;
    }
    else {
        /* values are invalid, block writing into output buffer */
        return -1;
    }
}

/*****************************************************************/
/* flush internal PCM sample buffers, then mp3 buffers           */
/* then write id3 v1 tags into bitstream.                        */
/*****************************************************************/

int
lame_encode_flush(lame_global_flags * gfp, unsigned char *mp3buffer, int mp3buffer_size)
{
    lame_internal_flags *gfc;
    SessionConfig_t const *cfg;
    EncStateVar_t *esv;
    short int buffer[2][1152];
    int     imp3 = 0, mp3count, mp3buffer_size_remaining;

    /* we always add POSTDELAY=288 padding to make sure granule with real
     * data can be complety decoded (because of 50% overlap with next granule */
    int     end_padding;
    int     frames_left;
    int     samples_to_encode;
    int     pcm_samples_per_frame;
    int     mf_needed;
    int     is_resampling_necessary;
    double  resample_ratio = 1;

    if (!is_lame_global_flags_valid(gfp)) {
        return -3;
    }
    gfc = gfp->internal_flags;
    if (!is_lame_internal_flags_valid(gfc)) {
        return -3;
    }
    cfg = &gfc->cfg;
    esv = &gfc->sv_enc;
    
    /* Was flush already called? */
    if (esv->mf_samples_to_encode < 1) {
        return 0;
    }
    pcm_samples_per_frame = 576 * cfg->mode_gr;
    mf_needed = lame_calc_needed(cfg);

    samples_to_encode = esv->mf_samples_to_encode - POSTDELAY;

    memset(buffer, 0, sizeof(buffer));
    mp3count = 0;

    is_resampling_necessary = isResamplingNecessary(cfg);
    if (is_resampling_necessary) {
        resample_ratio = (double)cfg->samplerate_in / (double)cfg->samplerate_out;
        /* delay due to resampling; needs to be fixed, if resampling code gets changed */
        samples_to_encode += 16. / resample_ratio;
    }
    end_padding = pcm_samples_per_frame - (samples_to_encode % pcm_samples_per_frame);
    if (end_padding < 576)
        end_padding += pcm_samples_per_frame;
    gfc->ov_enc.encoder_padding = end_padding;
    
    frames_left = (samples_to_encode + end_padding) / pcm_samples_per_frame;
    while (frames_left > 0 && imp3 >= 0) {
        int const frame_num = gfc->ov_enc.frame_number;
        int     bunch = mf_needed - esv->mf_size;

        bunch *= resample_ratio;
        if (bunch > 1152) bunch = 1152;
        if (bunch < 1) bunch = 1;

        mp3buffer_size_remaining = calc_mp3buffer_size_remaining(mp3buffer_size, mp3count);

        /* send in a frame of 0 padding until all internal sample buffers
         * are flushed
         */
        imp3 = lame_encode_buffer(gfp, buffer[0], buffer[1], bunch,
                                  mp3buffer, mp3buffer_size_remaining);
        if (imp3 > 0) {
            mp3buffer += imp3;
            mp3count += imp3;
        }
        {   /* even a single pcm sample can produce several frames!
             * for example: 1 Hz input file resampled to 8 kHz mpeg2.5
             */
            int const new_frames = gfc->ov_enc.frame_number - frame_num;
            if (new_frames > 0)
                frames_left -=  new_frames;
        }
    }
    /* Set esv->mf_samples_to_encode to 0, so we may detect
     * and break loops calling it more than once in a row.
     */
    esv->mf_samples_to_encode = 0;

    if (imp3 < 0) {
        /* some type of fatal error */
        return imp3;
    }

    mp3buffer_size_remaining = calc_mp3buffer_size_remaining(mp3buffer_size, mp3count);

    /* mp3 related stuff.  bit buffer might still contain some mp3 data */
    flush_bitstream(gfc);
    imp3 = copy_buffer(gfc, mp3buffer, mp3buffer_size_remaining, 1);
    save_gain_values(gfc);
    if (imp3 < 0) {
        /* some type of fatal error */
        return imp3;
    }
    mp3buffer += imp3;
    mp3count += imp3;
    mp3buffer_size_remaining = mp3buffer_size - mp3count;
    /* if user specifed buffer size = 0, dont check size */
    if (mp3buffer_size == 0)
        mp3buffer_size_remaining = INT_MAX;

    if (gfp->write_id3tag_automatic) {
        /* write a id3 tag to the bitstream */
        (void) id3tag_write_v1(gfp);

        imp3 = copy_buffer(gfc, mp3buffer, mp3buffer_size_remaining, 0);

        if (imp3 < 0) {
            return imp3;
        }
        mp3count += imp3;
    }
    return mp3count;
}

/***********************************************************************
 *
 *      lame_close ()
 *
 *  frees internal buffers
 *
 ***********************************************************************/

int
lame_close(lame_global_flags * gfp)
{
    int     ret = 0;
    if (gfp && gfp->class_id == LAME_ID) {
        lame_internal_flags *const gfc = gfp->internal_flags;
        gfp->class_id = 0;
        if (NULL == gfc || gfc->class_id != LAME_ID) {
            ret = -3;
        }
        if (NULL != gfc) {
            steady_tonal_dump_stats_if_requested(gfc);
            gfc->lame_init_params_successful = 0;
            gfc->class_id = 0;
            /* this routine will free all malloc'd data in gfc, and then free gfc: */
            freegfc(gfc);
            gfp->internal_flags = NULL;
        }
        if (gfp->lame_allocated_gfp) {
            gfp->lame_allocated_gfp = 0;
            free(gfp);
        }
    }
    return ret;
}

/*****************************************************************/
/* write VBR Xing header, and ID3 version 1 tag, if asked for    */
/*****************************************************************/
void    lame_mp3_tags_fid(lame_global_flags * gfp, FILE * fpStream);

void
lame_mp3_tags_fid(lame_global_flags * gfp, FILE * fpStream)
{
    lame_internal_flags *gfc;
    SessionConfig_t const *cfg;
    if (!is_lame_global_flags_valid(gfp)) {
        return;
    }
    gfc = gfp->internal_flags;
    if (!is_lame_internal_flags_valid(gfc)) {
        return;
    }
    cfg = &gfc->cfg;
    if (!cfg->write_lame_tag) {
        return;
    }
    /* Write Xing header again */
    if (fpStream && !fseek(fpStream, 0, SEEK_SET)) {
        int     rc = PutVbrTag(gfp, fpStream);
        switch (rc) {
        default:
            /* OK */
            break;

        case -1:
            ERRORF(gfc, "Error: could not update LAME tag.\n");
            break;

        case -2:
            ERRORF(gfc, "Error: could not update LAME tag, file not seekable.\n");
            break;

        case -3:
            ERRORF(gfc, "Error: could not update LAME tag, file not readable.\n");
            break;
        }
    }
}
