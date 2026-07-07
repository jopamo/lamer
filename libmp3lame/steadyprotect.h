#ifndef LAME_STEADYPROTECT_H
#define LAME_STEADYPROTECT_H

#include "util.h"

#define LAME_STEADY_TONAL_PROTECT_MODE_OFF 0
#define LAME_STEADY_TONAL_PROTECT_MODE_METRIC 1
#define LAME_STEADY_TONAL_PROTECT_MODE_RETRY_REJECT 2
#define LAME_STEADY_TONAL_PROTECT_MODE_ACCEPT 3

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
    FLOAT   max_loss_db;
    FLOAT   loss_db_sum;
    FLOAT   max_failure_db;
    FLOAT   failure_db_sum;
} steady_tonal_candidate_t;

typedef struct {
    FLOAT   max_noise;
    FLOAT   over_noise;
    int     over_count;
    FLOAT   max_loss_db;
    FLOAT   loss_db_sum;
    FLOAT   max_failure_db;
    FLOAT   failure_db_sum;
} steady_tonal_failure_t;

typedef struct {
    FLOAT   tighten_db;
    FLOAT   tighten_factor;
} steady_tonal_profile_t;

int
steady_tonal_protect_mode(void);

void
steady_tonal_stats_init_if_requested(lame_internal_flags *gfc);

void
steady_tonal_stats_free(lame_internal_flags *gfc);

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

void
steady_tonal_selected_failure(lame_internal_flags const *gfc,
                              gr_info const *cod_info,
                              FLOAT const *distort,
                              unsigned int sfb_mask,
                              steady_tonal_failure_t *failure);

void
steady_tonal_stats_note_frame(steady_tonal_stats_t *stats);

void
steady_tonal_stats_note_granule(steady_tonal_stats_t *stats,
                                gr_info const *cod_info);

void
steady_tonal_stats_note_candidate(steady_tonal_stats_t *stats,
                                  steady_tonal_candidate_t const *candidate,
                                  steady_tonal_failure_t const *before);

void
steady_tonal_stats_note_retry(steady_tonal_stats_t *stats);

void
steady_tonal_stats_note_reject(steady_tonal_stats_t *stats);

void
steady_tonal_stats_note_accept(steady_tonal_stats_t *stats,
                               int frame,
                               gr_info const *cod_info,
                               int gr, int ch, int best_bits,
                               steady_tonal_candidate_t const *candidate,
                               steady_tonal_failure_t const *before,
                               steady_tonal_failure_t const *after);

void
steady_tonal_dump_stats_if_requested(lame_internal_flags const *gfc);

int
steady_tonal_profile_count(void);

steady_tonal_profile_t const *
steady_tonal_profile_get(int profile_index);

#endif
