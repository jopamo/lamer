/* -*- mode: C; mode: fold -*- */
/*
 * LAME MP3 encoding engine
 *
 * PCM ingress, sample conversion, and frame encoding.
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
#include <config.h>
#endif

#include "bitstream.h"
#include "encoder.h"
#include "gain_analysis.h"
#include "lame.h"
#include "lame_global_flags.h"
#include "machine.h"
#include "util.h"

#include "lame_internal.h"

static int update_inbuffer_size(lame_internal_flags *gfc, const int nsamples) {
  EncStateVar_t *const esv = &gfc->sv_enc;
  if (esv->in_buffer_0 == 0 || esv->in_buffer_nsamples < nsamples) {
    if (esv->in_buffer_0) {
      free(esv->in_buffer_0);
    }
    if (esv->in_buffer_1) {
      free(esv->in_buffer_1);
    }
    esv->in_buffer_0 = lame_calloc(sample_t, nsamples);
    esv->in_buffer_1 = lame_calloc(sample_t, nsamples);
    esv->in_buffer_nsamples = nsamples;
  }
  if (esv->in_buffer_0 == NULL || esv->in_buffer_1 == NULL) {
    if (esv->in_buffer_0) {
      free(esv->in_buffer_0);
    }
    if (esv->in_buffer_1) {
      free(esv->in_buffer_1);
    }
    esv->in_buffer_0 = 0;
    esv->in_buffer_1 = 0;
    esv->in_buffer_nsamples = 0;
    ERRORF(gfc, "Error: can't allocate in_buffer buffer\n");
    return -2;
  }
  return 0;
}

/*
 * THE MAIN LAME ENCODING INTERFACE
 * mt 3/00
 *
 * input pcm data, output (maybe) mp3 frames.
 * This routine handles all buffering, resampling and filtering for you.
 * The required mp3buffer_size can be computed from num_samples,
 * samplerate and encoding rate, but here is a worst case estimate:
 *
 * mp3buffer_size in bytes = 1.25*num_samples + 7200
 *
 * return code = number of bytes output in mp3buffer.  can be 0
 *
 * NOTE: this routine uses LAME's internal PCM data representation,
 * 'sample_t'.  It should not be used by any application.
 * applications should use lame_encode_buffer(),
 *                         lame_encode_buffer_float()
 *                         lame_encode_buffer_int()
 * etc... depending on what type of data they are working with.
 */
static int lame_encode_buffer_sample_t(lame_internal_flags *gfc, int nsamples,
                                       unsigned char *mp3buf,
                                       const int mp3buf_size) {
  SessionConfig_t const *const cfg = &gfc->cfg;
  EncStateVar_t *const esv = &gfc->sv_enc;
  int pcm_samples_per_frame = 576 * cfg->mode_gr;
  int mp3size = 0, ret, i, ch, mf_needed;
  int mp3out;
  sample_t *mfbuf[2];
  sample_t *in_buffer[2];

  if (gfc->class_id != LAME_ID)
    return -3;

  if (nsamples == 0)
    return 0;

  /* copy out any tags that may have been written into bitstream */
  { /* if user specifed buffer size = 0, dont check size */
    int const buf_size = mp3buf_size == 0 ? INT_MAX : mp3buf_size;
    mp3out = copy_buffer(gfc, mp3buf, buf_size, 0);
  }
  if (mp3out < 0)
    return mp3out; /* not enough buffer space */
  mp3buf += mp3out;
  mp3size += mp3out;

  in_buffer[0] = esv->in_buffer_0;
  in_buffer[1] = esv->in_buffer_1;

  mf_needed = lame_calc_needed(cfg);

  mfbuf[0] = esv->mfbuf[0];
  mfbuf[1] = esv->mfbuf[1];

  while (nsamples > 0) {
    sample_t const *in_buffer_ptr[2];
    int n_in = 0;  /* number of input samples processed with fill_buffer */
    int n_out = 0; /* number of samples output with fill_buffer */
    /* n_in <> n_out if we are resampling */

    in_buffer_ptr[0] = in_buffer[0];
    in_buffer_ptr[1] = in_buffer[1];
    /* copy in new samples into mfbuf, with resampling */
    fill_buffer(gfc, mfbuf, &in_buffer_ptr[0], nsamples, &n_in, &n_out);

    /* compute ReplayGain of resampled input if requested */
    if (cfg->findReplayGain)
      if (AnalyzeSamples(gfc->sv_rpg.rgdata, &mfbuf[0][esv->mf_size],
                         &mfbuf[1][esv->mf_size], n_out,
                         cfg->channels_out) == GAIN_ANALYSIS_ERROR)
        return -6;

    /* update in_buffer counters */
    nsamples -= n_in;
    in_buffer[0] += n_in;
    if (cfg->channels_out == 2)
      in_buffer[1] += n_in;

    /* update mfbuf[] counters */
    esv->mf_size += n_out;
    assert(esv->mf_size <= MFSIZE);

    /* lame_encode_flush may have set gfc->mf_sample_to_encode to 0
     * so we have to reinitialize it here when that happened.
     */
    if (esv->mf_samples_to_encode < 1) {
      esv->mf_samples_to_encode = ENCDELAY + POSTDELAY;
    }
    esv->mf_samples_to_encode += n_out;

    if (esv->mf_size >= mf_needed) {
      /* encode the frame.  */
      /* mp3buf              = pointer to current location in buffer */
      /* mp3buf_size         = size of original mp3 output buffer */
      /*                     = 0 if we should not worry about the */
      /*                       buffer size because calling program is  */
      /*                       to lazy to compute it */
      /* mp3size             = size of data written to buffer so far */
      /* mp3buf_size-mp3size = amount of space avalable  */

      int buf_size = mp3buf_size - mp3size;
      if (mp3buf_size == 0)
        buf_size = INT_MAX;

      ret = lame_encode_mp3_frame(gfc, mfbuf[0], mfbuf[1], mp3buf, buf_size);

      if (ret < 0)
        return ret;
      mp3buf += ret;
      mp3size += ret;

      /* shift out old samples */
      esv->mf_size -= pcm_samples_per_frame;
      esv->mf_samples_to_encode -= pcm_samples_per_frame;
      for (ch = 0; ch < cfg->channels_out; ch++)
        for (i = 0; i < esv->mf_size; i++)
          mfbuf[ch][i] = mfbuf[ch][i + pcm_samples_per_frame];
    }
  }
  assert(nsamples == 0);

  return mp3size;
}

enum PCMSampleType {
  pcm_short_type,
  pcm_int_type,
  pcm_long_type,
  pcm_float_type,
  pcm_double_type
};

static void lame_copy_inbuffer(lame_internal_flags *gfc, void const *l,
                               void const *r, int nsamples,
                               enum PCMSampleType pcm_type, int jump, FLOAT s) {
  SessionConfig_t const *const cfg = &gfc->cfg;
  EncStateVar_t *const esv = &gfc->sv_enc;
  sample_t *ib0 = esv->in_buffer_0;
  sample_t *ib1 = esv->in_buffer_1;
  FLOAT m[2][2];

  /* Apply user defined re-scaling */
  m[0][0] = s * cfg->pcm_transform[0][0];
  m[0][1] = s * cfg->pcm_transform[0][1];
  m[1][0] = s * cfg->pcm_transform[1][0];
  m[1][1] = s * cfg->pcm_transform[1][1];

  /* make a copy of input buffer, changing type to sample_t */
#define COPY_AND_TRANSFORM(T)                                                  \
  {                                                                            \
    T const *bl = l, *br = r;                                                  \
    int i;                                                                     \
    for (i = 0; i < nsamples; i++) {                                           \
      sample_t const xl = *bl;                                                 \
      sample_t const xr = *br;                                                 \
      sample_t const u = xl * m[0][0] + xr * m[0][1];                          \
      sample_t const v = xl * m[1][0] + xr * m[1][1];                          \
      ib0[i] = u;                                                              \
      ib1[i] = v;                                                              \
      bl += jump;                                                              \
      br += jump;                                                              \
    }                                                                          \
  }
  switch (pcm_type) {
  case pcm_short_type:
    COPY_AND_TRANSFORM(short int);
    break;
  case pcm_int_type:
    COPY_AND_TRANSFORM(int);
    break;
  case pcm_long_type:
    COPY_AND_TRANSFORM(long int);
    break;
  case pcm_float_type:
    COPY_AND_TRANSFORM(float);
    break;
  case pcm_double_type:
    COPY_AND_TRANSFORM(double);
    break;
  }
}

static int
lame_encode_buffer_template(lame_global_flags *gfp, void const *buffer_l,
                            void const *buffer_r, const int nsamples,
                            unsigned char *mp3buf, const int mp3buf_size,
                            enum PCMSampleType pcm_type, int aa, FLOAT norm) {
  if (is_lame_global_flags_valid(gfp)) {
    lame_internal_flags *const gfc = gfp->internal_flags;
    if (is_lame_internal_flags_valid(gfc)) {
      SessionConfig_t const *const cfg = &gfc->cfg;

      if (nsamples == 0)
        return 0;

      if (update_inbuffer_size(gfc, nsamples) != 0) {
        return -2;
      }
      /* make a copy of input buffer, changing type to sample_t */
      if (cfg->channels_in > 1) {
        if (buffer_l == 0 || buffer_r == 0) {
          return 0;
        }
        lame_copy_inbuffer(gfc, buffer_l, buffer_r, nsamples, pcm_type, aa,
                           norm);
      } else {
        if (buffer_l == 0) {
          return 0;
        }
        lame_copy_inbuffer(gfc, buffer_l, buffer_l, nsamples, pcm_type, aa,
                           norm);
      }

      return lame_encode_buffer_sample_t(gfc, nsamples, mp3buf, mp3buf_size);
    }
  }
  return -3;
}

int lame_encode_buffer(lame_global_flags *gfp, const short int pcm_l[],
                       const short int pcm_r[], const int nsamples,
                       unsigned char *mp3buf, const int mp3buf_size) {
  return lame_encode_buffer_template(gfp, pcm_l, pcm_r, nsamples, mp3buf,
                                     mp3buf_size, pcm_short_type, 1, 1.0);
}

int lame_encode_buffer_float(lame_global_flags *gfp, const float pcm_l[],
                             const float pcm_r[], const int nsamples,
                             unsigned char *mp3buf, const int mp3buf_size) {
  /* input is assumed to be normalized to +/- 32768 for full scale */
  return lame_encode_buffer_template(gfp, pcm_l, pcm_r, nsamples, mp3buf,
                                     mp3buf_size, pcm_float_type, 1, 1.0);
}

int lame_encode_buffer_ieee_float(lame_t gfp, const float pcm_l[],
                                  const float pcm_r[], const int nsamples,
                                  unsigned char *mp3buf,
                                  const int mp3buf_size) {
  /* input is assumed to be normalized to +/- 1.0 for full scale */
  return lame_encode_buffer_template(gfp, pcm_l, pcm_r, nsamples, mp3buf,
                                     mp3buf_size, pcm_float_type, 1, 32767.0);
}

int lame_encode_buffer_interleaved_ieee_float(lame_t gfp, const float pcm[],
                                              const int nsamples,
                                              unsigned char *mp3buf,
                                              const int mp3buf_size) {
  /* input is assumed to be normalized to +/- 1.0 for full scale */
  return lame_encode_buffer_template(gfp, pcm, pcm + 1, nsamples, mp3buf,
                                     mp3buf_size, pcm_float_type, 2, 32767.0);
}

int lame_encode_buffer_ieee_double(lame_t gfp, const double pcm_l[],
                                   const double pcm_r[], const int nsamples,
                                   unsigned char *mp3buf,
                                   const int mp3buf_size) {
  /* input is assumed to be normalized to +/- 1.0 for full scale */
  return lame_encode_buffer_template(gfp, pcm_l, pcm_r, nsamples, mp3buf,
                                     mp3buf_size, pcm_double_type, 1, 32767.0);
}

int lame_encode_buffer_interleaved_ieee_double(lame_t gfp, const double pcm[],
                                               const int nsamples,
                                               unsigned char *mp3buf,
                                               const int mp3buf_size) {
  /* input is assumed to be normalized to +/- 1.0 for full scale */
  return lame_encode_buffer_template(gfp, pcm, pcm + 1, nsamples, mp3buf,
                                     mp3buf_size, pcm_double_type, 2, 32767.0);
}

int lame_encode_buffer_int(lame_global_flags *gfp, const int pcm_l[],
                           const int pcm_r[], const int nsamples,
                           unsigned char *mp3buf, const int mp3buf_size) {
  /* input is assumed to be normalized to +/- MAX_INT for full scale */
  FLOAT const norm = (1.0 / (1L << (8 * sizeof(int) - 16)));
  return lame_encode_buffer_template(gfp, pcm_l, pcm_r, nsamples, mp3buf,
                                     mp3buf_size, pcm_int_type, 1, norm);
}

int lame_encode_buffer_long2(lame_global_flags *gfp, const long pcm_l[],
                             const long pcm_r[], const int nsamples,
                             unsigned char *mp3buf, const int mp3buf_size) {
  /* input is assumed to be normalized to +/- MAX_LONG for full scale */
  FLOAT const norm = (1.0 / (1L << (8 * sizeof(long) - 16)));
  return lame_encode_buffer_template(gfp, pcm_l, pcm_r, nsamples, mp3buf,
                                     mp3buf_size, pcm_long_type, 1, norm);
}

int lame_encode_buffer_long(lame_global_flags *gfp, const long pcm_l[],
                            const long pcm_r[], const int nsamples,
                            unsigned char *mp3buf, const int mp3buf_size) {
  /* input is assumed to be normalized to +/- 32768 for full scale */
  return lame_encode_buffer_template(gfp, pcm_l, pcm_r, nsamples, mp3buf,
                                     mp3buf_size, pcm_long_type, 1, 1.0);
}

int lame_encode_buffer_interleaved(lame_global_flags *gfp, short int pcm[],
                                   int nsamples, unsigned char *mp3buf,
                                   int mp3buf_size) {
  /* input is assumed to be normalized to +/- MAX_SHORT for full scale */
  return lame_encode_buffer_template(gfp, pcm, pcm + 1, nsamples, mp3buf,
                                     mp3buf_size, pcm_short_type, 2, 1.0);
}

int lame_encode_buffer_interleaved_int(lame_t gfp, const int pcm[],
                                       const int nsamples,
                                       unsigned char *mp3buf,
                                       const int mp3buf_size) {
  /* input is assumed to be normalized to +/- MAX(int) for full scale */
  FLOAT const norm = (1.0 / (1L << (8 * sizeof(int) - 16)));
  return lame_encode_buffer_template(gfp, pcm, pcm + 1, nsamples, mp3buf,
                                     mp3buf_size, pcm_int_type, 2, norm);
}
