/*
 *	MP3 quantization
 *
 *	Copyright (c) 1999-2000 Mark Taylor
 *	Copyright (c) 2000-2012 Robert Hegemann
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/* $Id$ */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdio.h>

#include "lame.h"
#include "machine.h"
#include "encoder.h"
#include "util.h"
#include "vbrquantize.h"
#include "quantize_pvt.h"
#include "steadyprotect.h"




struct algo_s;
typedef struct algo_s algo_t;

typedef void (*alloc_sf_f) (const algo_t *, const int *, const int *, int);
typedef uint8_t (*find_sf_f) (const algo_t *, const FLOAT *, const FLOAT *, FLOAT, unsigned int, uint8_t);

struct algo_s {
    alloc_sf_f alloc;
    find_sf_f  find;
    const FLOAT *xr34orig;
    lame_internal_flags *gfc;
    gr_info *cod_info;
    int     mingain_l;
    int     mingain_s[3];
};

#ifndef SHORT_MASK_RELAX_FACTOR
#define SHORT_MASK_RELAX_FACTOR 1.5f
#endif
#ifndef SHORT_MASK_RELAX_DB
#define SHORT_MASK_RELAX_DB 1.7609126f
#endif
#ifndef SHORT_MASK_RELAX_BIT_SLOP
#define SHORT_MASK_RELAX_BIT_SLOP 8
#endif
#ifndef SHORT_REDIST_PART23_FLOOR
#define SHORT_REDIST_PART23_FLOOR 3400
#endif
#ifndef SHORT_REDIST_SCORE_FLOOR
#define SHORT_REDIST_SCORE_FLOOR 2.0f
#endif
#ifndef SHORT_REDIST_IMPULSE_RATIO_FLOOR
#define SHORT_REDIST_IMPULSE_RATIO_FLOOR 2.0f
#endif
#ifndef SHORT_REDIST_TAIL_RATIO_CEIL
#define SHORT_REDIST_TAIL_RATIO_CEIL 0.85f
#endif
#ifndef SHORT_SAFE_REDIST_SCORE_FLOOR
#define SHORT_SAFE_REDIST_SCORE_FLOOR 2.5f
#endif
#ifndef SHORT_SAFE_REDIST_IMPULSE_RATIO_FLOOR
#define SHORT_SAFE_REDIST_IMPULSE_RATIO_FLOOR 3.0f
#endif
#ifndef SHORT_SAFE_REDIST_TAIL_RATIO_CEIL
#define SHORT_SAFE_REDIST_TAIL_RATIO_CEIL 0.65f
#endif
#ifndef SHORT_REDIST_ATTACK_MAX_NOISE_FLOOR
#define SHORT_REDIST_ATTACK_MAX_NOISE_FLOOR 1.20f
#endif
#ifndef SHORT_REDIST_CATASTROPHE_DB
#define SHORT_REDIST_CATASTROPHE_DB 6.0f
#endif
#ifndef SHORT_REDIST_OVERCOUNT_EXPLODE
#define SHORT_REDIST_OVERCOUNT_EXPLODE 8
#endif
#ifndef SHORT_SAFE_REDIST_GLOBAL_MAX_NOISE_SLOP
#define SHORT_SAFE_REDIST_GLOBAL_MAX_NOISE_SLOP 1.0f
#endif
#ifndef SHORT_SAFE_REDIST_OVERCOUNT_SLOP
#define SHORT_SAFE_REDIST_OVERCOUNT_SLOP 2
#endif
#ifndef SHORT_SAFE_REDIST_PROFILE_MASK
#define SHORT_SAFE_REDIST_PROFILE_MASK ((1u << 0) | (1u << 2))
#endif
#ifndef SHORT_EXPERIMENTAL_REDIST_PROFILE_MASK
#define SHORT_EXPERIMENTAL_REDIST_PROFILE_MASK ((1u << 0) | (1u << 1) | (1u << 2) | (1u << 3))
#endif
#ifndef STEADY_TONAL_GLOBAL_MAX_NOISE_SLOP
#define STEADY_TONAL_GLOBAL_MAX_NOISE_SLOP 1.0f
#endif
#ifndef STEADY_TONAL_OVERCOUNT_SLOP
#define STEADY_TONAL_OVERCOUNT_SLOP 2
#endif

typedef struct {
    FLOAT   max_noise;
    FLOAT   over_noise;
    int     over_count;
} short_window_noise_t;

typedef struct {
    int     attack_max_short_band;
    FLOAT   attack_tighten_db;
    FLOAT   attack_tighten_factor;
    int     nonattack_max_short_band;
    FLOAT   nonattack_relax_db;
    FLOAT   nonattack_relax_factor;
    int     high_min_short_band;
    FLOAT   high_relax_db;
    FLOAT   high_relax_factor;
} short_redist_profile_t;

static short_redist_profile_t const short_redist_profiles[] = {
    { 10, -4.0f, 0.39810717055f, 12, 1.25f, 1.33352143216f, 11, 2.0f, 1.58489319246f },
    { 10, -6.0f, 0.25118864315f, 12, 0.75f, 1.18850222744f, 11, 1.25f, 1.33352143216f },
    { 10, -5.0f, 0.31622776602f, 12, 0.0f, 1.0f,            12, 0.75f, 1.18850222744f },
    {  9, -5.0f, 0.31622776602f, 10, 0.5f, 1.12201845430f, 10, 4.0f, 2.51188643151f }
};

typedef struct {
    char const *trace_prefix;
    char const *attack_profile_name;
    FLOAT   score_floor;
    FLOAT   impulse_ratio_floor;
    FLOAT   tail_ratio_ceil;
    int     part23_floor;
    FLOAT   attack_max_noise_floor;
    FLOAT   global_max_noise_slop;
    int     over_count_slop;
    FLOAT   attack_max_noise_accept_slop;
    unsigned int profile_mask;
} short_redist_mode_t;

static short_redist_mode_t const short_redist_mode_experimental = {
    "short_target_redist", "multi",
    SHORT_REDIST_SCORE_FLOOR,
    SHORT_REDIST_IMPULSE_RATIO_FLOOR,
    SHORT_REDIST_TAIL_RATIO_CEIL,
    SHORT_REDIST_PART23_FLOOR,
    SHORT_REDIST_ATTACK_MAX_NOISE_FLOOR,
    SHORT_REDIST_CATASTROPHE_DB,
    SHORT_REDIST_OVERCOUNT_EXPLODE,
    0.20f,
    SHORT_EXPERIMENTAL_REDIST_PROFILE_MASK
};

static short_redist_mode_t const short_redist_mode_safe = {
    "short_safe_redist", "safe",
    SHORT_SAFE_REDIST_SCORE_FLOOR,
    SHORT_SAFE_REDIST_IMPULSE_RATIO_FLOOR,
    SHORT_SAFE_REDIST_TAIL_RATIO_CEIL,
    SHORT_REDIST_PART23_FLOOR,
    SHORT_REDIST_ATTACK_MAX_NOISE_FLOOR,
    SHORT_SAFE_REDIST_GLOBAL_MAX_NOISE_SLOP,
    SHORT_SAFE_REDIST_OVERCOUNT_SLOP,
    0.0f,
    SHORT_SAFE_REDIST_PROFILE_MASK
};

static int block_sf(algo_t * that, const FLOAT l3_xmin[SFBMAX],
                    int vbrsf[SFBMAX], int vbrsfmin[SFBMAX]);
static void bitcount(const algo_t * that);
static int quantizeAndCountBits(const algo_t * that);
static void cutDistribution(const int sfwork[SFBMAX], int sf_out[SFBMAX], int cut);
static void outOfBitsStrategy(algo_t const* that, const int sfwork[SFBMAX],
                              const int vbrsfmin[SFBMAX], int target);
static int reduce_bit_usage(lame_internal_flags * gfc, int gr, int ch);

static int
short_mask_relax_psy_ch(lame_internal_flags const *gfc, int ch)
{
    if (gfc->cfg.mode == JOINT_STEREO && gfc->ov_enc.mode_ext == MPG_MD_MS_LR) {
        return ch + 2;
    }
    return ch;
}

static char const *
short_mask_relax_source_name(int psy_ch)
{
    return psy_ch < 2 ? "LR" : "MS";
}

static void
short_transient_window_noise(gr_info const *cod_info, FLOAT const *distort,
                             int attack_win, short_window_noise_t *res)
{
    int sfb;

    res->max_noise = -20.0f;
    res->over_noise = 0.0f;
    res->over_count = 0;

    for (sfb = cod_info->sfb_lmax; sfb < cod_info->psymax; ++sfb) {
        int const short_band =
            cod_info->sfb_smin + (sfb - cod_info->sfb_lmax) / 3;

        if (cod_info->window[sfb] != attack_win) {
            continue;
        }
        if (short_band < 3 || short_band > 10) {
            continue;
        }

        {
            FLOAT const noise = FAST_LOG10(Max(distort[sfb], 1E-20f));
            if (noise > 0.0f) {
                res->over_noise += noise;
                res->over_count++;
            }
            res->max_noise = Max(res->max_noise, noise);
        }
    }
}

static void
short_transient_build_redistributed_xmin(FLOAT *redis_xmin,
                                         FLOAT const *orig_xmin,
                                         gr_info const *cod_info,
                                         int attack_win,
                                         short_redist_profile_t const *profile,
                                         int *tightened_bands,
                                         int *relaxed_bands)
{
    int sfb;

    memcpy(redis_xmin, orig_xmin, sizeof(FLOAT) * SFBMAX);
    *tightened_bands = 0;
    *relaxed_bands = 0;

    for (sfb = cod_info->sfb_lmax; sfb < cod_info->psymax; ++sfb) {
        int const short_band =
            cod_info->sfb_smin + (sfb - cod_info->sfb_lmax) / 3;

        if (short_band < 3) {
            continue;
        }

        if (cod_info->window[sfb] == attack_win
            && short_band <= profile->attack_max_short_band) {
            redis_xmin[sfb] *= profile->attack_tighten_factor;
            (*tightened_bands)++;
            continue;
        }

        if (cod_info->window[sfb] != attack_win
            && short_band >= profile->high_min_short_band) {
            redis_xmin[sfb] *= profile->high_relax_factor;
            (*relaxed_bands)++;
            continue;
        }

        if (cod_info->window[sfb] != attack_win
            && short_band <= profile->nonattack_max_short_band) {
            redis_xmin[sfb] *= profile->nonattack_relax_factor;
            (*relaxed_bands)++;
        }
    }
}

static int
short_redist_profile_better(short_window_noise_t const *candidate, int candidate_bits,
                            short_window_noise_t const *best, int best_bits)
{
    if (candidate->over_noise < best->over_noise) {
        return 1;
    }
    if (candidate->over_noise > best->over_noise) {
        return 0;
    }
    if (candidate->max_noise < best->max_noise) {
        return 1;
    }
    if (candidate->max_noise > best->max_noise) {
        return 0;
    }
    return candidate_bits < best_bits;
}

static int
steady_tonal_profile_better(steady_tonal_failure_t const *candidate, int candidate_bits,
                            steady_tonal_failure_t const *best, int best_bits)
{
    if (candidate->failure_db_sum < best->failure_db_sum) {
        return 1;
    }
    if (candidate->failure_db_sum > best->failure_db_sum) {
        return 0;
    }
    if (candidate->max_failure_db < best->max_failure_db) {
        return 1;
    }
    if (candidate->max_failure_db > best->max_failure_db) {
        return 0;
    }
    return candidate_bits < best_bits;
}

static int
short_redist_profile_enabled(short_redist_mode_t const *mode, int profile_index)
{
    return (mode->profile_mask & (1u << profile_index)) != 0;
}

static int
short_redist_profiles_available(short_redist_mode_t const *mode)
{
    int profile_index;
    int count = 0;

    for (profile_index = 0;
         profile_index < (int) dimension_of(short_redist_profiles);
         ++profile_index) {
        if (short_redist_profile_enabled(mode, profile_index)) {
            ++count;
        }
    }
    return count;
}

static int
short_safe_transient_redistribute_allowed(lame_internal_flags const *gfc)
{
    SessionConfig_t const *const cfg = &gfc->cfg;
    PsyConst_t const *const psy = gfc->cd_psy;

    if (psy == 0 || !psy->safe_short_transient_redistribute) {
        return 0;
    }
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
}

static void
short_transient_redistribute_retry(lame_internal_flags *gfc,
                                   algo_t that_[2][2],
                                   const FLOAT l3_xmin[2][2][SFBMAX],
                                   int sfwork_[2][2][SFBMAX],
                                   int vbrsfmin_[2][2][SFBMAX],
                                   int max_nbits_ch[2][2],
                                   int ngr, int nch,
                                   int use_nbits_ch[2][2],
                                   int use_nbits_gr[2],
                                   int *use_nbits_fr,
                                   int max_nbits_fr,
                                   short_redist_mode_t const *mode)
{
    SessionConfig_t const *const cfg = &gfc->cfg;
    PsyStateVar_t const *const psv = &gfc->sv_psy;
    int const profiles_available = short_redist_profiles_available(mode);
    int gr2, ch2;

    for (gr2 = 0; gr2 < ngr; ++gr2) {
        for (ch2 = 0; ch2 < nch; ++ch2) {
            algo_t *that = &that_[gr2][ch2];
            gr_info *cod_info = that->cod_info;
            int const psy_ch = short_mask_relax_psy_ch(gfc, ch2);
            FLOAT const score_rel = psv->short_mask_score_rel[gr2][psy_ch];
            FLOAT const impulse_ratio =
                psv->short_mask_impulse_ratio[gr2][psy_ch];
            FLOAT const tail_ratio =
                psv->short_mask_tail_ratio[gr2][psy_ch];
            int const final_mask = psv->short_mask_final_mask[gr2][psy_ch];
            int const pos = psv->short_mask_pos[gr2][psy_ch];
            int const old_bits = cod_info->part2_3_length;
            int const attack_win =
                short_transient_attack_win(final_mask, pos);
            FLOAT old_distort[SFBMAX];
            short_window_noise_t old_attack_noise;
            calc_noise_result noise_orig;
            int candidate = 1;
            char const *candidate_reason = "eligible";

            if (cod_info->block_type != SHORT_TYPE) {
                continue;
            }

            calc_noise(cod_info, l3_xmin[gr2][ch2], old_distort, &noise_orig, 0);
            short_transient_window_noise(cod_info, old_distort,
                                         attack_win, &old_attack_noise);

            if (final_mask == 0) {
                candidate = 0;
                candidate_reason = "no_final_mask";
            }
            else if (score_rel < mode->score_floor) {
                candidate = 0;
                candidate_reason = "weak_short";
            }
            else if (impulse_ratio < mode->impulse_ratio_floor) {
                candidate = 0;
                candidate_reason = "low_impulse";
            }
            else if (tail_ratio > mode->tail_ratio_ceil) {
                candidate = 0;
                candidate_reason = "long_tail";
            }
            else if (old_bits < mode->part23_floor) {
                candidate = 0;
                candidate_reason = "low_part23";
            }
            else if (old_attack_noise.max_noise < mode->attack_max_noise_floor) {
                candidate = 0;
                candidate_reason = "low_attack_noise";
            }

            if (cfg->analysis) {
                fprintf(stderr,
                        "%s_candidate=%d frame=%d gr=%d ch=%d psy_ch=%d source=%s "
                        "%s_reject_reason=%s attack_win=%d score_rel=%.2f final_mask=0x%02x "
                        "impulse_ratio=%.2f tail_ratio=%.2f attack_profile=%s "
                        "%s_profiles_available=%d "
                        "old_bits=%d old_attack_max_noise=%.2f old_attack_over_noise=%.2f "
                        "old_attack_over_count=%d old_global_max_noise=%.2f old_over_count=%d\n",
                        mode->trace_prefix, candidate,
                        gfc->ov_enc.frame_number, gr2, ch2, psy_ch,
                        short_mask_relax_source_name(psy_ch),
                        mode->trace_prefix, candidate_reason,
                        attack_win, (double) score_rel, final_mask,
                        (double) impulse_ratio, (double) tail_ratio,
                        mode->attack_profile_name,
                        mode->trace_prefix, profiles_available,
                        old_bits, (double) old_attack_noise.max_noise,
                        (double) old_attack_noise.over_noise,
                        old_attack_noise.over_count,
                        (double) noise_orig.max_noise,
                        noise_orig.over_count);
            }

            if (!candidate) {
                continue;
            }

            {
                FLOAT redis_xmin[SFBMAX];
                FLOAT new_distort[SFBMAX];
                FLOAT restore_distort[SFBMAX];
                gr_info saved_gi = *cod_info;
                gr_info best_gi = *cod_info;
                int saved_sfwork[SFBMAX];
                int best_sfwork[SFBMAX];
                int saved_vbrsfmin[SFBMAX];
                int best_vbrsfmin[SFBMAX];
                int saved_scfsi[2][4];
                int best_scfsi[2][4];
                int profile_index;
                int old_nbits = use_nbits_ch[gr2][ch2];
                int accept = 0;
                int rollback_ok = 1;
                int profiles_tried = 0;
                int best_profile = -1;
                int best_nbits = old_nbits;
                int best_bits = old_bits;
                int best_tightened_bands = 0;
                int best_relaxed_bands = 0;
                int have_legal_profile = 0;
                char const *reject_reason = "no_legal_profile";
                calc_noise_result best_noise_orig = noise_orig;
                calc_noise_result noise_restore_orig;
                short_window_noise_t best_attack_noise = old_attack_noise;

                memcpy(saved_sfwork, sfwork_[gr2][ch2], sizeof(saved_sfwork));
                memcpy(saved_vbrsfmin, vbrsfmin_[gr2][ch2],
                       sizeof(saved_vbrsfmin));
                memcpy(saved_scfsi, gfc->l3_side.scfsi,
                       sizeof(saved_scfsi));

                for (profile_index = 0;
                     profile_index < (int) dimension_of(short_redist_profiles);
                     ++profile_index) {
                    short_redist_profile_t const *profile;
                    calc_noise_result noise_retry_orig;
                    short_window_noise_t new_attack_noise;
                    int tightened_bands = 0;
                    int relaxed_bands = 0;
                    int new_bits;
                    int new_nbits = old_nbits;
                    int legal_profile = 0;
                    int trial_rollback_ok;
                    char const *profile_reject_reason = "legal";

                    if (!short_redist_profile_enabled(mode, profile_index)) {
                        continue;
                    }

                    profile = &short_redist_profiles[profile_index];
                    ++profiles_tried;
                    *cod_info = saved_gi;
                    memcpy(sfwork_[gr2][ch2], saved_sfwork,
                           sizeof(saved_sfwork));
                    memcpy(vbrsfmin_[gr2][ch2], saved_vbrsfmin,
                           sizeof(saved_vbrsfmin));
                    memcpy(gfc->l3_side.scfsi, saved_scfsi,
                           sizeof(saved_scfsi));

                    short_transient_build_redistributed_xmin(redis_xmin,
                                                             l3_xmin[gr2][ch2],
                                                             cod_info,
                                                             attack_win,
                                                             profile,
                                                             &tightened_bands,
                                                             &relaxed_bands);

                    {
                        int vbrmax;
                        int *sfwork = sfwork_[gr2][ch2];
                        int *vbrsfmin2 = vbrsfmin_[gr2][ch2];

                        vbrmax = block_sf(that, redis_xmin, sfwork, vbrsfmin2);
                        that->alloc(that, sfwork, vbrsfmin2, vbrmax);
                        bitcount(that);
                        cutDistribution(sfwork, sfwork,
                                        that->cod_info->global_gain);
                        outOfBitsStrategy(that, sfwork, vbrsfmin2,
                                          max_nbits_ch[gr2][ch2]);
                        memset(&cod_info->l3_enc[0], 0, sizeof(cod_info->l3_enc));
                        (void) quantizeAndCountBits(that);
                    }

                    calc_noise(cod_info, l3_xmin[gr2][ch2], new_distort,
                               &noise_retry_orig, 0);
                    short_transient_window_noise(cod_info, new_distort,
                                                 attack_win, &new_attack_noise);

                    new_bits = cod_info->part2_3_length;

                    if (new_bits > MAX_BITS_PER_CHANNEL) {
                        profile_reject_reason = "bit_limit";
                    }
                    else if (noise_retry_orig.max_noise >
                             noise_orig.max_noise + mode->global_max_noise_slop) {
                        profile_reject_reason = "global_max_noise";
                    }
                    else if (noise_retry_orig.over_count >
                             noise_orig.over_count + mode->over_count_slop) {
                        profile_reject_reason = "over_count";
                    }
                    else {
                        new_nbits = reduce_bit_usage(gfc, gr2, ch2);
                        if (new_nbits > max_nbits_ch[gr2][ch2]) {
                            profile_reject_reason = "channel_budget";
                        }
                        else if (*use_nbits_fr + (new_nbits - old_nbits) >
                                 max_nbits_fr) {
                            profile_reject_reason = "frame_budget";
                        }
                        else {
                            legal_profile = 1;
                        }
                    }

                    if (legal_profile
                        && (!have_legal_profile
                            || short_redist_profile_better(&new_attack_noise, new_bits,
                                                           &best_attack_noise, best_bits))) {
                        have_legal_profile = 1;
                        best_profile = profile_index;
                        best_gi = *cod_info;
                        memcpy(best_sfwork, sfwork_[gr2][ch2],
                               sizeof(best_sfwork));
                        memcpy(best_vbrsfmin, vbrsfmin_[gr2][ch2],
                               sizeof(best_vbrsfmin));
                        memcpy(best_scfsi, gfc->l3_side.scfsi,
                               sizeof(best_scfsi));
                        best_nbits = new_nbits;
                        best_bits = new_bits;
                        best_tightened_bands = tightened_bands;
                        best_relaxed_bands = relaxed_bands;
                        best_attack_noise = new_attack_noise;
                        best_noise_orig = noise_retry_orig;
                    }

                    *cod_info = saved_gi;
                    memcpy(sfwork_[gr2][ch2], saved_sfwork,
                           sizeof(saved_sfwork));
                    memcpy(vbrsfmin_[gr2][ch2], saved_vbrsfmin,
                           sizeof(saved_vbrsfmin));
                    memcpy(gfc->l3_side.scfsi, saved_scfsi,
                           sizeof(saved_scfsi));
                    calc_noise(cod_info, l3_xmin[gr2][ch2], restore_distort,
                               &noise_restore_orig, 0);
                    trial_rollback_ok =
                        cod_info->part2_3_length == old_bits
                        && noise_restore_orig.over_count == noise_orig.over_count
                        && fabsf(noise_restore_orig.max_noise - noise_orig.max_noise) < 1e-6f
                        && fabsf(noise_restore_orig.over_noise - noise_orig.over_noise) < 1e-6f
                        && memcmp(gfc->l3_side.scfsi, saved_scfsi,
                                  sizeof(saved_scfsi)) == 0;

                    if (cfg->analysis) {
                        fprintf(stderr,
                                "%s_profile_try=1 frame=%d gr=%d ch=%d psy_ch=%d source=%s "
                                "%s_profile=%d %s_profile_legal=%d "
                                "attack_win=%d old_bits=%d new_bits=%d "
                                "impulse_ratio=%.2f tail_ratio=%.2f "
                                "attack_tighten_db=%.2f nonattack_relax_db=%.2f high_relax_db=%.2f "
                                "old_attack_max_noise=%.2f new_attack_max_noise=%.2f "
                                "old_attack_over_noise=%.2f new_attack_over_noise=%.2f "
                                "old_attack_over_count=%d new_attack_over_count=%d "
                                "old_global_max_noise=%.2f new_global_max_noise=%.2f "
                                "old_over_count=%d new_over_count=%d "
                                "tightened_bands=%d relaxed_bands=%d "
                                "%s_reject_reason=%s rollback_ok=%d\n",
                                mode->trace_prefix, gfc->ov_enc.frame_number,
                                gr2, ch2, psy_ch,
                                short_mask_relax_source_name(psy_ch),
                                mode->trace_prefix, profile_index,
                                mode->trace_prefix, legal_profile,
                                attack_win, old_bits, new_bits,
                                (double) impulse_ratio, (double) tail_ratio,
                                (double) profile->attack_tighten_db,
                                (double) profile->nonattack_relax_db,
                                (double) profile->high_relax_db,
                                (double) old_attack_noise.max_noise,
                                (double) new_attack_noise.max_noise,
                                (double) old_attack_noise.over_noise,
                                (double) new_attack_noise.over_noise,
                                old_attack_noise.over_count,
                                new_attack_noise.over_count,
                                (double) noise_orig.max_noise,
                                (double) noise_retry_orig.max_noise,
                                noise_orig.over_count,
                                noise_retry_orig.over_count,
                                tightened_bands, relaxed_bands,
                                mode->trace_prefix, profile_reject_reason,
                                trial_rollback_ok);
                    }

                    if (!trial_rollback_ok) {
                        rollback_ok = 0;
                        reject_reason = "rollback_failed";
                        break;
                    }
                }

                if (rollback_ok && have_legal_profile) {
                    int const attack_over_improved =
                        best_attack_noise.over_noise < old_attack_noise.over_noise;
                    int const attack_max_ok =
                        best_attack_noise.max_noise
                        <= old_attack_noise.max_noise + mode->attack_max_noise_accept_slop;
                    int const global_max_ok =
                        best_noise_orig.max_noise
                        <= noise_orig.max_noise + mode->global_max_noise_slop;
                    int const over_count_ok =
                        best_noise_orig.over_count
                        <= noise_orig.over_count + mode->over_count_slop;

                    accept = best_bits <= MAX_BITS_PER_CHANNEL
                        && attack_over_improved
                        && attack_max_ok
                        && global_max_ok
                        && over_count_ok;

                    if (accept) {
                        reject_reason = "accepted";
                    }
                    else if (!attack_over_improved) {
                        reject_reason = "attack_over_noise";
                    }
                    else if (!attack_max_ok) {
                        reject_reason = "attack_max_noise";
                    }
                    else if (!global_max_ok) {
                        reject_reason = "global_max_noise";
                    }
                    else {
                        reject_reason = "over_count";
                    }
                }

                if (accept) {
                    *cod_info = best_gi;
                    memcpy(sfwork_[gr2][ch2], best_sfwork,
                           sizeof(best_sfwork));
                    memcpy(vbrsfmin_[gr2][ch2], best_vbrsfmin,
                           sizeof(best_vbrsfmin));
                    memcpy(gfc->l3_side.scfsi, best_scfsi,
                           sizeof(best_scfsi));
                    use_nbits_ch[gr2][ch2] = best_nbits;
                    use_nbits_gr[gr2] += (best_nbits - old_nbits);
                    *use_nbits_fr += (best_nbits - old_nbits);
                }
                else {
                    *cod_info = saved_gi;
                    memcpy(sfwork_[gr2][ch2], saved_sfwork,
                           sizeof(saved_sfwork));
                    memcpy(vbrsfmin_[gr2][ch2], saved_vbrsfmin,
                           sizeof(saved_vbrsfmin));
                    memcpy(gfc->l3_side.scfsi, saved_scfsi,
                           sizeof(saved_scfsi));
                }

                if (cfg->analysis) {
                    int summary_profile = have_legal_profile ? best_profile : -1;
                    int summary_bits = have_legal_profile ? best_bits : old_bits;
                    int summary_tightened_bands =
                        have_legal_profile ? best_tightened_bands : 0;
                    int summary_relaxed_bands =
                        have_legal_profile ? best_relaxed_bands : 0;
                    short_window_noise_t const *summary_attack_noise =
                        have_legal_profile ? &best_attack_noise : &old_attack_noise;
                    calc_noise_result const *summary_noise_orig =
                        have_legal_profile ? &best_noise_orig : &noise_orig;

                    fprintf(stderr,
                            "%s_retry=1 %s_profiles_tried=%d "
                            "%s_best_profile=%d %s_accept=%d "
                            "frame=%d gr=%d ch=%d psy_ch=%d source=%s "
                            "attack_win=%d old_bits=%d new_bits=%d "
                            "impulse_ratio=%.2f tail_ratio=%.2f attack_profile=%s "
                            "old_attack_max_noise=%.2f new_attack_max_noise=%.2f "
                            "old_attack_over_noise=%.2f new_attack_over_noise=%.2f "
                            "old_attack_over_count=%d new_attack_over_count=%d "
                            "old_global_max_noise=%.2f new_global_max_noise=%.2f "
                            "old_over_count=%d new_over_count=%d "
                            "tightened_bands=%d relaxed_bands=%d "
                            "%s_reject_reason=%s rollback_ok=%d\n",
                            mode->trace_prefix,
                            mode->trace_prefix, profiles_tried,
                            mode->trace_prefix, summary_profile,
                            mode->trace_prefix, accept,
                            gfc->ov_enc.frame_number, gr2, ch2, psy_ch,
                            short_mask_relax_source_name(psy_ch), attack_win,
                            old_bits, summary_bits,
                            (double) impulse_ratio, (double) tail_ratio,
                            mode->attack_profile_name,
                            (double) old_attack_noise.max_noise,
                            (double) summary_attack_noise->max_noise,
                            (double) old_attack_noise.over_noise,
                            (double) summary_attack_noise->over_noise,
                            old_attack_noise.over_count,
                            summary_attack_noise->over_count,
                            (double) noise_orig.max_noise,
                            (double) summary_noise_orig->max_noise,
                            noise_orig.over_count,
                            summary_noise_orig->over_count,
                            summary_tightened_bands, summary_relaxed_bands,
                            mode->trace_prefix, reject_reason, rollback_ok);
                }
            }
        }
    }
}

static void
steady_tonal_protect_retry(lame_internal_flags *gfc,
                           algo_t that_[2][2],
                           const FLOAT l3_xmin[2][2][SFBMAX],
                           int sfwork_[2][2][SFBMAX],
                           int vbrsfmin_[2][2][SFBMAX],
                           int max_nbits_ch[2][2],
                           int ngr, int nch,
                           int use_nbits_ch[2][2],
                           int use_nbits_gr[2],
                           int *use_nbits_fr,
                           int max_nbits_fr)
{
    SessionConfig_t const *const cfg = &gfc->cfg;
    steady_tonal_stats_t *const steady_stats = gfc->steady_tonal_stats;
    int const steady_mode = steady_tonal_protect_mode();
    int const profiles_available = steady_tonal_profile_count();
    int gr2, ch2;

    if (steady_stats != 0) {
        steady_tonal_stats_note_frame(steady_stats);
    }
    for (gr2 = 0; gr2 < ngr; ++gr2) {
        for (ch2 = 0; ch2 < nch; ++ch2) {
            algo_t *that = &that_[gr2][ch2];
            gr_info *cod_info = that->cod_info;
            int const psy_ch = short_mask_relax_psy_ch(gfc, ch2);
            int const old_bits = cod_info->part2_3_length;
            FLOAT old_distort[SFBMAX];
            steady_tonal_candidate_t candidate_info;
            steady_tonal_failure_t old_selected_failure;
            calc_noise_result noise_orig;
            char const *candidate_reason = "eligible";
            int candidate;

            if (steady_stats != 0) {
                steady_tonal_stats_note_granule(steady_stats, cod_info);
            }
            if (cod_info->block_type == SHORT_TYPE) {
                continue;
            }

            calc_noise(cod_info, l3_xmin[gr2][ch2], old_distort, &noise_orig, 0);
            candidate = steady_tonal_candidate_select(gfc, cod_info, gr2, psy_ch,
                                                      old_distort,
                                                      &candidate_info,
                                                      &candidate_reason);
            steady_tonal_selected_failure(gfc, cod_info, old_distort,
                                          candidate_info.sfb_mask,
                                          &old_selected_failure);

            if (cfg->analysis) {
                fprintf(stderr,
                        "steady_tonal_protect_candidate=%d frame=%d gr=%d ch=%d psy_ch=%d source=%s "
                        "steady_tonal_protect_reject_reason=%s "
                        "steady_tonal_protect_profiles_available=%d "
                        "selected_sfb_count=%d selected_sfb_mask=0x%08x "
                        "first_sfb=%d last_sfb=%d "
                        "mean_tonality=%.3f min_stability=%.3f mean_ath_margin_db=%.2f "
                        "old_bits=%d "
                        "old_selected_max_noise=%.2f old_selected_over_noise=%.2f old_selected_over_count=%d "
                        "old_selected_max_loss_db=%.2f old_selected_loss_db_sum=%.2f "
                        "old_selected_max_failure_db=%.2f old_selected_failure_db_sum=%.2f "
                        "old_global_max_noise=%.2f old_over_count=%d\n",
                        candidate,
                        gfc->ov_enc.frame_number, gr2, ch2, psy_ch,
                        short_mask_relax_source_name(psy_ch),
                        candidate_reason, profiles_available,
                        candidate_info.sfb_count, candidate_info.sfb_mask,
                        candidate_info.first_sfb, candidate_info.last_sfb,
                        (double) candidate_info.mean_tonality,
                        (double) candidate_info.min_stability,
                        (double) candidate_info.mean_ath_margin_db,
                        old_bits,
                        (double) old_selected_failure.max_noise,
                        (double) old_selected_failure.over_noise,
                        old_selected_failure.over_count,
                        (double) old_selected_failure.max_loss_db,
                        (double) old_selected_failure.loss_db_sum,
                        (double) old_selected_failure.max_failure_db,
                        (double) old_selected_failure.failure_db_sum,
                        (double) noise_orig.max_noise,
                        noise_orig.over_count);
            }

            if (!candidate) {
                continue;
            }
            if (steady_stats != 0) {
                steady_tonal_stats_note_candidate(steady_stats, &candidate_info,
                                                  &old_selected_failure);
            }
            if (steady_mode == LAME_STEADY_TONAL_PROTECT_MODE_METRIC) {
                if (steady_stats != 0) {
                    steady_tonal_stats_note_reject(steady_stats);
                }
                continue;
            }

            {
                FLOAT redis_xmin[SFBMAX];
                FLOAT new_distort[SFBMAX];
                FLOAT restore_distort[SFBMAX];
                gr_info saved_gi = *cod_info;
                gr_info best_gi = *cod_info;
                int saved_sfwork[SFBMAX];
                int best_sfwork[SFBMAX];
                int saved_vbrsfmin[SFBMAX];
                int best_vbrsfmin[SFBMAX];
                int saved_scfsi[2][4];
                int best_scfsi[2][4];
                int profile_index;
                int old_nbits = use_nbits_ch[gr2][ch2];
                int accept = 0;
                int rollback_ok = 1;
                int profiles_tried = 0;
                int best_profile = -1;
                int best_nbits = old_nbits;
                int best_bits = old_bits;
                int best_tightened_bands = 0;
                int have_legal_profile = 0;
                char const *reject_reason = "no_legal_profile";
                calc_noise_result best_noise_orig = noise_orig;
                calc_noise_result noise_restore_orig;
                steady_tonal_failure_t best_selected_failure = old_selected_failure;
                steady_tonal_failure_t restore_selected_failure;

                memcpy(saved_sfwork, sfwork_[gr2][ch2], sizeof(saved_sfwork));
                memcpy(saved_vbrsfmin, vbrsfmin_[gr2][ch2],
                       sizeof(saved_vbrsfmin));
                memcpy(saved_scfsi, gfc->l3_side.scfsi,
                       sizeof(saved_scfsi));
                if (steady_stats != 0) {
                    steady_tonal_stats_note_retry(steady_stats);
                }

                for (profile_index = 0;
                     profile_index < profiles_available;
                     ++profile_index) {
                    steady_tonal_profile_t const *profile =
                        steady_tonal_profile_get(profile_index);
                    calc_noise_result noise_retry_orig;
                    steady_tonal_failure_t new_selected_failure;
                    int tightened_bands = 0;
                    int new_bits;
                    int new_nbits = old_nbits;
                    int legal_profile = 0;
                    int trial_rollback_ok;
                    char const *profile_reject_reason = "legal";

                    if (profile == 0) {
                        continue;
                    }

                    ++profiles_tried;
                    *cod_info = saved_gi;
                    memcpy(sfwork_[gr2][ch2], saved_sfwork,
                           sizeof(saved_sfwork));
                    memcpy(vbrsfmin_[gr2][ch2], saved_vbrsfmin,
                           sizeof(saved_vbrsfmin));
                    memcpy(gfc->l3_side.scfsi, saved_scfsi,
                           sizeof(saved_scfsi));

                    steady_tonal_build_xmin(redis_xmin, l3_xmin[gr2][ch2],
                                            cod_info, &candidate_info, profile,
                                            &tightened_bands);

                    {
                        int vbrmax;
                        int *sfwork = sfwork_[gr2][ch2];
                        int *vbrsfmin2 = vbrsfmin_[gr2][ch2];

                        vbrmax = block_sf(that, redis_xmin, sfwork, vbrsfmin2);
                        that->alloc(that, sfwork, vbrsfmin2, vbrmax);
                        bitcount(that);
                        cutDistribution(sfwork, sfwork,
                                        that->cod_info->global_gain);
                        outOfBitsStrategy(that, sfwork, vbrsfmin2,
                                          max_nbits_ch[gr2][ch2]);
                        memset(&cod_info->l3_enc[0], 0, sizeof(cod_info->l3_enc));
                        (void) quantizeAndCountBits(that);
                    }

                    calc_noise(cod_info, l3_xmin[gr2][ch2], new_distort,
                               &noise_retry_orig, 0);
                    steady_tonal_selected_failure(gfc, cod_info, new_distort,
                                                  candidate_info.sfb_mask,
                                                  &new_selected_failure);

                    new_bits = cod_info->part2_3_length;

                    if (new_bits > MAX_BITS_PER_CHANNEL) {
                        profile_reject_reason = "bit_limit";
                    }
                    else if (noise_retry_orig.max_noise >
                             noise_orig.max_noise + STEADY_TONAL_GLOBAL_MAX_NOISE_SLOP) {
                        profile_reject_reason = "global_max_noise";
                    }
                    else if (noise_retry_orig.over_count >
                             noise_orig.over_count + STEADY_TONAL_OVERCOUNT_SLOP) {
                        profile_reject_reason = "over_count";
                    }
                    else {
                        new_nbits = reduce_bit_usage(gfc, gr2, ch2);
                        if (new_nbits > max_nbits_ch[gr2][ch2]) {
                            profile_reject_reason = "channel_budget";
                        }
                        else if (*use_nbits_fr + (new_nbits - old_nbits) >
                                 max_nbits_fr) {
                            profile_reject_reason = "frame_budget";
                        }
                        else {
                            legal_profile = 1;
                        }
                    }

                    if (legal_profile
                        && (!have_legal_profile
                            || steady_tonal_profile_better(&new_selected_failure, new_bits,
                                                           &best_selected_failure, best_bits))) {
                        have_legal_profile = 1;
                        best_profile = profile_index;
                        best_gi = *cod_info;
                        memcpy(best_sfwork, sfwork_[gr2][ch2],
                               sizeof(best_sfwork));
                        memcpy(best_vbrsfmin, vbrsfmin_[gr2][ch2],
                               sizeof(best_vbrsfmin));
                        memcpy(best_scfsi, gfc->l3_side.scfsi,
                               sizeof(best_scfsi));
                        best_nbits = new_nbits;
                        best_bits = new_bits;
                        best_tightened_bands = tightened_bands;
                        best_selected_failure = new_selected_failure;
                        best_noise_orig = noise_retry_orig;
                    }

                    *cod_info = saved_gi;
                    memcpy(sfwork_[gr2][ch2], saved_sfwork,
                           sizeof(saved_sfwork));
                    memcpy(vbrsfmin_[gr2][ch2], saved_vbrsfmin,
                           sizeof(saved_vbrsfmin));
                    memcpy(gfc->l3_side.scfsi, saved_scfsi,
                           sizeof(saved_scfsi));
                    calc_noise(cod_info, l3_xmin[gr2][ch2], restore_distort,
                               &noise_restore_orig, 0);
                    steady_tonal_selected_failure(gfc, cod_info, restore_distort,
                                                  candidate_info.sfb_mask,
                                                  &restore_selected_failure);
                    trial_rollback_ok =
                        cod_info->part2_3_length == old_bits
                        && noise_restore_orig.over_count == noise_orig.over_count
                        && fabsf(noise_restore_orig.max_noise - noise_orig.max_noise) < 1e-6f
                        && fabsf(noise_restore_orig.over_noise - noise_orig.over_noise) < 1e-6f
                        && restore_selected_failure.over_count == old_selected_failure.over_count
                        && fabsf(restore_selected_failure.max_noise - old_selected_failure.max_noise) < 1e-6f
                        && fabsf(restore_selected_failure.over_noise - old_selected_failure.over_noise) < 1e-6f
                        && fabsf(restore_selected_failure.max_loss_db - old_selected_failure.max_loss_db) < 1e-6f
                        && fabsf(restore_selected_failure.loss_db_sum - old_selected_failure.loss_db_sum) < 1e-6f
                        && fabsf(restore_selected_failure.max_failure_db - old_selected_failure.max_failure_db) < 1e-6f
                        && fabsf(restore_selected_failure.failure_db_sum - old_selected_failure.failure_db_sum) < 1e-6f
                        && memcmp(gfc->l3_side.scfsi, saved_scfsi,
                                  sizeof(saved_scfsi)) == 0;

                    if (cfg->analysis) {
                        fprintf(stderr,
                                "steady_tonal_protect_profile_try=1 frame=%d gr=%d ch=%d psy_ch=%d source=%s "
                                "steady_tonal_protect_profile=%d steady_tonal_protect_profile_legal=%d "
                                "old_bits=%d new_bits=%d tighten_db=%.2f "
                                "selected_sfb_count=%d selected_sfb_mask=0x%08x "
                                "old_selected_max_noise=%.2f new_selected_max_noise=%.2f "
                                "old_selected_over_noise=%.2f new_selected_over_noise=%.2f "
                                "old_selected_over_count=%d new_selected_over_count=%d "
                                "old_selected_max_loss_db=%.2f new_selected_max_loss_db=%.2f "
                                "old_selected_loss_db_sum=%.2f new_selected_loss_db_sum=%.2f "
                                "old_selected_max_failure_db=%.2f new_selected_max_failure_db=%.2f "
                                "old_selected_failure_db_sum=%.2f new_selected_failure_db_sum=%.2f "
                                "old_global_max_noise=%.2f new_global_max_noise=%.2f "
                                "old_over_count=%d new_over_count=%d "
                                "tightened_bands=%d "
                                "steady_tonal_protect_reject_reason=%s rollback_ok=%d\n",
                                gfc->ov_enc.frame_number,
                                gr2, ch2, psy_ch,
                                short_mask_relax_source_name(psy_ch),
                                profile_index, legal_profile,
                                old_bits, new_bits,
                                (double) profile->tighten_db,
                                candidate_info.sfb_count, candidate_info.sfb_mask,
                                (double) old_selected_failure.max_noise,
                                (double) new_selected_failure.max_noise,
                                (double) old_selected_failure.over_noise,
                                (double) new_selected_failure.over_noise,
                                old_selected_failure.over_count,
                                new_selected_failure.over_count,
                                (double) old_selected_failure.max_loss_db,
                                (double) new_selected_failure.max_loss_db,
                                (double) old_selected_failure.loss_db_sum,
                                (double) new_selected_failure.loss_db_sum,
                                (double) old_selected_failure.max_failure_db,
                                (double) new_selected_failure.max_failure_db,
                                (double) old_selected_failure.failure_db_sum,
                                (double) new_selected_failure.failure_db_sum,
                                (double) noise_orig.max_noise,
                                (double) noise_retry_orig.max_noise,
                                noise_orig.over_count,
                                noise_retry_orig.over_count,
                                tightened_bands,
                                profile_reject_reason,
                                trial_rollback_ok);
                    }

                    if (!trial_rollback_ok) {
                        rollback_ok = 0;
                        reject_reason = "rollback_failed";
                        break;
                    }
                }

                if (rollback_ok && have_legal_profile) {
                    int const selected_failure_improved =
                        best_selected_failure.failure_db_sum
                        < old_selected_failure.failure_db_sum;
                    int const selected_max_failure_ok =
                        best_selected_failure.max_failure_db
                        <= old_selected_failure.max_failure_db;
                    int const global_max_ok =
                        best_noise_orig.max_noise
                        <= noise_orig.max_noise + STEADY_TONAL_GLOBAL_MAX_NOISE_SLOP;
                    int const over_count_ok =
                        best_noise_orig.over_count
                        <= noise_orig.over_count + STEADY_TONAL_OVERCOUNT_SLOP;

                    accept = best_bits <= MAX_BITS_PER_CHANNEL
                        && selected_failure_improved
                        && selected_max_failure_ok
                        && global_max_ok
                        && over_count_ok;

                    if (accept
                        && steady_mode == LAME_STEADY_TONAL_PROTECT_MODE_RETRY_REJECT) {
                        accept = 0;
                        reject_reason = "mode_retry_reject";
                    }
                    else
                    if (accept) {
                        reject_reason = "accepted";
                    }
                    else if (!selected_failure_improved) {
                        reject_reason = "selected_failure";
                    }
                    else if (!selected_max_failure_ok) {
                        reject_reason = "selected_max_failure";
                    }
                    else if (!global_max_ok) {
                        reject_reason = "global_max_noise";
                    }
                    else {
                        reject_reason = "over_count";
                    }
                }

                if (accept) {
                    *cod_info = best_gi;
                    memcpy(sfwork_[gr2][ch2], best_sfwork,
                           sizeof(best_sfwork));
                    memcpy(vbrsfmin_[gr2][ch2], best_vbrsfmin,
                           sizeof(best_vbrsfmin));
                    memcpy(gfc->l3_side.scfsi, best_scfsi,
                           sizeof(best_scfsi));
                    use_nbits_ch[gr2][ch2] = best_nbits;
                    use_nbits_gr[gr2] += (best_nbits - old_nbits);
                    *use_nbits_fr += (best_nbits - old_nbits);
                    if (steady_stats != 0) {
                        steady_tonal_stats_note_accept(steady_stats,
                                                       gfc->ov_enc.frame_number,
                                                       cod_info, gr2, ch2,
                                                       best_bits,
                                                       &candidate_info,
                                                       &old_selected_failure,
                                                       &best_selected_failure);
                    }
                }
                else {
                    *cod_info = saved_gi;
                    memcpy(sfwork_[gr2][ch2], saved_sfwork,
                           sizeof(saved_sfwork));
                    memcpy(vbrsfmin_[gr2][ch2], saved_vbrsfmin,
                           sizeof(saved_vbrsfmin));
                    memcpy(gfc->l3_side.scfsi, saved_scfsi,
                           sizeof(saved_scfsi));
                    if (steady_stats != 0) {
                        steady_tonal_stats_note_reject(steady_stats);
                    }
                }

                if (cfg->analysis) {
                    int summary_profile = have_legal_profile ? best_profile : -1;
                    int summary_bits = have_legal_profile ? best_bits : old_bits;
                    int summary_tightened_bands =
                        have_legal_profile ? best_tightened_bands : 0;
                    steady_tonal_failure_t const *summary_selected_failure =
                        have_legal_profile ? &best_selected_failure : &old_selected_failure;
                    calc_noise_result const *summary_noise_orig =
                        have_legal_profile ? &best_noise_orig : &noise_orig;

                    fprintf(stderr,
                            "steady_tonal_protect_retry=1 steady_tonal_protect_profiles_tried=%d "
                            "steady_tonal_protect_best_profile=%d steady_tonal_protect_accept=%d "
                            "frame=%d gr=%d ch=%d psy_ch=%d source=%s "
                            "selected_sfb_count=%d selected_sfb_mask=0x%08x "
                            "first_sfb=%d last_sfb=%d "
                            "mean_tonality=%.3f min_stability=%.3f mean_ath_margin_db=%.2f "
                            "old_bits=%d new_bits=%d "
                            "old_selected_max_noise=%.2f new_selected_max_noise=%.2f "
                            "old_selected_over_noise=%.2f new_selected_over_noise=%.2f "
                            "old_selected_over_count=%d new_selected_over_count=%d "
                            "old_selected_max_loss_db=%.2f new_selected_max_loss_db=%.2f "
                            "old_selected_loss_db_sum=%.2f new_selected_loss_db_sum=%.2f "
                            "old_selected_max_failure_db=%.2f new_selected_max_failure_db=%.2f "
                            "old_selected_failure_db_sum=%.2f new_selected_failure_db_sum=%.2f "
                            "old_global_max_noise=%.2f new_global_max_noise=%.2f "
                            "old_over_count=%d new_over_count=%d "
                            "tightened_bands=%d "
                            "steady_tonal_protect_reject_reason=%s rollback_ok=%d\n",
                            profiles_tried,
                            summary_profile, accept,
                            gfc->ov_enc.frame_number, gr2, ch2, psy_ch,
                            short_mask_relax_source_name(psy_ch),
                            candidate_info.sfb_count, candidate_info.sfb_mask,
                            candidate_info.first_sfb, candidate_info.last_sfb,
                            (double) candidate_info.mean_tonality,
                            (double) candidate_info.min_stability,
                            (double) candidate_info.mean_ath_margin_db,
                            old_bits, summary_bits,
                            (double) old_selected_failure.max_noise,
                            (double) summary_selected_failure->max_noise,
                            (double) old_selected_failure.over_noise,
                            (double) summary_selected_failure->over_noise,
                            old_selected_failure.over_count,
                            summary_selected_failure->over_count,
                            (double) old_selected_failure.max_loss_db,
                            (double) summary_selected_failure->max_loss_db,
                            (double) old_selected_failure.loss_db_sum,
                            (double) summary_selected_failure->loss_db_sum,
                            (double) old_selected_failure.max_failure_db,
                            (double) summary_selected_failure->max_failure_db,
                            (double) old_selected_failure.failure_db_sum,
                            (double) summary_selected_failure->failure_db_sum,
                            (double) noise_orig.max_noise,
                            (double) summary_noise_orig->max_noise,
                            noise_orig.over_count,
                            summary_noise_orig->over_count,
                            summary_tightened_bands,
                            reject_reason, rollback_ok);
                }
            }
        }
    }
}
/*********************************************************************
 * Direct float-to-int conversion is the only quantization path now.
 *********************************************************************/
#  define QUANTFAC(rx)  adj43[rx]
#  define XRPOW_FTOI(src,dest) ((dest) = (int)(src))


inline static float
vec_max_c(lamer_dsp const *dsp, const float * xr34, unsigned int bw)
{
    return dsp->max_f32(xr34, (int) bw);
}


inline static  uint8_t
find_lowest_scalefac(const FLOAT xr34)
{
    uint8_t sf_ok = 255;
    uint8_t sf = 128, delsf = 64;
    uint8_t i;
    FLOAT const ixmax_val = IXMAX_VAL;
    for (i = 0; i < 8; ++i) {
        FLOAT const xfsf = ipow20[sf] * xr34;
        if (xfsf <= ixmax_val) {
            sf_ok = sf;
            sf -= delsf;
        }
        else {
            sf += delsf;
        }
        delsf >>= 1;
    }
    return sf_ok;
}


/*  do call the calc_sfb_noise_* functions only with sf values
 *  for which holds: sfpow34*xr34 <= IXMAX_VAL
 */

static  FLOAT
calc_sfb_noise_x34(lamer_dsp const *dsp,
                   const FLOAT * xr, const FLOAT * xr34, unsigned int bw, uint8_t sf)
{
    return dsp->vbr_calc_sfb_noise_x34(xr, xr34, bw, sf);
}



struct calc_noise_cache {
    unsigned char valid[256];
    FLOAT   value[256];
};

typedef struct calc_noise_cache calc_noise_cache_t;


static  uint8_t
tri_calc_sfb_noise_x34(lamer_dsp const *dsp,
                       const FLOAT * xr, const FLOAT * xr34, FLOAT l3_xmin, unsigned int bw,
                       uint8_t sf, calc_noise_cache_t * did_it)
{
    if (did_it->valid[sf] == 0) {
        did_it->valid[sf] = 1;
        did_it->value[sf] = calc_sfb_noise_x34(dsp, xr, xr34, bw, sf);
    }
    if (l3_xmin < did_it->value[sf]) {
        return 1;
    }
    if (sf < 255) {
        uint8_t const sf_x = sf + 1;
        if (did_it->valid[sf_x] == 0) {
            did_it->valid[sf_x] = 1;
            did_it->value[sf_x] = calc_sfb_noise_x34(dsp, xr, xr34, bw, sf_x);
        }
        if (l3_xmin < did_it->value[sf_x]) {
            return 1;
        }
    }
    if (sf > 0) {
        uint8_t const sf_x = sf - 1;
        if (did_it->valid[sf_x] == 0) {
            did_it->valid[sf_x] = 1;
            did_it->value[sf_x] = calc_sfb_noise_x34(dsp, xr, xr34, bw, sf_x);
        }
        if (l3_xmin < did_it->value[sf_x]) {
            return 1;
        }
    }
    return 0;
}


/**
 *  Robert Hegemann 2001-05-01
 *  calculates quantization step size determined by allowed masking
 */
static int
calc_scalefac(FLOAT l3_xmin, int bw)
{
    FLOAT const c = 5.799142446; /* 10 * 10^(2/3) * log10(4/3) */
    return 210 + (int) (c * log10f(l3_xmin / bw) - .5f);
}

static uint8_t
guess_scalefac_x34(const algo_t *that,
                   const FLOAT * xr, const FLOAT * xr34, FLOAT l3_xmin, unsigned int bw, uint8_t sf_min)
{
    int const guess = calc_scalefac(l3_xmin, bw);
    (void) that;
    if (guess < sf_min) return sf_min;
    if (guess >= 255) return 255;
    (void) xr;
    (void) xr34;
    return guess;
}


/* the find_scalefac* routines calculate
 * a quantization step size which would
 * introduce as much noise as is allowed.
 * The larger the step size the more
 * quantization noise we'll get. The
 * scalefactors are there to lower the
 * global step size, allowing limited
 * differences in quantization step sizes
 * per band (shaping the noise).
 */

static  uint8_t
find_scalefac_x34(const algo_t *that,
                  const FLOAT * xr, const FLOAT * xr34, FLOAT l3_xmin, unsigned int bw,
                  uint8_t sf_min)
{
    lamer_dsp const *const dsp = &that->gfc->dsp;
    calc_noise_cache_t did_it;
    uint8_t sf = 128, sf_ok = 255, delsf = 128, seen_good_one = 0, i;
    memset(did_it.valid, 0, sizeof(did_it.valid));
    for (i = 0; i < 8; ++i) {
        delsf >>= 1;
        if (sf <= sf_min) {
            sf += delsf;
        }
        else {
            uint8_t const bad = tri_calc_sfb_noise_x34(dsp, xr, xr34, l3_xmin, bw, sf, &did_it);
            if (bad) {  /* distortion.  try a smaller scalefactor */
                sf -= delsf;
            }
            else {
                sf_ok = sf;
                sf += delsf;
                seen_good_one = 1;
            }
        }
    }
    /*  returning a scalefac without distortion, if possible
     */
    if (seen_good_one > 0) {
        sf = sf_ok;
    }
    if (sf <= sf_min) {
        sf = sf_min;
    }
    return sf;
}



/***********************************************************************
 *
 *      calc_short_block_vbr_sf()
 *      calc_long_block_vbr_sf()
 *
 *  Mark Taylor 2000-??-??
 *  Robert Hegemann 2000-10-25 made functions of it
 *
 ***********************************************************************/

/* a variation for vbr-mtrh */
static int
block_sf(algo_t * that, const FLOAT l3_xmin[SFBMAX], int vbrsf[SFBMAX], int vbrsfmin[SFBMAX])
{
    FLOAT   max_xr34;
    const FLOAT *const xr = &that->cod_info->xr[0];
    const FLOAT *const xr34_orig = &that->xr34orig[0];
    const int *const width = &that->cod_info->width[0];
    const char *const energy_above_cutoff = &that->cod_info->energy_above_cutoff[0];
    unsigned int const max_nonzero_coeff = (unsigned int) that->cod_info->max_nonzero_coeff;
    uint8_t maxsf = 0;
    int     sfb = 0, m_o = -1;
    unsigned int j = 0, i = 0;
    int const psymax = that->cod_info->psymax;

    assert(that->cod_info->max_nonzero_coeff >= 0);

    that->mingain_l = 0;
    that->mingain_s[0] = 0;
    that->mingain_s[1] = 0;
    that->mingain_s[2] = 0;
    while (j <= max_nonzero_coeff) {
        unsigned int const w = (unsigned int) width[sfb];
        unsigned int const m = (unsigned int) (max_nonzero_coeff - j + 1);
        unsigned int l = w;
        uint8_t m1, m2;
        if (l > m) {
            l = m;
        }
        max_xr34 = vec_max_c(&that->gfc->dsp, &xr34_orig[j], l);

        m1 = find_lowest_scalefac(max_xr34);
        vbrsfmin[sfb] = m1;
        if (that->mingain_l < m1) {
            that->mingain_l = m1;
        }
        if (that->mingain_s[i] < m1) {
            that->mingain_s[i] = m1;
        }
        if (++i > 2) {
            i = 0;
        }
        if (sfb < psymax && w > 2) { /* mpeg2.5 at 8 kHz doesn't use all scalefactors, unused have width 2 */
            if (energy_above_cutoff[sfb]) {
                m2 = that->find(that, &xr[j], &xr34_orig[j], l3_xmin[sfb], l, m1);
                if (maxsf < m2) {
                    maxsf = m2;
                }
                if (m_o < m2 && m2 < 255) {
                    m_o = m2;
                }
            }
            else {
                m2 = 255;
                maxsf = 255;
            }
        }
        else {
            if (maxsf < m1) {
                maxsf = m1;
            }
            m2 = maxsf;
        }
        vbrsf[sfb] = m2;
        ++sfb;
        j += w;        
    }
    for (; sfb < SFBMAX; ++sfb) {
        vbrsf[sfb] = maxsf;
        vbrsfmin[sfb] = 0;
    }
    if (m_o > -1) {
        maxsf = m_o;
        for (sfb = 0; sfb < SFBMAX; ++sfb) {
            if (vbrsf[sfb] == 255) {
                vbrsf[sfb] = m_o;
            }
        }
    }
    return maxsf;
}



/***********************************************************************
 *
 *  quantize xr34 based on scalefactors
 *
 *  block_xr34
 *
 *  Mark Taylor 2000-??-??
 *  Robert Hegemann 2000-10-20 made functions of them
 *
 ***********************************************************************/

static void
quantize_x34(const algo_t * that)
{
    const FLOAT *xr34_orig = that->xr34orig;
    gr_info *const cod_info = that->cod_info;
    int const ifqstep = (cod_info->scalefac_scale == 0) ? 2 : 4;
    int    *l3 = cod_info->l3_enc;
    unsigned int j = 0, sfb = 0;
    unsigned int const max_nonzero_coeff = (unsigned int) cod_info->max_nonzero_coeff;

    assert(cod_info->max_nonzero_coeff >= 0);
    assert(cod_info->max_nonzero_coeff < 576);

    while (j <= max_nonzero_coeff) {
        int const s =
            (cod_info->scalefac[sfb] + (cod_info->preflag ? pretab[sfb] : 0)) * ifqstep
            + cod_info->subblock_gain[cod_info->window[sfb]] * 8;
        uint8_t const sfac = (uint8_t) (cod_info->global_gain - s);
        FLOAT const sfpow34 = ipow20[sfac];
        unsigned int const w = (unsigned int) cod_info->width[sfb];
        unsigned int const m = (unsigned int) (max_nonzero_coeff - j + 1);
        unsigned int i;

        assert((cod_info->global_gain - s) >= 0);
        assert(cod_info->width[sfb] >= 0);
        j += w;
        ++sfb;
        
        i = (w <= m) ? w : m;
        that->gfc->dsp.vbr_quantize_x34(l3, xr34_orig, i, sfpow34);
        l3 += i;
        xr34_orig += i;
    }
}



static const uint8_t max_range_short[SBMAX_s * 3] = {
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    0, 0, 0
};

static const uint8_t max_range_long[SBMAX_l] = {
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 0
};

static const uint8_t max_range_long_lsf_pretab[SBMAX_l] = {
    7, 7, 7, 7, 7, 7, 3, 3, 3, 3, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};



/*
    sfb=0..5  scalefac < 16
    sfb>5     scalefac < 8

    ifqstep = ( cod_info->scalefac_scale == 0 ) ? 2 : 4;
    ol_sf =  (cod_info->global_gain-210.0);
    ol_sf -= 8*cod_info->subblock_gain[i];
    ol_sf -= ifqstep*scalefac[gr][ch].s[sfb][i];
*/

static void
set_subblock_gain(gr_info * cod_info, const int mingain_s[3], int sf[])
{
    const int maxrange1 = 15, maxrange2 = 7;
    const int ifqstepShift = (cod_info->scalefac_scale == 0) ? 1 : 2;
    int    *const sbg = cod_info->subblock_gain;
    unsigned int const psymax = (unsigned int) cod_info->psymax;
    unsigned int psydiv = 18;
    int     sbg0, sbg1, sbg2;
    unsigned int sfb, i;
    int     min_sbg = 7;

    if (psydiv > psymax) {
        psydiv = psymax;
    }
    for (i = 0; i < 3; ++i) {
        int     maxsf1 = 0, maxsf2 = 0, minsf = 1000;
        /* see if we should use subblock gain */
        for (sfb = i; sfb < psydiv; sfb += 3) { /* part 1 */
            int const v = -sf[sfb];
            if (maxsf1 < v) {
                maxsf1 = v;
            }
            if (minsf > v) {
                minsf = v;
            }
        }
        for (; sfb < SFBMAX; sfb += 3) { /* part 2 */
            int const v = -sf[sfb];
            if (maxsf2 < v) {
                maxsf2 = v;
            }
            if (minsf > v) {
                minsf = v;
            }
        }

        /* boost subblock gain as little as possible so we can
         * reach maxsf1 with scalefactors
         * 8*sbg >= maxsf1
         */
        {
            int const m1 = maxsf1 - (maxrange1 << ifqstepShift);
            int const m2 = maxsf2 - (maxrange2 << ifqstepShift);

            maxsf1 = Max(m1, m2);
        }
        if (minsf > 0) {
            sbg[i] = minsf >> 3;
        }
        else {
            sbg[i] = 0;
        }
        if (maxsf1 > 0) {
            int const m1 = sbg[i];
            int const m2 = (maxsf1 + 7) >> 3;
            sbg[i] = Max(m1, m2);
        }
        if (sbg[i] > 0 && mingain_s[i] > (cod_info->global_gain - sbg[i] * 8)) {
            sbg[i] = (cod_info->global_gain - mingain_s[i]) >> 3;
        }
        if (sbg[i] > 7) {
            sbg[i] = 7;
        }
        if (min_sbg > sbg[i]) {
            min_sbg = sbg[i];
        }
    }
    sbg0 = sbg[0] * 8;
    sbg1 = sbg[1] * 8;
    sbg2 = sbg[2] * 8;
    for (sfb = 0; sfb < SFBMAX; sfb += 3) {
        sf[sfb + 0] += sbg0;
        sf[sfb + 1] += sbg1;
        sf[sfb + 2] += sbg2;
    }
    if (min_sbg > 0) {
        for (i = 0; i < 3; ++i) {
            sbg[i] -= min_sbg;
        }
        cod_info->global_gain -= min_sbg * 8;
    }
}



/*
	  ifqstep = ( cod_info->scalefac_scale == 0 ) ? 2 : 4;
	  ol_sf =  (cod_info->global_gain-210.0);
	  ol_sf -= ifqstep*scalefac[gr][ch].l[sfb];
	  if (cod_info->preflag && sfb>=11)
	  ol_sf -= ifqstep*pretab[sfb];
*/
static void
set_scalefacs(gr_info * cod_info, const int *vbrsfmin, int sf[], const uint8_t * max_range)
{
    const int ifqstep = (cod_info->scalefac_scale == 0) ? 2 : 4;
    const int ifqstepShift = (cod_info->scalefac_scale == 0) ? 1 : 2;
    int    *const scalefac = cod_info->scalefac;
    int const sfbmax = cod_info->sfbmax;
    int     sfb;
    int const *const sbg = cod_info->subblock_gain;
    int const *const window = cod_info->window;
    int const preflag = cod_info->preflag;

    if (preflag) {
        for (sfb = 11; sfb < sfbmax; ++sfb) {
            sf[sfb] += pretab[sfb] * ifqstep;
        }
    }
    for (sfb = 0; sfb < sfbmax; ++sfb) {
        int const gain = cod_info->global_gain - (sbg[window[sfb]] * 8)
            - ((preflag ? pretab[sfb] : 0) * ifqstep);

        if (sf[sfb] < 0) {
            int const m = gain - vbrsfmin[sfb];
            /* ifqstep*scalefac >= -sf[sfb], so round UP */
            scalefac[sfb] = (ifqstep - 1 - sf[sfb]) >> ifqstepShift;

            if (scalefac[sfb] > max_range[sfb]) {
                scalefac[sfb] = max_range[sfb];
            }
            if (scalefac[sfb] > 0 && (scalefac[sfb] << ifqstepShift) > m) {
                scalefac[sfb] = m >> ifqstepShift;
            }
        }
        else {
            scalefac[sfb] = 0;
        }
    }
    for (; sfb < SFBMAX; ++sfb) {
        scalefac[sfb] = 0; /* sfb21 */
    }
}


#ifndef NDEBUG
static int
checkScalefactor(const gr_info * cod_info, const int vbrsfmin[SFBMAX])
{
    int const ifqstep = cod_info->scalefac_scale == 0 ? 2 : 4;
    int     sfb;
    for (sfb = 0; sfb < cod_info->psymax; ++sfb) {
        const int s =
            ((cod_info->scalefac[sfb] +
              (cod_info->preflag ? pretab[sfb] : 0)) * ifqstep) +
            cod_info->subblock_gain[cod_info->window[sfb]] * 8;

        if ((cod_info->global_gain - s) < vbrsfmin[sfb]) {
            /*
               fprintf( stdout, "sf %d\n", sfb );
               fprintf( stdout, "min %d\n", vbrsfmin[sfb] );
               fprintf( stdout, "ggain %d\n", cod_info->global_gain );
               fprintf( stdout, "scalefac %d\n", cod_info->scalefac[sfb] );
               fprintf( stdout, "pretab %d\n", (cod_info->preflag ? pretab[sfb] : 0) );
               fprintf( stdout, "scale %d\n", (cod_info->scalefac_scale + 1) );
               fprintf( stdout, "subgain %d\n", cod_info->subblock_gain[cod_info->window[sfb]] * 8 );
               fflush( stdout );
               exit(-1);
             */
            return 0;
        }
    }
    return 1;
}
#endif


/******************************************************************
 *
 *  short block scalefacs
 *
 ******************************************************************/

static void
short_block_constrain(const algo_t * that, const int vbrsf[SFBMAX],
                      const int vbrsfmin[SFBMAX], int vbrmax)
{
    gr_info *const cod_info = that->cod_info;
    lame_internal_flags const *const gfc = that->gfc;
    SessionConfig_t const *const cfg = &gfc->cfg;
    int const maxminsfb = that->mingain_l;
    int     mover, maxover0 = 0, maxover1 = 0, delta = 0;
    int     v, v0, v1;
    int     sfb;
    int const psymax = cod_info->psymax;

    for (sfb = 0; sfb < psymax; ++sfb) {
        assert(vbrsf[sfb] >= vbrsfmin[sfb]);
        v = vbrmax - vbrsf[sfb];
        if (delta < v) {
            delta = v;
        }
        v0 = v - (4 * 14 + 2 * max_range_short[sfb]);
        v1 = v - (4 * 14 + 4 * max_range_short[sfb]);
        if (maxover0 < v0) {
            maxover0 = v0;
        }
        if (maxover1 < v1) {
            maxover1 = v1;
        }
    }
    if (cfg->noise_shaping == 2) {
        /* allow scalefac_scale=1 */
        mover = Min(maxover0, maxover1);
    }
    else {
        mover = maxover0;
    }
    if (delta > mover) {
        delta = mover;
    }
    vbrmax -= delta;
    maxover0 -= mover;
    maxover1 -= mover;

    if (maxover0 == 0) {
        cod_info->scalefac_scale = 0;
    }
    else if (maxover1 == 0) {
        cod_info->scalefac_scale = 1;
    }
    if (vbrmax < maxminsfb) {
        vbrmax = maxminsfb;
    }
    cod_info->global_gain = vbrmax;

    if (cod_info->global_gain < 0) {
        cod_info->global_gain = 0;
    }
    else if (cod_info->global_gain > 255) {
        cod_info->global_gain = 255;
    }
    {
        int     sf_temp[SFBMAX];
        for (sfb = 0; sfb < SFBMAX; ++sfb) {
            sf_temp[sfb] = vbrsf[sfb] - vbrmax;
        }
        set_subblock_gain(cod_info, &that->mingain_s[0], sf_temp);
        set_scalefacs(cod_info, vbrsfmin, sf_temp, max_range_short);
    }
    assert(checkScalefactor(cod_info, vbrsfmin));
}



/******************************************************************
 *
 *  long block scalefacs
 *
 ******************************************************************/

static void
long_block_constrain(const algo_t * that, const int vbrsf[SFBMAX], const int vbrsfmin[SFBMAX],
                     int vbrmax)
{
    gr_info *const cod_info = that->cod_info;
    lame_internal_flags const *const gfc = that->gfc;
    SessionConfig_t const *const cfg = &gfc->cfg;
    uint8_t const *max_rangep;
    int const maxminsfb = that->mingain_l;
    int     sfb;
    int     maxover0, maxover1, maxover0p, maxover1p, mover, delta = 0;
    int     v, v0, v1, v0p, v1p, vm0p = 1, vm1p = 1;
    int const psymax = cod_info->psymax;

    max_rangep = cfg->mode_gr == 2 ? max_range_long : max_range_long_lsf_pretab;

    maxover0 = 0;
    maxover1 = 0;
    maxover0p = 0;      /* pretab */
    maxover1p = 0;      /* pretab */

    for (sfb = 0; sfb < psymax; ++sfb) {
        assert(vbrsf[sfb] >= vbrsfmin[sfb]);
        v = vbrmax - vbrsf[sfb];
        if (delta < v) {
            delta = v;
        }
        v0 = v - 2 * max_range_long[sfb];
        v1 = v - 4 * max_range_long[sfb];
        v0p = v - 2 * (max_rangep[sfb] + pretab[sfb]);
        v1p = v - 4 * (max_rangep[sfb] + pretab[sfb]);
        if (maxover0 < v0) {
            maxover0 = v0;
        }
        if (maxover1 < v1) {
            maxover1 = v1;
        }
        if (maxover0p < v0p) {
            maxover0p = v0p;
        }
        if (maxover1p < v1p) {
            maxover1p = v1p;
        }
    }
    if (vm0p == 1) {
        int     gain = vbrmax - maxover0p;
        if (gain < maxminsfb) {
            gain = maxminsfb;
        }
        for (sfb = 0; sfb < psymax; ++sfb) {
            int const a = (gain - vbrsfmin[sfb]) - 2 * pretab[sfb];
            if (a <= 0) {
                vm0p = 0;
                vm1p = 0;
                break;
            }
        }
    }
    if (vm1p == 1) {
        int     gain = vbrmax - maxover1p;
        if (gain < maxminsfb) {
            gain = maxminsfb;
        }
        for (sfb = 0; sfb < psymax; ++sfb) {
            int const b = (gain - vbrsfmin[sfb]) - 4 * pretab[sfb];
            if (b <= 0) {
                vm1p = 0;
                break;
            }
        }
    }
    if (vm0p == 0) {
        maxover0p = maxover0;
    }
    if (vm1p == 0) {
        maxover1p = maxover1;
    }
    if (cfg->noise_shaping != 2) {
        maxover1 = maxover0;
        maxover1p = maxover0p;
    }
    mover = Min(maxover0, maxover0p);
    mover = Min(mover, maxover1);
    mover = Min(mover, maxover1p);

    if (delta > mover) {
        delta = mover;
    }
    vbrmax -= delta;
    if (vbrmax < maxminsfb) {
        vbrmax = maxminsfb;
    }
    maxover0 -= mover;
    maxover0p -= mover;
    maxover1 -= mover;
    maxover1p -= mover;

    if (maxover0 == 0) {
        cod_info->scalefac_scale = 0;
        cod_info->preflag = 0;
        max_rangep = max_range_long;
    }
    else if (maxover0p == 0) {
        cod_info->scalefac_scale = 0;
        cod_info->preflag = 1;
    }
    else if (maxover1 == 0) {
        cod_info->scalefac_scale = 1;
        cod_info->preflag = 0;
        max_rangep = max_range_long;
    }
    else if (maxover1p == 0) {
        cod_info->scalefac_scale = 1;
        cod_info->preflag = 1;
    }
    else {
        assert(0);      /* this should not happen */
    }
    cod_info->global_gain = vbrmax;
    if (cod_info->global_gain < 0) {
        cod_info->global_gain = 0;
    }
    else if (cod_info->global_gain > 255) {
        cod_info->global_gain = 255;
    }
    {
        int     sf_temp[SFBMAX];
        for (sfb = 0; sfb < SFBMAX; ++sfb) {
            sf_temp[sfb] = vbrsf[sfb] - vbrmax;
        }
        set_scalefacs(cod_info, vbrsfmin, sf_temp, max_rangep);
    }
    assert(checkScalefactor(cod_info, vbrsfmin));
}



static void
bitcount(const algo_t * that)
{
    int     rc = scale_bitcount(that->gfc, that->cod_info);

    if (rc == 0) {
        return;
    }
    /*  this should not happen due to the way the scalefactors are selected  */
    ERRORF(that->gfc, "INTERNAL ERROR IN VBR NEW CODE (986), please send bug report\n");
    exit(-1);
}



static int
quantizeAndCountBits(const algo_t * that)
{
    quantize_x34(that);
    that->cod_info->part2_3_length = noquant_count_bits(that->gfc, that->cod_info, 0);
    return that->cod_info->part2_3_length;
}





static int
tryGlobalStepsize(const algo_t * that, const int sfwork[SFBMAX],
                  const int vbrsfmin[SFBMAX], int delta)
{
    FLOAT const xrpow_max = that->cod_info->xrpow_max;
    int     sftemp[SFBMAX], i, nbits;
    int     gain, vbrmax = 0;
    for (i = 0; i < SFBMAX; ++i) {
        gain = sfwork[i] + delta;
        if (gain < vbrsfmin[i]) {
            gain = vbrsfmin[i];
        }
        if (gain > 255) {
            gain = 255;
        }
        if (vbrmax < gain) {
            vbrmax = gain;
        }
        sftemp[i] = gain;
    }
    that->alloc(that, sftemp, vbrsfmin, vbrmax);
    bitcount(that);
    nbits = quantizeAndCountBits(that);
    that->cod_info->xrpow_max = xrpow_max;
    return nbits;
}



static void
searchGlobalStepsizeMax(const algo_t * that, const int sfwork[SFBMAX],
                        const int vbrsfmin[SFBMAX], int target)
{
    gr_info const *const cod_info = that->cod_info;
    const int gain = cod_info->global_gain;
    int     curr = gain;
    int     gain_ok = 1024;
    int     nbits = LARGE_BITS;
    int     l = gain, r = 512;

    assert(gain >= 0);
    while (l <= r) {
        curr = (l + r) >> 1;
        nbits = tryGlobalStepsize(that, sfwork, vbrsfmin, curr - gain);
        if (nbits == 0 || (nbits + cod_info->part2_length) < target) {
            r = curr - 1;
            gain_ok = curr;
        }
        else {
            l = curr + 1;
            if (gain_ok == 1024) {
                gain_ok = curr;
            }
        }
    }
    if (gain_ok != curr) {
        curr = gain_ok;
        nbits = tryGlobalStepsize(that, sfwork, vbrsfmin, curr - gain);
    }
}



static int
sfDepth(const int sfwork[SFBMAX])
{
    int     m = 0;
    unsigned int i, j;
    for (j = SFBMAX, i = 0; j > 0; --j, ++i) {
        int const di = 255 - sfwork[i];
        if (m < di) {
            m = di;
        }
        assert(sfwork[i] >= 0);
        assert(sfwork[i] <= 255);
    }
    assert(m >= 0);
    assert(m <= 255);
    return m;
}


static void
cutDistribution(const int sfwork[SFBMAX], int sf_out[SFBMAX], int cut)
{
    unsigned int i, j;
    for (j = SFBMAX, i = 0; j > 0; --j, ++i) {
        int const x = sfwork[i];
        sf_out[i] = x < cut ? x : cut;
    }
}


static int
flattenDistribution(const int sfwork[SFBMAX], int sf_out[SFBMAX], int dm, int k, int p)
{
    unsigned int i, j;
    int     x, sfmax = 0;
    if (dm > 0) {
        for (j = SFBMAX, i = 0; j > 0; --j, ++i) {
            int const di = p - sfwork[i];
            x = sfwork[i] + (k * di) / dm;
            if (x < 0) {
                x = 0;
            }
            else {
                if (x > 255) {
                    x = 255;
                }
            }
            sf_out[i] = x;
            if (sfmax < x) {
                sfmax = x;
            }
        }
    }
    else {
        for (j = SFBMAX, i = 0; j > 0u; --j, ++i) {
            x = sfwork[i];
            sf_out[i] = x;
            if (sfmax < x) {
                sfmax = x;
            }
        }
    }
    return sfmax;
}


static int
tryThatOne(algo_t const* that, const int sftemp[SFBMAX], const int vbrsfmin[SFBMAX], int vbrmax)
{
    FLOAT const xrpow_max = that->cod_info->xrpow_max;
    int     nbits = LARGE_BITS;
    that->alloc(that, sftemp, vbrsfmin, vbrmax);
    bitcount(that);
    nbits = quantizeAndCountBits(that);
    nbits += that->cod_info->part2_length;
    that->cod_info->xrpow_max = xrpow_max;
    return nbits;
}


static void
outOfBitsStrategy(algo_t const* that, const int sfwork[SFBMAX], const int vbrsfmin[SFBMAX], int target)
{
    int     wrk[SFBMAX];
    int const dm = sfDepth(sfwork);
    int const p = that->cod_info->global_gain;
    int     nbits;

    /* PART 1 */
    {
        int     bi = dm / 2;
        int     bi_ok = -1;
        int     bu = 0;
        int     bo = dm;
        for (;;) {
            int const sfmax = flattenDistribution(sfwork, wrk, dm, bi, p);
            nbits = tryThatOne(that, wrk, vbrsfmin, sfmax);
            if (nbits <= target) {
                bi_ok = bi;
                bo = bi - 1;
            }
            else {
                bu = bi + 1;
            }
            if (bu <= bo) {
                bi = (bu + bo) / 2;
            }
            else {
                break;
            }
        }
        if (bi_ok >= 0) {
            if (bi != bi_ok) {
                int const sfmax = flattenDistribution(sfwork, wrk, dm, bi_ok, p);
                nbits = tryThatOne(that, wrk, vbrsfmin, sfmax);
            }
            return;
        }
    }

    /* PART 2: */
    {
        int     bi = (255 + p) / 2;
        int     bi_ok = -1;
        int     bu = p;
        int     bo = 255;
        for (;;) {
            int const sfmax = flattenDistribution(sfwork, wrk, dm, dm, bi);
            nbits = tryThatOne(that, wrk, vbrsfmin, sfmax);
            if (nbits <= target) {
                bi_ok = bi;
                bo = bi - 1;
            }
            else {
                bu = bi + 1;
            }
            if (bu <= bo) {
                bi = (bu + bo) / 2;
            }
            else {
                break;
            }
        }
        if (bi_ok >= 0) {
            if (bi != bi_ok) {
                int const sfmax = flattenDistribution(sfwork, wrk, dm, dm, bi_ok);
                nbits = tryThatOne(that, wrk, vbrsfmin, sfmax);
            }
            return;
        }
    }

    /* fall back to old code, likely to be never called */
    searchGlobalStepsizeMax(that, wrk, vbrsfmin, target);
}


static int
reduce_bit_usage(lame_internal_flags * gfc, int gr, int ch
    )
{
    SessionConfig_t const *const cfg = &gfc->cfg;
    gr_info *const cod_info = &gfc->l3_side.tt[gr][ch];
    /*  try some better scalefac storage
     */
    best_scalefac_store(gfc, gr, ch, &gfc->l3_side);

    /*  best huffman_divide may save some bits too
     */
    if (cfg->use_best_huffman == 1)
        best_huffman_divide(gfc, cod_info);
    return cod_info->part2_3_length + cod_info->part2_length;
}




int
VBR_encode_frame(lame_internal_flags * gfc, const FLOAT xr34orig[2][2][576],
                 const FLOAT l3_xmin[2][2][SFBMAX], const int max_bits[2][2])
{
    SessionConfig_t const *const cfg = &gfc->cfg;
    int     sfwork_[2][2][SFBMAX];
    int     vbrsfmin_[2][2][SFBMAX];
    algo_t  that_[2][2];
    int const ngr = cfg->mode_gr;
    int const nch = cfg->channels_out;
    int     max_nbits_ch[2][2] = {{0, 0}, {0 ,0}};
    int     max_nbits_gr[2] = {0, 0};
    int     max_nbits_fr = 0;
    int     use_nbits_ch[2][2] = {{MAX_BITS_PER_CHANNEL+1, MAX_BITS_PER_CHANNEL+1}
                                 ,{MAX_BITS_PER_CHANNEL+1, MAX_BITS_PER_CHANNEL+1}};
    int     use_nbits_gr[2] = { MAX_BITS_PER_GRANULE+1, MAX_BITS_PER_GRANULE+1 };
    int     use_nbits_fr = MAX_BITS_PER_GRANULE+MAX_BITS_PER_GRANULE;
    int     gr, ch;
    int     ok, sum_fr;

    /* set up some encoding parameters
     */
    for (gr = 0; gr < ngr; ++gr) {
        max_nbits_gr[gr] = 0;
        for (ch = 0; ch < nch; ++ch) {
            max_nbits_ch[gr][ch] = max_bits[gr][ch];
            use_nbits_ch[gr][ch] = 0;
            max_nbits_gr[gr] += max_bits[gr][ch];
            max_nbits_fr += max_bits[gr][ch];
            that_[gr][ch].find = (cfg->full_outer_loop < 0) ? guess_scalefac_x34 : find_scalefac_x34;
            that_[gr][ch].gfc = gfc;
            that_[gr][ch].cod_info = &gfc->l3_side.tt[gr][ch];
            that_[gr][ch].xr34orig = xr34orig[gr][ch];
            if (that_[gr][ch].cod_info->block_type == SHORT_TYPE) {
                that_[gr][ch].alloc = short_block_constrain;
            }
            else {
                that_[gr][ch].alloc = long_block_constrain;
            }
        }               /* for ch */
    }
    /* searches scalefactors
     */
    for (gr = 0; gr < ngr; ++gr) {
        for (ch = 0; ch < nch; ++ch) {
            if (max_bits[gr][ch] > 0) {
                algo_t *that = &that_[gr][ch];
                int    *sfwork = sfwork_[gr][ch];
                int    *vbrsfmin = vbrsfmin_[gr][ch];
                int     vbrmax;

                vbrmax = block_sf(that, l3_xmin[gr][ch], sfwork, vbrsfmin);
                that->alloc(that, sfwork, vbrsfmin, vbrmax);
                bitcount(that);
            }
            else {
                /*  xr contains no energy 
                 *  l3_enc, our encoding data, will be quantized to zero
                 *  continue with next channel
                 */
            }
        }               /* for ch */
    }
    /* encode 'as is'
     */
    use_nbits_fr = 0;
    for (gr = 0; gr < ngr; ++gr) {
        use_nbits_gr[gr] = 0;
        for (ch = 0; ch < nch; ++ch) {
            algo_t const *that = &that_[gr][ch];
            if (max_bits[gr][ch] > 0) {
                memset(&that->cod_info->l3_enc[0], 0, sizeof(that->cod_info->l3_enc));
                (void) quantizeAndCountBits(that);
            }
            else {
                /*  xr contains no energy 
                 *  l3_enc, our encoding data, will be quantized to zero
                 *  continue with next channel
                 */
            }
            use_nbits_ch[gr][ch] = reduce_bit_usage(gfc, gr, ch);
            use_nbits_gr[gr] += use_nbits_ch[gr][ch];
        }               /* for ch */
        use_nbits_fr += use_nbits_gr[gr];
    }

    /* check bit constrains
     */
    if (use_nbits_fr <= max_nbits_fr) {
        ok = 1;
        for (gr = 0; gr < ngr; ++gr) {
            if (use_nbits_gr[gr] > MAX_BITS_PER_GRANULE) {
                /* violates the rule that every granule has to use no more
                 * bits than MAX_BITS_PER_GRANULE
                 */
                ok = 0;
            }
            for (ch = 0; ch < nch; ++ch) {
                if (use_nbits_ch[gr][ch] > MAX_BITS_PER_CHANNEL) {
                    /* violates the rule that every gr_ch has to use no more
                     * bits than MAX_BITS_PER_CHANNEL
                     *
                     * This isn't explicitly stated in the ISO docs, but the
                     * part2_3_length field has only 12 bits, that makes it
                     * up to a maximum size of 4095 bits!!!
                     */
                    ok = 0;
                }
            }
        }
        if (ok) {
            return use_nbits_fr;
        }
    }
    
    /* OK, we are in trouble and have to define how many bits are
     * to be used for each granule
     */
    {
        ok = 1;
        sum_fr = 0;

        for (gr = 0; gr < ngr; ++gr) {
            max_nbits_gr[gr] = 0;
            for (ch = 0; ch < nch; ++ch) {
                if (use_nbits_ch[gr][ch] > MAX_BITS_PER_CHANNEL) {
                    max_nbits_ch[gr][ch] = MAX_BITS_PER_CHANNEL;
                }
                else {
                    max_nbits_ch[gr][ch] = use_nbits_ch[gr][ch];
                }
                max_nbits_gr[gr] += max_nbits_ch[gr][ch];
            }
            if (max_nbits_gr[gr] > MAX_BITS_PER_GRANULE) {
                float   f[2] = {0.0f, 0.0f}, s = 0.0f;
                for (ch = 0; ch < nch; ++ch) {
                    if (max_nbits_ch[gr][ch] > 0) {
                        f[ch] = sqrt(sqrt(max_nbits_ch[gr][ch]));
                        s += f[ch];
                    }
                    else {
                        f[ch] = 0;
                    }
                }
                for (ch = 0; ch < nch; ++ch) {
                    if (s > 0) {
                        max_nbits_ch[gr][ch] = MAX_BITS_PER_GRANULE * f[ch] / s;
                    }
                    else {
                        max_nbits_ch[gr][ch] = 0;
                    }
                }
                if (nch > 1) {
                    if (max_nbits_ch[gr][0] > use_nbits_ch[gr][0] + 32) {
                        max_nbits_ch[gr][1] += max_nbits_ch[gr][0];
                        max_nbits_ch[gr][1] -= use_nbits_ch[gr][0] + 32;
                        max_nbits_ch[gr][0] = use_nbits_ch[gr][0] + 32;
                    }
                    if (max_nbits_ch[gr][1] > use_nbits_ch[gr][1] + 32) {
                        max_nbits_ch[gr][0] += max_nbits_ch[gr][1];
                        max_nbits_ch[gr][0] -= use_nbits_ch[gr][1] + 32;
                        max_nbits_ch[gr][1] = use_nbits_ch[gr][1] + 32;
                    }
                    if (max_nbits_ch[gr][0] > MAX_BITS_PER_CHANNEL) {
                        max_nbits_ch[gr][0] = MAX_BITS_PER_CHANNEL;
                    }
                    if (max_nbits_ch[gr][1] > MAX_BITS_PER_CHANNEL) {
                        max_nbits_ch[gr][1] = MAX_BITS_PER_CHANNEL;
                    }
                }
                max_nbits_gr[gr] = 0;
                for (ch = 0; ch < nch; ++ch) {
                    max_nbits_gr[gr] += max_nbits_ch[gr][ch];
                }
            }
            sum_fr += max_nbits_gr[gr];
        }
        if (sum_fr > max_nbits_fr) {
            {
                float   f[2] = {0.0f, 0.0f}, s = 0.0f;
                for (gr = 0; gr < ngr; ++gr) {
                    if (max_nbits_gr[gr] > 0) {
                        f[gr] = sqrt(max_nbits_gr[gr]);
                        s += f[gr];
                    }
                    else {
                        f[gr] = 0;
                    }
                }
                for (gr = 0; gr < ngr; ++gr) {
                    if (s > 0) {
                        max_nbits_gr[gr] = max_nbits_fr * f[gr] / s;
                    }
                    else {
                        max_nbits_gr[gr] = 0;
                    }
                }
            }
            if (ngr > 1) {
                if (max_nbits_gr[0] > use_nbits_gr[0] + 125) {
                    max_nbits_gr[1] += max_nbits_gr[0];
                    max_nbits_gr[1] -= use_nbits_gr[0] + 125;
                    max_nbits_gr[0] = use_nbits_gr[0] + 125;
                }
                if (max_nbits_gr[1] > use_nbits_gr[1] + 125) {
                    max_nbits_gr[0] += max_nbits_gr[1];
                    max_nbits_gr[0] -= use_nbits_gr[1] + 125;
                    max_nbits_gr[1] = use_nbits_gr[1] + 125;
                }
                for (gr = 0; gr < ngr; ++gr) {
                    if (max_nbits_gr[gr] > MAX_BITS_PER_GRANULE) {
                        max_nbits_gr[gr] = MAX_BITS_PER_GRANULE;
                    }
                }
            }
            for (gr = 0; gr < ngr; ++gr) {
                float   f[2] = {0.0f, 0.0f}, s = 0.0f;
                for (ch = 0; ch < nch; ++ch) {
                    if (max_nbits_ch[gr][ch] > 0) {
                        f[ch] = sqrt(max_nbits_ch[gr][ch]);
                        s += f[ch];
                    }
                    else {
                        f[ch] = 0;
                    }
                }
                for (ch = 0; ch < nch; ++ch) {
                    if (s > 0) {
                        max_nbits_ch[gr][ch] = max_nbits_gr[gr] * f[ch] / s;
                    }
                    else {
                        max_nbits_ch[gr][ch] = 0;
                    }
                }
                if (nch > 1) {
                    if (max_nbits_ch[gr][0] > use_nbits_ch[gr][0] + 32) {
                        max_nbits_ch[gr][1] += max_nbits_ch[gr][0];
                        max_nbits_ch[gr][1] -= use_nbits_ch[gr][0] + 32;
                        max_nbits_ch[gr][0] = use_nbits_ch[gr][0] + 32;
                    }
                    if (max_nbits_ch[gr][1] > use_nbits_ch[gr][1] + 32) {
                        max_nbits_ch[gr][0] += max_nbits_ch[gr][1];
                        max_nbits_ch[gr][0] -= use_nbits_ch[gr][1] + 32;
                        max_nbits_ch[gr][1] = use_nbits_ch[gr][1] + 32;
                    }
                    for (ch = 0; ch < nch; ++ch) {
                        if (max_nbits_ch[gr][ch] > MAX_BITS_PER_CHANNEL) {
                            max_nbits_ch[gr][ch] = MAX_BITS_PER_CHANNEL;
                        }
                    }
                }
            }
        }
        /* sanity check */
        sum_fr = 0;
        for (gr = 0; gr < ngr; ++gr) {
            int     sum_gr = 0;
            for (ch = 0; ch < nch; ++ch) {
                sum_gr += max_nbits_ch[gr][ch];
                if (max_nbits_ch[gr][ch] > MAX_BITS_PER_CHANNEL) {
                    ok = 0;
                }
            }
            sum_fr += sum_gr;
            if (sum_gr > MAX_BITS_PER_GRANULE) {
                ok = 0;
            }
        }
        if (sum_fr > max_nbits_fr) {
            ok = 0;
        }
        if (!ok) {
            /* we must have done something wrong, fallback to 'on_pe' based constrain */
            for (gr = 0; gr < ngr; ++gr) {
                for (ch = 0; ch < nch; ++ch) {
                    max_nbits_ch[gr][ch] = max_bits[gr][ch];
                }
            }
        }
    }

    /* we already called the 'best_scalefac_store' function, so we need to reset some
     * variables before we can do it again.
     */
    for (ch = 0; ch < nch; ++ch) {
        gfc->l3_side.scfsi[ch][0] = 0;
        gfc->l3_side.scfsi[ch][1] = 0;
        gfc->l3_side.scfsi[ch][2] = 0;
        gfc->l3_side.scfsi[ch][3] = 0;
    }
    for (gr = 0; gr < ngr; ++gr) {
        for (ch = 0; ch < nch; ++ch) {
            gfc->l3_side.tt[gr][ch].scalefac_compress = 0;
        }
    }

    /* alter our encoded data, until it fits into the target bitrate
     */
    use_nbits_fr = 0;
    for (gr = 0; gr < ngr; ++gr) {
        use_nbits_gr[gr] = 0;
        for (ch = 0; ch < nch; ++ch) {
            algo_t const *that = &that_[gr][ch];
            use_nbits_ch[gr][ch] = 0;
            if (max_bits[gr][ch] > 0) {
                int    *sfwork = sfwork_[gr][ch];
                int const *vbrsfmin = vbrsfmin_[gr][ch];
                cutDistribution(sfwork, sfwork, that->cod_info->global_gain);
                outOfBitsStrategy(that, sfwork, vbrsfmin, max_nbits_ch[gr][ch]);
            }
            use_nbits_ch[gr][ch] = reduce_bit_usage(gfc, gr, ch);
            assert(use_nbits_ch[gr][ch] <= max_nbits_ch[gr][ch]);
            use_nbits_gr[gr] += use_nbits_ch[gr][ch];
        }               /* for ch */
        use_nbits_fr += use_nbits_gr[gr];
    }

    if (gfc->cd_psy->experimental_short_transient_redistribute) {
        short_transient_redistribute_retry(gfc, that_, l3_xmin, sfwork_,
                                           vbrsfmin_, max_nbits_ch,
                                           ngr, nch, use_nbits_ch,
                                           use_nbits_gr, &use_nbits_fr,
                                           max_nbits_fr,
                                           &short_redist_mode_experimental);
    }
    else if (short_safe_transient_redistribute_allowed(gfc)) {
        short_transient_redistribute_retry(gfc, that_, l3_xmin, sfwork_,
                                           vbrsfmin_, max_nbits_ch,
                                           ngr, nch, use_nbits_ch,
                                           use_nbits_gr, &use_nbits_fr,
                                           max_nbits_fr,
                                           &short_redist_mode_safe);
    }
    if (safe_steady_tonal_protect_allowed(gfc)) {
        steady_tonal_protect_retry(gfc, that_, l3_xmin, sfwork_,
                                   vbrsfmin_, max_nbits_ch,
                                   ngr, nch, use_nbits_ch,
                                   use_nbits_gr, &use_nbits_fr,
                                   max_nbits_fr);
    }

    /* experimental: impossible short-block threshold guard (post-second-pass) */
    if (gfc->cd_psy->experimental_short_mask_relax
        && !gfc->cd_psy->experimental_short_transient_redistribute) {
        int gr2, ch2;
        for (gr2 = 0; gr2 < ngr; ++gr2) {
            for (ch2 = 0; ch2 < nch; ++ch2) {
                algo_t *that = &that_[gr2][ch2];
                gr_info *cod_info = that->cod_info;
                PsyStateVar_t const *psv = &gfc->sv_psy;
                int const psy_ch = short_mask_relax_psy_ch(gfc, ch2);
                FLOAT const score_rel = psv->short_mask_score_rel[gr2][psy_ch];
                int const final_mask = psv->short_mask_final_mask[gr2][psy_ch];
                int const old_bits = cod_info->part2_3_length;
                FLOAT old_distort[SFBMAX];
                calc_noise_result noise_orig;
                int strong_short;
                int near_maxbits;
                int high_noise;
                int relaxed_bands;
                int candidate;
                char const *candidate_reason;

                if (cod_info->block_type != SHORT_TYPE) {
                    continue;
                }

                calc_noise(cod_info, l3_xmin[gr2][ch2], old_distort, &noise_orig, 0);

                strong_short = (score_rel >= 3.0f);
                near_maxbits = (old_bits >= MAX_BITS_PER_CHANNEL - SHORT_MASK_RELAX_BIT_SLOP);
                high_noise = (noise_orig.over_count > 2 && noise_orig.max_noise > 2.0f);
                relaxed_bands = 0;
                {
                    int sfb;
                    for (sfb = 0; sfb < cod_info->psymax; ++sfb) {
                        if (old_distort[sfb] > 1.0f) {
                            ++relaxed_bands;
                        }
                    }
                }

                candidate = 1;
                candidate_reason = "eligible";
                if (final_mask == 0) {
                    candidate = 0;
                    candidate_reason = "no_final_mask";
                }
                else if (!strong_short) {
                    candidate = 0;
                    candidate_reason = "weak_short";
                }
                else if (!near_maxbits) {
                    candidate = 0;
                    candidate_reason = "not_near_maxbits";
                }
                else if (!high_noise) {
                    candidate = 0;
                    candidate_reason = "not_high_noise";
                }
                else if (relaxed_bands == 0) {
                    candidate = 0;
                    candidate_reason = "no_relaxed_bands";
                }

                if (cfg->analysis) {
                    fprintf(stderr,
                            "short_mask_relax_candidate=%d frame=%d gr=%d ch=%d psy_ch=%d source=%s "
                            "reject_reason=%s score_rel=%.2f final_mask=0x%02x "
                            "strong_short=%d near_maxbits=%d high_noise=%d "
                            "old_bits=%d old_max_noise_orig=%.2f old_over_noise_orig=%.2f "
                            "old_over_count_orig=%d relax_db=%.2f relaxed_bands=%d\n",
                            candidate, gfc->ov_enc.frame_number, gr2, ch2, psy_ch,
                            short_mask_relax_source_name(psy_ch), candidate_reason,
                            (double) score_rel, final_mask, strong_short,
                            near_maxbits, high_noise, old_bits,
                            (double) noise_orig.max_noise,
                            (double) noise_orig.over_noise,
                            noise_orig.over_count,
                            (double) SHORT_MASK_RELAX_DB, relaxed_bands);
                }

                if (!candidate) {
                    continue;
                }

                {
                    FLOAT relaxed_xmin[SFBMAX];
                    gr_info saved_gi = *cod_info;
                    int saved_sfwork[SFBMAX];
                    int saved_vbrsfmin[SFBMAX];
                    calc_noise_result noise_retry_relaxed;
                    calc_noise_result noise_retry_orig;
                    int new_bits;
                    int accept;
                    int rollback_ok = 1;
                    char const *reject_reason;

                    memcpy(saved_sfwork, sfwork_[gr2][ch2], sizeof(saved_sfwork));
                    memcpy(saved_vbrsfmin, vbrsfmin_[gr2][ch2],
                           sizeof(saved_vbrsfmin));
                    memcpy(relaxed_xmin, l3_xmin[gr2][ch2], sizeof(relaxed_xmin));
                    {
                        int sfb;
                        for (sfb = 0; sfb < cod_info->psymax; ++sfb) {
                            if (old_distort[sfb] > 1.0f) {
                                relaxed_xmin[sfb] *= SHORT_MASK_RELAX_FACTOR;
                            }
                        }
                    }

                    {
                        int vbrmax;
                        int *sfwork = sfwork_[gr2][ch2];
                        int *vbrsfmin2 = vbrsfmin_[gr2][ch2];

                        vbrmax = block_sf(that, relaxed_xmin, sfwork, vbrsfmin2);
                        that->alloc(that, sfwork, vbrsfmin2, vbrmax);
                        bitcount(that);
                        memset(&cod_info->l3_enc[0], 0, sizeof(cod_info->l3_enc));
                        (void) quantizeAndCountBits(that);
                    }

                    calc_noise(cod_info, relaxed_xmin, old_distort,
                               &noise_retry_relaxed, 0);
                    calc_noise(cod_info, l3_xmin[gr2][ch2], old_distort,
                               &noise_retry_orig, 0);

                    new_bits = cod_info->part2_3_length;
                    accept = (new_bits <= MAX_BITS_PER_CHANNEL)
                        && (new_bits <= old_bits)
                        && (noise_retry_orig.max_noise <= noise_orig.max_noise
                            || noise_retry_orig.over_noise < noise_orig.over_noise
                            || noise_retry_orig.over_count < noise_orig.over_count);

                    if (accept) {
                        int old_nbits = use_nbits_ch[gr2][ch2];
                        use_nbits_ch[gr2][ch2] = reduce_bit_usage(gfc, gr2, ch2);
                        use_nbits_gr[gr2] += (use_nbits_ch[gr2][ch2] - old_nbits);
                        use_nbits_fr += (use_nbits_ch[gr2][ch2] - old_nbits);
                        reject_reason = "accepted";
                    }
                    else {
                        FLOAT restore_distort[SFBMAX];
                        calc_noise_result noise_restore_orig;

                        *cod_info = saved_gi;
                        memcpy(sfwork_[gr2][ch2], saved_sfwork, sizeof(saved_sfwork));
                        memcpy(vbrsfmin_[gr2][ch2], saved_vbrsfmin,
                               sizeof(saved_vbrsfmin));

                        calc_noise(cod_info, l3_xmin[gr2][ch2], restore_distort,
                                   &noise_restore_orig, 0);
                        rollback_ok =
                            cod_info->part2_3_length == old_bits
                            && noise_restore_orig.over_count == noise_orig.over_count
                            && fabsf(noise_restore_orig.max_noise - noise_orig.max_noise) < 1e-6f
                            && fabsf(noise_restore_orig.over_noise - noise_orig.over_noise) < 1e-6f;

                        if (new_bits > MAX_BITS_PER_CHANNEL) {
                            reject_reason = "bit_limit";
                        }
                        else if (new_bits > old_bits) {
                            reject_reason = "more_bits";
                        }
                        else {
                            reject_reason = "no_orig_improvement";
                        }
                    }

                    if (cfg->analysis) {
                        fprintf(stderr,
                                "short_mask_relax_retry=1 short_mask_relax_accept=%d accept=%d "
                                "frame=%d gr=%d ch=%d psy_ch=%d source=%s "
                                "reject_reason=%s rollback_ok=%d "
                                "score_rel=%.2f final_mask=0x%02x "
                                "old_bits=%d new_bits=%d "
                                "old_max_noise_orig=%.2f new_max_noise_orig=%.2f "
                                "old_over_noise_orig=%.2f new_over_noise_orig=%.2f "
                                "old_over_count_orig=%d new_over_count_orig=%d "
                                "new_max_noise_relaxed=%.2f new_over_noise_relaxed=%.2f "
                                "new_over_count_relaxed=%d relax_db=%.2f relaxed_bands=%d\n",
                                accept, accept, gfc->ov_enc.frame_number, gr2, ch2,
                                psy_ch, short_mask_relax_source_name(psy_ch),
                                reject_reason, rollback_ok,
                                (double) score_rel, final_mask,
                                old_bits, new_bits,
                                (double) noise_orig.max_noise,
                                (double) noise_retry_orig.max_noise,
                                (double) noise_orig.over_noise,
                                (double) noise_retry_orig.over_noise,
                                noise_orig.over_count,
                                noise_retry_orig.over_count,
                                (double) noise_retry_relaxed.max_noise,
                                (double) noise_retry_relaxed.over_noise,
                                noise_retry_relaxed.over_count,
                                (double) SHORT_MASK_RELAX_DB, relaxed_bands);
                    }
                }
            }
        }
    }

    /* check bit constrains, but it should always be ok, iff there are no bugs ;-)
     */
    if (use_nbits_fr <= max_nbits_fr) {
        return use_nbits_fr;
    }

    ERRORF(gfc, "INTERNAL ERROR IN VBR NEW CODE (1313), please send bug report\n"
           "maxbits=%d usedbits=%d\n", max_nbits_fr, use_nbits_fr);
    exit(-1);
}
