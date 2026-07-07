#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <arm_neon.h>

#include "lame.h"
#include "machine.h"
#include "encoder.h"
#include "util.h"
#include "quantize_pvt.h"
#include "vector/lamer_dsp.h"

static void
neon_abs_f32(FLOAT *dst, FLOAT const *src, int n)
{
    int i = 0;

    for (; i + 4 <= n; i += 4) {
        float32x4_t const x = vld1q_f32(src + i);
        vst1q_f32(dst + i, vabsq_f32(x));
    }
    for (; i < n; ++i) {
        dst[i] = fabsf(src[i]);
    }
}

static FLOAT
neon_max_f32(FLOAT const *src, int n)
{
    float32x4_t maxv = vdupq_n_f32(0.0f);
    FLOAT tmp[4];
    FLOAT m = 0.0f;
    int i = 0;

    for (; i + 4 <= n; i += 4) {
        float32x4_t const x = vld1q_f32(src + i);
        maxv = vmaxq_f32(maxv, x);
    }
    vst1q_f32(tmp, maxv);
    if (m < tmp[0]) m = tmp[0];
    if (m < tmp[1]) m = tmp[1];
    if (m < tmp[2]) m = tmp[2];
    if (m < tmp[3]) m = tmp[3];
    for (; i < n; ++i) {
        if (m < src[i]) {
            m = src[i];
        }
    }
    return m;
}

static FLOAT
neon_sum_sq_f32(FLOAT const *src, int n)
{
    FLOAT tmp[4];
    FLOAT sum = 0.0f;
    int i = 0;

    for (; i + 4 <= n; i += 4) {
        float32x4_t x = vld1q_f32(src + i);
        x = vabsq_f32(x);
        x = vmulq_f32(x, x);
        vst1q_f32(tmp, x);
        sum += tmp[0];
        sum += tmp[1];
        sum += tmp[2];
        sum += tmp[3];
    }
    for (; i < n; ++i) {
        FLOAT const x = src[i];
        sum += x * x;
    }
    return sum;
}

static FLOAT
neon_dot_f32(FLOAT const *a, FLOAT const *b, int n)
{
    FLOAT tmp[4];
    FLOAT sum = 0.0f;
    int i = 0;

    for (; i + 4 <= n; i += 4) {
        float32x4_t const av = vld1q_f32(a + i);
        float32x4_t const bv = vld1q_f32(b + i);
        float32x4_t const p = vmulq_f32(av, bv);
        vst1q_f32(tmp, p);
        sum += tmp[0];
        sum += tmp[1];
        sum += tmp[2];
        sum += tmp[3];
    }
    for (; i < n; ++i) {
        sum += a[i] * b[i];
    }
    return sum;
}

static void
neon_window_mul_f32(FLOAT *dst, FLOAT const *src, FLOAT const *win, int n)
{
    int i = 0;

    for (; i + 4 <= n; i += 4) {
        float32x4_t const x = vld1q_f32(src + i);
        float32x4_t const w = vld1q_f32(win + i);
        vst1q_f32(dst + i, vmulq_f32(x, w));
    }
    for (; i < n; ++i) {
        dst[i] = src[i] * win[i];
    }
}

static void
neon_scalar_k_34_4(FLOAT x[4], int l3[4])
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
neon_vbr_calc_sfb_noise_x34(FLOAT const *xr, FLOAT const *xr34,
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
        float32x4_t scaled = vmulq_f32(vdupq_n_f32(sfpow34), vld1q_f32(xr34));
        vst1q_f32(x, scaled);
        neon_scalar_k_34_4(x, l3);
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
        neon_scalar_k_34_4(x, l3);
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
neon_vbr_quantize_x34(int *l3, FLOAT const *xr34, unsigned int bw, FLOAT sfpow34)
{
    FLOAT x[4];
    unsigned int i = bw >> 2u;
    unsigned int const remaining = bw & 0x03u;

    while (i-- > 0) {
        float32x4_t scaled = vmulq_f32(vdupq_n_f32(sfpow34), vld1q_f32(xr34));
        vst1q_f32(x, scaled);
        neon_scalar_k_34_4(x, l3);
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
        neon_scalar_k_34_4(x, tmp_l3);
        switch (remaining) {
        case 3: l3[2] = tmp_l3[2];
        case 2: l3[1] = tmp_l3[1];
        case 1: l3[0] = tmp_l3[0];
        }
    }
}

static void
neon_reconstructed_energy_f32(FLOAT const *xr, int const *ix,
                              FLOAT step, int n,
                              FLOAT *source_energy,
                              FLOAT *quant_energy)
{
    float32x4_t const stepv = vdupq_n_f32(step);
    FLOAT source_tmp[4];
    FLOAT quant_tmp[4];
    FLOAT pow_tmp[4];
    FLOAT source = 0.0f;
    FLOAT quant = 0.0f;
    int i = 0;

    for (; i + 4 <= n; i += 4) {
        float32x4_t x = vld1q_f32(xr + i);
        float32x4_t p;
        float32x4_t r;

        x = vabsq_f32(x);
        x = vmulq_f32(x, x);
        vst1q_f32(source_tmp, x);

        pow_tmp[0] = pow43[ix[i + 0]];
        pow_tmp[1] = pow43[ix[i + 1]];
        pow_tmp[2] = pow43[ix[i + 2]];
        pow_tmp[3] = pow43[ix[i + 3]];
        p = vld1q_f32(pow_tmp);
        r = vmulq_f32(p, stepv);
        r = vmulq_f32(r, r);
        vst1q_f32(quant_tmp, r);

        source += source_tmp[0];
        quant += quant_tmp[0];
        source += source_tmp[1];
        quant += quant_tmp[1];
        source += source_tmp[2];
        quant += quant_tmp[2];
        source += source_tmp[3];
        quant += quant_tmp[3];
    }

    for (; i < n; ++i) {
        FLOAT const orig = fabsf(xr[i]);
        FLOAT const recon = pow43[ix[i]] * step;

        source += orig * orig;
        quant += recon * recon;
    }

    *source_energy = source;
    *quant_energy = quant;
}

void
lamer_dsp_init_aarch64_neon(lamer_dsp *dsp)
{
    dsp->name = "neon";
    dsp->abs_f32 = neon_abs_f32;
    dsp->max_f32 = neon_max_f32;
    dsp->sum_sq_f32 = neon_sum_sq_f32;
    dsp->dot_f32 = neon_dot_f32;
    dsp->window_mul_f32 = neon_window_mul_f32;
    dsp->reconstructed_energy_f32 = neon_reconstructed_energy_f32;
    dsp->vbr_calc_sfb_noise_x34 = neon_vbr_calc_sfb_noise_x34;
    dsp->vbr_quantize_x34 = neon_vbr_quantize_x34;
}
