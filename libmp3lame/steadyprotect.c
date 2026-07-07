#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lame.h"
#include "machine.h"
#include "encoder.h"
#include "util.h"
#include "quantize_pvt.h"
#include "steadyprotect.h"

#ifndef LAME_ENABLE_SAFE_STEADY_TONAL_PROTECT_DEFAULT
#define LAME_ENABLE_SAFE_STEADY_TONAL_PROTECT_DEFAULT 1
#endif
#ifndef LAME_STEADY_TONAL_PROTECT_MODE
# if LAME_ENABLE_SAFE_STEADY_TONAL_PROTECT_DEFAULT
#  define LAME_STEADY_TONAL_PROTECT_MODE LAME_STEADY_TONAL_PROTECT_MODE_ACCEPT
# else
#  define LAME_STEADY_TONAL_PROTECT_MODE LAME_STEADY_TONAL_PROTECT_MODE_OFF
# endif
#endif

#ifndef STEADY_TONAL_TRANSIENT_SCORE_CEIL
#define STEADY_TONAL_TRANSIENT_SCORE_CEIL 1.5f
#endif
#ifndef STEADY_TONAL_TONALITY_FLOOR
#define STEADY_TONAL_TONALITY_FLOOR 0.45f
#endif
#ifndef STEADY_TONAL_STABILITY_FLOOR
#define STEADY_TONAL_STABILITY_FLOOR 0.60f
#endif
#ifndef STEADY_TONAL_ATH_MARGIN_DB_FLOOR
#define STEADY_TONAL_ATH_MARGIN_DB_FLOOR 6.0f
#endif
#ifndef STEADY_TONAL_NOISE_DB_FLOOR
#define STEADY_TONAL_NOISE_DB_FLOOR 0.10f
#endif
#ifndef STEADY_TONAL_LOSS_OK_DB
#define STEADY_TONAL_LOSS_OK_DB 0.75f
#endif
#ifndef STEADY_TONAL_BAND_FAILURE_DB_FLOOR
#define STEADY_TONAL_BAND_FAILURE_DB_FLOOR 1.00f
#endif
#ifndef STEADY_TONAL_CANDIDATE_FAILURE_DB_FLOOR
#define STEADY_TONAL_CANDIDATE_FAILURE_DB_FLOOR 1.50f
#endif
#ifndef STEADY_TONAL_MIN_HZ
#define STEADY_TONAL_MIN_HZ 300.0f
#endif
#ifndef STEADY_TONAL_MAX_HZ
#define STEADY_TONAL_MAX_HZ 8000.0f
#endif
#ifndef STEADY_TONAL_MIN_BAND_COUNT
#define STEADY_TONAL_MIN_BAND_COUNT 1
#endif

static steady_tonal_profile_t const steady_tonal_profiles[] = {
    { -0.75f, 0.84139514165f },
    { -1.50f, 0.70794578438f }
};

int
steady_tonal_protect_mode(void)
{
    return LAME_STEADY_TONAL_PROTECT_MODE;
}

static FLOAT
steady_tonal_sfb_center_hz(lame_internal_flags const *gfc, int sfb)
{
    SessionConfig_t const *const cfg = &gfc->cfg;
    FLOAT const start = (FLOAT) gfc->scalefac_band.l[sfb];
    FLOAT const end = (FLOAT) gfc->scalefac_band.l[sfb + 1];
    return (start + end) * 0.5f * ((FLOAT) cfg->samplerate_out / 1152.0f);
}

static void
steady_tonal_band_metric(gr_info const *cod_info, int sfb, int start, FLOAT distort_sfb,
                         FLOAT *noise_db, FLOAT *loss_db, FLOAT *failure_db)
{
    int const width = cod_info->width[sfb];
    int const *const scalefac = cod_info->scalefac;
    int const *const ix = cod_info->l3_enc;
    FLOAT const step =
        POW20(cod_info->global_gain
              - (((scalefac[sfb] + (cod_info->preflag ? pretab[sfb] : 0))
                  << (cod_info->scalefac_scale + 1)))
              - cod_info->subblock_gain[cod_info->window[sfb]] * 8);
    FLOAT source_energy = 0.0f;
    FLOAT quant_energy = 0.0f;
    int i;

    for (i = 0; i < width; ++i) {
        FLOAT const orig = fabsf(cod_info->xr[start + i]);
        FLOAT const recon = pow43[ix[start + i]] * step;

        source_energy += orig * orig;
        quant_energy += recon * recon;
    }

    *noise_db = Max(FAST_LOG10(Max(distort_sfb, 1E-20f)), 0.0f);
    *loss_db = 0.0f;
    if (source_energy > 1E-20f && quant_energy < source_energy) {
        *loss_db = FAST_LOG10(Max(source_energy / Max(quant_energy, 1E-20f), 1E-20f));
        if (*loss_db < 0.0f) {
            *loss_db = 0.0f;
        }
    }
    *failure_db = Max(*loss_db - STEADY_TONAL_LOSS_OK_DB, 0.0f) + *noise_db;
}

int
safe_steady_tonal_protect_allowed(lame_internal_flags const *gfc)
{
#if LAME_STEADY_TONAL_PROTECT_MODE == LAME_STEADY_TONAL_PROTECT_MODE_OFF
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

static void
steady_tonal_note_mask(unsigned long long by_sfb[SBMAX_l], unsigned int sfb_mask)
{
    int sfb;

    for (sfb = 0; sfb < SBMAX_l; ++sfb) {
        if ((sfb_mask & (1u << sfb)) != 0) {
            by_sfb[sfb]++;
        }
    }
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
    int sfb, j = 0;

    memset(candidate, 0, sizeof(*candidate));
    candidate->first_sfb = -1;
    candidate->last_sfb = -1;
    candidate->min_stability = 1.0f;
    candidate->max_noise = -20.0f;
    candidate->max_loss_db = 0.0f;
    candidate->max_failure_db = 0.0f;
    *reject_reason = "eligible";

    if (cod_info->block_type == SHORT_TYPE) {
        *reject_reason = "short_type";
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
        FLOAT noise_db = 0.0f;
        FLOAT loss_db = 0.0f;
        FLOAT failure_db = 0.0f;

        steady_tonal_band_metric(cod_info, sfb, j, distort[sfb],
                                 &noise_db, &loss_db, &failure_db);

        if (hz < STEADY_TONAL_MIN_HZ || hz > STEADY_TONAL_MAX_HZ) {
            j += cod_info->width[sfb];
            continue;
        }
        if (!cod_info->energy_above_cutoff[sfb]) {
            j += cod_info->width[sfb];
            continue;
        }
        if (tonality < STEADY_TONAL_TONALITY_FLOOR) {
            j += cod_info->width[sfb];
            continue;
        }
        if (stability < STEADY_TONAL_STABILITY_FLOOR) {
            j += cod_info->width[sfb];
            continue;
        }
        if (ath_margin_db < STEADY_TONAL_ATH_MARGIN_DB_FLOOR) {
            j += cod_info->width[sfb];
            continue;
        }
        if (noise_db < STEADY_TONAL_NOISE_DB_FLOOR
            && loss_db <= STEADY_TONAL_LOSS_OK_DB) {
            j += cod_info->width[sfb];
            continue;
        }
        if (failure_db < STEADY_TONAL_BAND_FAILURE_DB_FLOOR) {
            j += cod_info->width[sfb];
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
        candidate->loss_db_sum += loss_db;
        candidate->max_loss_db = Max(candidate->max_loss_db, loss_db);
        candidate->failure_db_sum += failure_db;
        candidate->max_failure_db = Max(candidate->max_failure_db, failure_db);
        j += cod_info->width[sfb];
    }

    if (candidate->sfb_count < STEADY_TONAL_MIN_BAND_COUNT) {
        *reject_reason = "no_failure_bands";
        return 0;
    }

    if (candidate->failure_db_sum < STEADY_TONAL_CANDIDATE_FAILURE_DB_FLOOR) {
        *reject_reason = "weak_failure";
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

void
steady_tonal_selected_failure(gr_info const *cod_info,
                              FLOAT const *distort,
                              unsigned int sfb_mask,
                              steady_tonal_failure_t *failure)
{
    int sfb, j = 0;

    memset(failure, 0, sizeof(*failure));
    failure->max_noise = -20.0f;

    for (sfb = 0; sfb < cod_info->psy_lmax && sfb < SBMAX_l; ++sfb) {
        if ((sfb_mask & (1u << sfb)) != 0) {
            FLOAT noise_db = 0.0f;
            FLOAT loss_db = 0.0f;
            FLOAT failure_db = 0.0f;

            steady_tonal_band_metric(cod_info, sfb, j, distort[sfb],
                                     &noise_db, &loss_db, &failure_db);
            if (noise_db > 0.0f) {
                failure->over_noise += noise_db;
                failure->over_count++;
            }
            failure->max_noise = Max(failure->max_noise, noise_db);
            failure->loss_db_sum += loss_db;
            failure->max_loss_db = Max(failure->max_loss_db, loss_db);
            failure->failure_db_sum += failure_db;
            failure->max_failure_db = Max(failure->max_failure_db, failure_db);
        }
        j += cod_info->width[sfb];
    }
}

void
steady_tonal_stats_note_frame(lame_internal_flags *gfc)
{
    gfc->steady_tonal_stats.frames_seen++;
}

void
steady_tonal_stats_note_granule(lame_internal_flags *gfc,
                                gr_info const *cod_info)
{
    gfc->steady_tonal_stats.granules_seen++;
    if (cod_info->block_type != SHORT_TYPE) {
        gfc->steady_tonal_stats.long_granules_seen++;
    }
}

void
steady_tonal_stats_note_candidate(lame_internal_flags *gfc,
                                  steady_tonal_candidate_t const *candidate,
                                  steady_tonal_failure_t const *before)
{
    steady_tonal_stats_t *const stats = &gfc->steady_tonal_stats;

    stats->candidate_granules++;
    stats->selected_bands_total += candidate->sfb_count;
    stats->failure_before_sum += before->failure_db_sum;
    stats->max_failure_before = Max(stats->max_failure_before,
                                    (double) before->max_failure_db);
    steady_tonal_note_mask(stats->candidate_by_sfb, candidate->sfb_mask);
}

void
steady_tonal_stats_note_retry(lame_internal_flags *gfc)
{
    gfc->steady_tonal_stats.retry_granules++;
}

void
steady_tonal_stats_note_reject(lame_internal_flags *gfc)
{
    gfc->steady_tonal_stats.reject_granules++;
}

void
steady_tonal_stats_note_accept(lame_internal_flags *gfc,
                               gr_info const *cod_info,
                               int gr, int ch, int best_bits,
                               steady_tonal_candidate_t const *candidate,
                               steady_tonal_failure_t const *before,
                               steady_tonal_failure_t const *after)
{
    steady_tonal_stats_t *const stats = &gfc->steady_tonal_stats;

    stats->accept_granules++;
    stats->accepted_selected_bands_total += candidate->sfb_count;
    stats->failure_after_sum += after->failure_db_sum;
    stats->max_failure_after = Max(stats->max_failure_after,
                                   (double) after->max_failure_db);
    steady_tonal_note_mask(stats->accept_by_sfb, candidate->sfb_mask);

    if (stats->commit_event_count < STEADY_TONAL_COMMIT_EVENT_CAP) {
        steady_tonal_commit_event_t *const ev =
            &stats->commit_events[stats->commit_event_count++];
        ev->frame = gfc->ov_enc.frame_number;
        ev->gr = gr;
        ev->ch = ch;
        ev->block_type = cod_info->block_type;
        ev->selected_bands = candidate->sfb_count;
        ev->best_bits = best_bits;
        ev->failure_before_sum = before->failure_db_sum;
        ev->failure_after_sum = after->failure_db_sum;
        ev->max_before = before->max_failure_db;
        ev->max_after = after->max_failure_db;
        ev->selected_sfb_mask_lo = candidate->sfb_mask;
        ev->selected_sfb_mask_hi = 0;
    }
    else {
        stats->commit_event_overflow++;
    }
}

void
steady_tonal_dump_stats_if_requested(lame_internal_flags const *gfc)
{
    char const *const env = getenv("LAME_STEADY_DEBUG");
    steady_tonal_stats_t const *const stats = &gfc->steady_tonal_stats;
    unsigned int i;

    if (env == 0 || *env == '\0') {
        return;
    }

    fprintf(stderr,
            "steady_tonal_stats mode=%d cfg_vbr=%d cfg_short_blocks=%d cfg_free_format=%d "
            "cfg_ATHonly=%d cfg_noATH=%d cfg_samplerate_out=%d "
            "allowed_now=%d "
            "frames_seen=%llu granules_seen=%llu "
            "long_granules_seen=%llu candidate_granules=%llu retry_granules=%llu "
            "accept_granules=%llu reject_granules=%llu selected_bands_total=%llu "
            "accepted_selected_bands_total=%llu failure_before_sum=%.2f "
            "failure_after_sum=%.2f max_failure_before=%.2f max_failure_after=%.2f "
            "commit_event_count=%u commit_event_overflow=%u\n",
            steady_tonal_protect_mode(),
            gfc->cfg.vbr, gfc->cfg.short_blocks, gfc->cfg.free_format,
            gfc->cfg.ATHonly, gfc->cfg.noATH, gfc->cfg.samplerate_out,
            safe_steady_tonal_protect_allowed(gfc),
            stats->frames_seen, stats->granules_seen,
            stats->long_granules_seen, stats->candidate_granules,
            stats->retry_granules, stats->accept_granules,
            stats->reject_granules, stats->selected_bands_total,
            stats->accepted_selected_bands_total,
            stats->failure_before_sum, stats->failure_after_sum,
            stats->max_failure_before, stats->max_failure_after,
            stats->commit_event_count, stats->commit_event_overflow);

    for (i = 0; i < stats->commit_event_count; ++i) {
        steady_tonal_commit_event_t const *const ev = &stats->commit_events[i];
        fprintf(stderr,
                "steady_tonal_commit_event index=%u frame=%d gr=%d ch=%d block_type=%d "
                "selected_bands=%d best_bits=%d failure_before_sum=%.2f "
                "failure_after_sum=%.2f max_before=%.2f max_after=%.2f "
                "selected_sfb_mask_lo=0x%08x selected_sfb_mask_hi=0x%08x\n",
                i, ev->frame, ev->gr, ev->ch, ev->block_type,
                ev->selected_bands, ev->best_bits,
                (double) ev->failure_before_sum,
                (double) ev->failure_after_sum,
                (double) ev->max_before,
                (double) ev->max_after,
                ev->selected_sfb_mask_lo, ev->selected_sfb_mask_hi);
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
