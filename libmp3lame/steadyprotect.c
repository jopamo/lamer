#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <string.h>

#include "lame.h"
#include "machine.h"
#include "encoder.h"
#include "util.h"
#include "steadyprotect.h"

#ifndef LAME_ENABLE_SAFE_STEADY_TONAL_PROTECT_DEFAULT
#define LAME_ENABLE_SAFE_STEADY_TONAL_PROTECT_DEFAULT 1
#endif

#ifndef STEADY_TONAL_TRANSIENT_SCORE_CEIL
#define STEADY_TONAL_TRANSIENT_SCORE_CEIL 1.5f
#endif
#ifndef STEADY_TONAL_PART23_FLOOR
#define STEADY_TONAL_PART23_FLOOR 1400
#endif
#ifndef STEADY_TONAL_TONALITY_FLOOR
#define STEADY_TONAL_TONALITY_FLOOR 0.55f
#endif
#ifndef STEADY_TONAL_STABILITY_FLOOR
#define STEADY_TONAL_STABILITY_FLOOR 0.70f
#endif
#ifndef STEADY_TONAL_ATH_MARGIN_DB_FLOOR
#define STEADY_TONAL_ATH_MARGIN_DB_FLOOR 8.0f
#endif
#ifndef STEADY_TONAL_NOISE_DB_FLOOR
#define STEADY_TONAL_NOISE_DB_FLOOR 0.25f
#endif
#ifndef STEADY_TONAL_MIN_HZ
#define STEADY_TONAL_MIN_HZ 300.0f
#endif
#ifndef STEADY_TONAL_MAX_HZ
#define STEADY_TONAL_MAX_HZ 8000.0f
#endif
#ifndef STEADY_TONAL_MIN_BAND_COUNT
#define STEADY_TONAL_MIN_BAND_COUNT 2
#endif

static steady_tonal_profile_t const steady_tonal_profiles[] = {
    { -0.75f, 0.84139514165f },
    { -1.50f, 0.70794578438f }
};

static FLOAT
steady_tonal_sfb_center_hz(lame_internal_flags const *gfc, int sfb)
{
    SessionConfig_t const *const cfg = &gfc->cfg;
    FLOAT const start = (FLOAT) gfc->scalefac_band.l[sfb];
    FLOAT const end = (FLOAT) gfc->scalefac_band.l[sfb + 1];
    return (start + end) * 0.5f * ((FLOAT) cfg->samplerate_out / 1152.0f);
}

int
safe_steady_tonal_protect_allowed(lame_internal_flags const *gfc)
{
#if !LAME_ENABLE_SAFE_STEADY_TONAL_PROTECT_DEFAULT
    (void) gfc;
    return 0;
#else
    SessionConfig_t const *const cfg = &gfc->cfg;

    if (cfg->vbr != vbr_mtrh) {
        return 0;
    }
    if (cfg->free_format) {
        return 0;
    }
    if (cfg->ATHonly || cfg->noATH) {
        return 0;
    }
    if (cfg->short_blocks != short_block_allowed
        && cfg->short_blocks != short_block_coupled) {
        return 0;
    }
    if (cfg->samplerate_out < 22050) {
        return 0;
    }
    return 1;
#endif
}

int
steady_tonal_candidate_select(lame_internal_flags const *gfc,
                              gr_info const *cod_info,
                              int gr, int psy_ch,
                              FLOAT const *distort,
                              steady_tonal_candidate_t *candidate,
                              char const **reject_reason)
{
    PsyStateVar_t const *const psv = &gfc->sv_psy;
    FLOAT tonality_sum = 0.0f;
    FLOAT ath_margin_sum = 0.0f;
    int sfb;

    memset(candidate, 0, sizeof(*candidate));
    candidate->first_sfb = -1;
    candidate->last_sfb = -1;
    candidate->min_stability = 1.0f;
    candidate->max_noise = -20.0f;
    *reject_reason = "eligible";

    if (cod_info->block_type == SHORT_TYPE) {
        *reject_reason = "short_type";
        return 0;
    }
    if (cod_info->part2_3_length < STEADY_TONAL_PART23_FLOOR) {
        *reject_reason = "low_part23";
        return 0;
    }
    if (psv->short_mask_final_mask[gr][psy_ch] != 0
        || psv->short_mask_score_rel[gr][psy_ch] >= STEADY_TONAL_TRANSIENT_SCORE_CEIL) {
        *reject_reason = "recent_transient";
        return 0;
    }

    for (sfb = 0; sfb < cod_info->psy_lmax && sfb < SBMAX_l; ++sfb) {
        FLOAT const hz = steady_tonal_sfb_center_hz(gfc, sfb);
        FLOAT const energy = psv->steady_band_energy[gr][psy_ch][sfb];
        FLOAT const tonality = psv->steady_band_tonality[gr][psy_ch][sfb];
        FLOAT const stability = psv->steady_band_stability[gr][psy_ch][sfb];
        FLOAT const ath = gfc->ATH->l[sfb];
        FLOAT const ath_margin_db = FAST_LOG10(Max(energy / Max(ath, 1E-20f), 1E-20f));
        FLOAT const noise_db = FAST_LOG10(Max(distort[sfb], 1E-20f));

        if (hz < STEADY_TONAL_MIN_HZ || hz > STEADY_TONAL_MAX_HZ) {
            continue;
        }
        if (!cod_info->energy_above_cutoff[sfb]) {
            continue;
        }
        if (tonality < STEADY_TONAL_TONALITY_FLOOR) {
            continue;
        }
        if (stability < STEADY_TONAL_STABILITY_FLOOR) {
            continue;
        }
        if (ath_margin_db < STEADY_TONAL_ATH_MARGIN_DB_FLOOR) {
            continue;
        }
        if (noise_db < STEADY_TONAL_NOISE_DB_FLOOR) {
            continue;
        }

        candidate->sfb_mask |= (1u << sfb);
        candidate->sfb_count++;
        if (candidate->first_sfb < 0) {
            candidate->first_sfb = sfb;
        }
        candidate->last_sfb = sfb;
        tonality_sum += tonality;
        ath_margin_sum += ath_margin_db;
        candidate->min_stability = Min(candidate->min_stability, stability);
        if (noise_db > 0.0f) {
            candidate->over_noise += noise_db;
            candidate->over_count++;
        }
        candidate->max_noise = Max(candidate->max_noise, noise_db);
    }

    if (candidate->sfb_count < STEADY_TONAL_MIN_BAND_COUNT) {
        *reject_reason = candidate->sfb_count > 0 ? "too_few_bands" : "no_selected_bands";
        return 0;
    }

    candidate->mean_tonality = tonality_sum / candidate->sfb_count;
    candidate->mean_ath_margin_db = ath_margin_sum / candidate->sfb_count;
    return 1;
}

void
steady_tonal_build_xmin(FLOAT *dst, FLOAT const *src,
                        gr_info const *cod_info,
                        steady_tonal_candidate_t const *candidate,
                        steady_tonal_profile_t const *profile,
                        int *tightened_bands)
{
    int sfb;

    memcpy(dst, src, sizeof(FLOAT) * SFBMAX);
    *tightened_bands = 0;

    for (sfb = 0; sfb < cod_info->psy_lmax && sfb < SBMAX_l; ++sfb) {
        if ((candidate->sfb_mask & (1u << sfb)) == 0) {
            continue;
        }
        dst[sfb] *= profile->tighten_factor;
        (*tightened_bands)++;
    }
}

int
steady_tonal_profile_count(void)
{
    return (int) dimension_of(steady_tonal_profiles);
}

steady_tonal_profile_t const *
steady_tonal_profile_get(int profile_index)
{
    if (profile_index < 0 || profile_index >= (int) dimension_of(steady_tonal_profiles)) {
        return 0;
    }
    return &steady_tonal_profiles[profile_index];
}
