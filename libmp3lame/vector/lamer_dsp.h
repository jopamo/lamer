#ifndef LAMER_DSP_H
#define LAMER_DSP_H

#include "machine.h"

/*
 * Internal SIMD policy:
 *
 * Class A helpers must be byte-for-byte safe against scalar when they are used
 * in encoder-visible paths.
 * Class B helpers may change floating-point reductions and must be corpus
 * validated before becoming the default.
 * Class C helpers feed decisions.  If SIMD is used there, keep scalar decision
 * order or treat the path as intentional output-changing encoder behavior.
 */
typedef enum lamer_dsp_helper_class_e {
    LAMER_DSP_CLASS_A_BIT_EXACT = 1,
    LAMER_DSP_CLASS_B_NUMERICALLY_CLOSE = 2,
    LAMER_DSP_CLASS_C_DECISION_PATH = 3
} lamer_dsp_helper_class_t;

typedef struct lamer_dsp_s {
    char const *name;

    void  (*abs_f32)(FLOAT *dst, FLOAT const *src, int n);
    FLOAT (*max_f32)(FLOAT const *src, int n);
    FLOAT (*sum_sq_f32)(FLOAT const *src, int n);
    FLOAT (*dot_f32)(FLOAT const *a, FLOAT const *b, int n);
    void  (*window_mul_f32)(FLOAT *dst, FLOAT const *src,
                            FLOAT const *win, int n);

    /*
     * Reconstructed-energy helper for steady-tonal metrics.
     * This is a decision-path helper.  Optimized implementations compute
     * per-sample terms with SIMD but accumulate in scalar index order, matching
     * the scalar reduction order instead of doing horizontal vector reductions.
     */
    void (*reconstructed_energy_f32)(FLOAT const *xr, int const *ix,
                                     FLOAT step, int n,
                                     FLOAT *source_energy,
                                     FLOAT *quant_energy);

    /*
     * VBR quantize support.  These are decision-path helpers.  Implementations
     * must preserve scalar conversion and reduction order, or stay scalar.
     */
    FLOAT (*vbr_calc_sfb_noise_x34)(FLOAT const *xr, FLOAT const *xr34,
                                    unsigned int bw, uint8_t sf);
    void (*vbr_quantize_x34)(int *l3, FLOAT const *xr34,
                             unsigned int bw, FLOAT sfpow34);
} lamer_dsp;

void lamer_dsp_init(lamer_dsp *dsp);
void lamer_dsp_init_scalar(lamer_dsp *dsp);

#endif
