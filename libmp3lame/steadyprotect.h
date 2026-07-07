#ifndef LAME_STEADYPROTECT_H
#define LAME_STEADYPROTECT_H

#include "util.h"

typedef struct {
    unsigned int sfb_mask;
    int     sfb_count;
    int     first_sfb;
    int     last_sfb;
    FLOAT   mean_tonality;
    FLOAT   min_stability;
    FLOAT   mean_ath_margin_db;
    FLOAT   max_noise;
    FLOAT   over_noise;
    int     over_count;
} steady_tonal_candidate_t;

typedef struct {
    FLOAT   tighten_db;
    FLOAT   tighten_factor;
} steady_tonal_profile_t;

int
safe_steady_tonal_protect_allowed(lame_internal_flags const *gfc);

int
steady_tonal_candidate_select(lame_internal_flags const *gfc,
                              gr_info const *cod_info,
                              int gr, int psy_ch,
                              FLOAT const *distort,
                              steady_tonal_candidate_t *candidate,
                              char const **reject_reason);

void
steady_tonal_build_xmin(FLOAT *dst, FLOAT const *src,
                        gr_info const *cod_info,
                        steady_tonal_candidate_t const *candidate,
                        steady_tonal_profile_t const *profile,
                        int *tightened_bands);

int
steady_tonal_profile_count(void);

steady_tonal_profile_t const *
steady_tonal_profile_get(int profile_index);

#endif
