#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdlib.h>
#include <string.h>

#include "lame.h"
#include "machine.h"
#include "encoder.h"
#include "util.h"
#include "quantize_pvt.h"
#include "vector/lamer_dsp.h"

#ifdef LAMER_HAVE_X86_SSE2
void lamer_dsp_init_x86_sse2(lamer_dsp *dsp);
#endif

#ifdef LAMER_HAVE_X86_AVX2
void lamer_dsp_init_x86_avx2(lamer_dsp *dsp);
#endif

#ifdef LAMER_HAVE_AARCH64_NEON
void lamer_dsp_init_aarch64_neon(lamer_dsp *dsp);
#endif

static void
scalar_abs_f32(FLOAT *dst, FLOAT const *src, int n)
{
    int i;
    for (i = 0; i < n; ++i) {
        dst[i] = fabsf(src[i]);
    }
}

static FLOAT
scalar_abs_max_f32(FLOAT const *src, int n, FLOAT floor)
{
    FLOAT m = floor;
    int i;
    for (i = 0; i < n; ++i) {
        FLOAT const x = fabsf(src[i]);
        if (m < x) {
            m = x;
        }
    }
    return m;
}

static FLOAT
scalar_max_f32(FLOAT const *src, int n)
{
    FLOAT m = 0.0f;
    int i;
    for (i = 0; i < n; ++i) {
        if (m < src[i]) {
            m = src[i];
        }
    }
    return m;
}

static FLOAT
scalar_sum_sq_f32(FLOAT const *src, int n)
{
    FLOAT sum = 0.0f;
    int i;
    for (i = 0; i < n; ++i) {
        FLOAT const x = src[i];
        sum += x * x;
    }
    return sum;
}

static FLOAT
scalar_dot_f32(FLOAT const *a, FLOAT const *b, int n)
{
    FLOAT sum = 0.0f;
    int i;
    for (i = 0; i < n; ++i) {
        sum += a[i] * b[i];
    }
    return sum;
}

static void
scalar_window_mul_f32(FLOAT *dst, FLOAT const *src, FLOAT const *win, int n)
{
    int i;
    for (i = 0; i < n; ++i) {
        dst[i] = src[i] * win[i];
    }
}

static void
scalar_psy_attack_hpf_f32(FLOAT *dst, FLOAT const *src, int n,
                          FLOAT const *coef)
{
    int i;

    for (i = 0; i < n; ++i) {
        FLOAT sum1 = src[i + 10];
        FLOAT sum2 = 0.0f;
        int j;

        for (j = 0; j < 9; j += 2) {
            sum1 += coef[j] * (src[i + j] + src[i + 21 - j]);
            sum2 += coef[j + 1] * (src[i + j + 1] + src[i + 20 - j]);
        }
        dst[i] = sum1 + sum2;
    }
}

static void
scalar_k_34_4(FLOAT x[4], int l3[4])
{
    l3[0] = (int) x[0];
    l3[1] = (int) x[1];
    l3[2] = (int) x[2];
    l3[3] = (int) x[3];
    x[0] += adj43[l3[0]];
    x[1] += adj43[l3[1]];
    x[2] += adj43[l3[2]];
    x[3] += adj43[l3[3]];
    l3[0] = (int) x[0];
    l3[1] = (int) x[1];
    l3[2] = (int) x[2];
    l3[3] = (int) x[3];
}

static FLOAT
scalar_vbr_calc_sfb_noise_x34(FLOAT const *xr, FLOAT const *xr34,
                              unsigned int bw, uint8_t sf)
{
    FLOAT x[4];
    int l3[4];
    FLOAT const sfpow = pow20[sf + Q_MAX2];
    FLOAT const sfpow34 = ipow20[sf];
    FLOAT xfsf = 0.0f;
    unsigned int i = bw >> 2u;
    unsigned int const remaining = bw & 0x03u;

    while (i-- > 0) {
        x[0] = sfpow34 * xr34[0];
        x[1] = sfpow34 * xr34[1];
        x[2] = sfpow34 * xr34[2];
        x[3] = sfpow34 * xr34[3];

        scalar_k_34_4(x, l3);

        x[0] = fabsf(xr[0]) - sfpow * pow43[l3[0]];
        x[1] = fabsf(xr[1]) - sfpow * pow43[l3[1]];
        x[2] = fabsf(xr[2]) - sfpow * pow43[l3[2]];
        x[3] = fabsf(xr[3]) - sfpow * pow43[l3[3]];
        xfsf += (x[0] * x[0] + x[1] * x[1]) + (x[2] * x[2] + x[3] * x[3]);

        xr += 4;
        xr34 += 4;
    }
    if (remaining) {
        x[0] = x[1] = x[2] = x[3] = 0.0f;
        switch (remaining) {
        case 3: x[2] = sfpow34 * xr34[2];
        case 2: x[1] = sfpow34 * xr34[1];
        case 1: x[0] = sfpow34 * xr34[0];
        }

        scalar_k_34_4(x, l3);
        x[0] = x[1] = x[2] = x[3] = 0.0f;

        switch (remaining) {
        case 3: x[2] = fabsf(xr[2]) - sfpow * pow43[l3[2]];
        case 2: x[1] = fabsf(xr[1]) - sfpow * pow43[l3[1]];
        case 1: x[0] = fabsf(xr[0]) - sfpow * pow43[l3[0]];
        }
        xfsf += (x[0] * x[0] + x[1] * x[1]) + (x[2] * x[2] + x[3] * x[3]);
    }
    return xfsf;
}

static void
scalar_vbr_quantize_x34(int *l3, FLOAT const *xr34, unsigned int bw, FLOAT sfpow34)
{
    FLOAT x[4];
    unsigned int i = bw >> 2u;
    unsigned int const remaining = bw & 0x03u;

    while (i-- > 0) {
        x[0] = sfpow34 * xr34[0];
        x[1] = sfpow34 * xr34[1];
        x[2] = sfpow34 * xr34[2];
        x[3] = sfpow34 * xr34[3];

        scalar_k_34_4(x, l3);

        l3 += 4;
        xr34 += 4;
    }
    if (remaining) {
        int tmp_l3[4];
        x[0] = x[1] = x[2] = x[3] = 0.0f;
        switch (remaining) {
        case 3: x[2] = sfpow34 * xr34[2];
        case 2: x[1] = sfpow34 * xr34[1];
        case 1: x[0] = sfpow34 * xr34[0];
        }

        scalar_k_34_4(x, tmp_l3);

        switch (remaining) {
        case 3: l3[2] = tmp_l3[2];
        case 2: l3[1] = tmp_l3[1];
        case 1: l3[0] = tmp_l3[0];
        }
    }
}

static void
scalar_reconstructed_energy_f32(FLOAT const *xr, int const *ix,
                                FLOAT step, int n,
                                FLOAT *source_energy,
                                FLOAT *quant_energy)
{
    FLOAT source = 0.0f;
    FLOAT quant = 0.0f;
    int i;

    for (i = 0; i < n; ++i) {
        FLOAT const orig = fabsf(xr[i]);
        FLOAT const recon = pow43[ix[i]] * step;

        source += orig * orig;
        quant += recon * recon;
    }

    *source_energy = source;
    *quant_energy = quant;
}

void
lamer_dsp_init_scalar(lamer_dsp *dsp)
{
    dsp->name = "scalar";
    dsp->abs_f32 = scalar_abs_f32;
    dsp->abs_max_f32 = scalar_abs_max_f32;
    dsp->max_f32 = scalar_max_f32;
    dsp->sum_sq_f32 = scalar_sum_sq_f32;
    dsp->dot_f32 = scalar_dot_f32;
    dsp->window_mul_f32 = scalar_window_mul_f32;
    dsp->psy_attack_hpf_f32 = scalar_psy_attack_hpf_f32;
    dsp->reconstructed_energy_f32 = scalar_reconstructed_energy_f32;
    dsp->vbr_calc_sfb_noise_x34 = scalar_vbr_calc_sfb_noise_x34;
    dsp->vbr_quantize_x34 = scalar_vbr_quantize_x34;
}

#ifdef LAMER_ENABLE_SIMD
static int
simd_env_disabled(char const *env)
{
    return env != 0
        && (strcmp(env, "0") == 0
            || strcmp(env, "off") == 0
            || strcmp(env, "false") == 0
            || strcmp(env, "disabled") == 0
            || strcmp(env, "scalar") == 0);
}

static int
simd_env_allows(char const *env, char const *backend)
{
    return env == 0
        || *env == '\0'
        || strcmp(env, "auto") == 0
        || strcmp(env, backend) == 0;
}

# ifdef LAMER_HAVE_X86_AVX2
static int
x86_cpu_supports_avx2(void)
{
#  if (defined(__GNUC__) || defined(__clang__)) \
    && (defined(__x86_64__) || defined(__i386__))
    __builtin_cpu_init();
    return __builtin_cpu_supports("avx2") != 0;
#  else
    return 0;
#  endif
}
# endif
#endif

void
lamer_dsp_init(lamer_dsp *dsp)
{
    lamer_dsp_init_scalar(dsp);

#ifdef LAMER_ENABLE_SIMD
    char const *const env = getenv("LAMER_SIMD");

    if (simd_env_disabled(env)) {
        return;
    }

# ifdef LAMER_HAVE_X86_AVX2
    if (simd_env_allows(env, "avx2") && x86_cpu_supports_avx2()) {
        lamer_dsp_init_x86_avx2(dsp);
        return;
    }
# endif

# ifdef LAMER_HAVE_X86_SSE2
    if (simd_env_allows(env, "sse2")) {
        lamer_dsp_init_x86_sse2(dsp);
        return;
    }
# endif

# ifdef LAMER_HAVE_AARCH64_NEON
    if (simd_env_allows(env, "neon")) {
        lamer_dsp_init_aarch64_neon(dsp);
        return;
    }
# endif
#endif
}
