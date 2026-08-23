/*
 * Private helpers shared by the public encoder lifecycle modules.
 */

#ifndef LAME_INTERNAL_H
#define LAME_INTERNAL_H

#include <assert.h>

#include "encoder.h"
#include "util.h"

static inline int lame_calc_needed(SessionConfig_t const *cfg) {
  int const pcm_samples_per_frame = 576 * cfg->mode_gr;
  int mf_needed;

  /* Keep these invariants close to the buffer-size calculation. */
#if ENCDELAY < MDCTDELAY
#error ENCDELAY is less than MDCTDELAY, see encoder.h
#endif
#if FFTOFFSET > BLKSIZE
#error FFTOFFSET is greater than BLKSIZE, see encoder.h
#endif

  mf_needed = BLKSIZE + pcm_samples_per_frame - FFTOFFSET;
  mf_needed = Max(mf_needed, 512 + pcm_samples_per_frame - 32);

  assert(MFSIZE >= mf_needed);
  return mf_needed;
}

#endif /* LAME_INTERNAL_H */
