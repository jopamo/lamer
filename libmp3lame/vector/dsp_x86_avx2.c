#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <immintrin.h>
#include <stdlib.h>
#include <string.h>

#include "encoder.h"
#include "lame.h"
#include "machine.h"
#include "quantize_pvt.h"
#include "util.h"
#include "vector/lamer_dsp.h"

static int avx2_experimental_quant_simd_enabled(void) {
  char const *const env = getenv("LAMER_SIMD_EXPERIMENTAL_QUANT");

  return env != 0 && *env != '\0' && strcmp(env, "0") != 0 &&
         strcmp(env, "off") != 0 && strcmp(env, "false") != 0 &&
         strcmp(env, "disabled") != 0;
}

static void avx2_abs_f32(FLOAT *dst, FLOAT const *src, int n) {
  __m256 const abs_mask = _mm256_castsi256_ps(_mm256_set1_epi32(0x7fffffff));
  int i = 0;

  for (; i + 8 <= n; i += 8) {
    __m256 const x = _mm256_loadu_ps(src + i);
    _mm256_storeu_ps(dst + i, _mm256_and_ps(x, abs_mask));
  }
  for (; i < n; ++i) {
    dst[i] = fabsf(src[i]);
  }
  _mm256_zeroupper();
}

static FLOAT avx2_abs_max_f32(FLOAT const *src, int n, FLOAT floor) {
  __m256 const abs_mask = _mm256_castsi256_ps(_mm256_set1_epi32(0x7fffffff));
  __m256 maxv = _mm256_set1_ps(floor);
  FLOAT tmp[8];
  FLOAT m = floor;
  int i = 0;

  for (; i + 8 <= n; i += 8) {
    __m256 const x = _mm256_and_ps(_mm256_loadu_ps(src + i), abs_mask);
    maxv = _mm256_max_ps(maxv, x);
  }
  _mm256_storeu_ps(tmp, maxv);
  if (m < tmp[0])
    m = tmp[0];
  if (m < tmp[1])
    m = tmp[1];
  if (m < tmp[2])
    m = tmp[2];
  if (m < tmp[3])
    m = tmp[3];
  if (m < tmp[4])
    m = tmp[4];
  if (m < tmp[5])
    m = tmp[5];
  if (m < tmp[6])
    m = tmp[6];
  if (m < tmp[7])
    m = tmp[7];
  for (; i < n; ++i) {
    FLOAT const x = fabsf(src[i]);
    if (m < x) {
      m = x;
    }
  }
  _mm256_zeroupper();
  return m;
}

static FLOAT avx2_max_f32(FLOAT const *src, int n) {
  __m256 maxv = _mm256_setzero_ps();
  FLOAT tmp[8];
  FLOAT m = 0.0f;
  int i = 0;

  for (; i + 8 <= n; i += 8) {
    __m256 const x = _mm256_loadu_ps(src + i);
    maxv = _mm256_max_ps(maxv, x);
  }
  _mm256_storeu_ps(tmp, maxv);
  if (m < tmp[0])
    m = tmp[0];
  if (m < tmp[1])
    m = tmp[1];
  if (m < tmp[2])
    m = tmp[2];
  if (m < tmp[3])
    m = tmp[3];
  if (m < tmp[4])
    m = tmp[4];
  if (m < tmp[5])
    m = tmp[5];
  if (m < tmp[6])
    m = tmp[6];
  if (m < tmp[7])
    m = tmp[7];
  for (; i < n; ++i) {
    if (m < src[i]) {
      m = src[i];
    }
  }
  _mm256_zeroupper();
  return m;
}

static FLOAT avx2_sum_sq_f32(FLOAT const *src, int n) {
  __m256 const abs_mask = _mm256_castsi256_ps(_mm256_set1_epi32(0x7fffffff));
  FLOAT tmp[8];
  FLOAT sum = 0.0f;
  int i = 0;

  for (; i + 8 <= n; i += 8) {
    __m256 x = _mm256_loadu_ps(src + i);
    x = _mm256_and_ps(x, abs_mask);
    x = _mm256_mul_ps(x, x);
    _mm256_storeu_ps(tmp, x);
    sum += tmp[0];
    sum += tmp[1];
    sum += tmp[2];
    sum += tmp[3];
    sum += tmp[4];
    sum += tmp[5];
    sum += tmp[6];
    sum += tmp[7];
  }
  for (; i < n; ++i) {
    FLOAT const x = src[i];
    sum += x * x;
  }
  _mm256_zeroupper();
  return sum;
}

static FLOAT avx2_dot_f32(FLOAT const *a, FLOAT const *b, int n) {
  FLOAT tmp[8];
  FLOAT sum = 0.0f;
  int i = 0;

  for (; i + 8 <= n; i += 8) {
    __m256 const av = _mm256_loadu_ps(a + i);
    __m256 const bv = _mm256_loadu_ps(b + i);
    __m256 const p = _mm256_mul_ps(av, bv);
    _mm256_storeu_ps(tmp, p);
    sum += tmp[0];
    sum += tmp[1];
    sum += tmp[2];
    sum += tmp[3];
    sum += tmp[4];
    sum += tmp[5];
    sum += tmp[6];
    sum += tmp[7];
  }
  for (; i < n; ++i) {
    sum += a[i] * b[i];
  }
  _mm256_zeroupper();
  return sum;
}

static void avx2_window_mul_f32(FLOAT *dst, FLOAT const *src, FLOAT const *win,
                                int n) {
  int i = 0;

  for (; i + 8 <= n; i += 8) {
    __m256 const x = _mm256_loadu_ps(src + i);
    __m256 const w = _mm256_loadu_ps(win + i);
    _mm256_storeu_ps(dst + i, _mm256_mul_ps(x, w));
  }
  for (; i < n; ++i) {
    dst[i] = src[i] * win[i];
  }
  _mm256_zeroupper();
}

static void avx2_psy_attack_hpf_f32(FLOAT *dst, FLOAT const *src, int n,
                                    FLOAT const *coef) {
  int i = 0;

  for (; i + 8 <= n; i += 8) {
    __m256 sum1 = _mm256_loadu_ps(src + i + 10);
    __m256 sum2 = _mm256_setzero_ps();
    __m256 a, b, c;
    int j;

    for (j = 0; j < 9; j += 2) {
      a = _mm256_loadu_ps(src + i + j);
      b = _mm256_loadu_ps(src + i + 21 - j);
      c = _mm256_set1_ps(coef[j]);
      sum1 = _mm256_add_ps(sum1, _mm256_mul_ps(c, _mm256_add_ps(a, b)));

      a = _mm256_loadu_ps(src + i + j + 1);
      b = _mm256_loadu_ps(src + i + 20 - j);
      c = _mm256_set1_ps(coef[j + 1]);
      sum2 = _mm256_add_ps(sum2, _mm256_mul_ps(c, _mm256_add_ps(a, b)));
    }
    _mm256_storeu_ps(dst + i, _mm256_add_ps(sum1, sum2));
  }
  if (i + 4 <= n) {
    __m128 sum1 = _mm_loadu_ps(src + i + 10);
    __m128 sum2 = _mm_setzero_ps();
    __m128 a, b, c;
    int j;

    for (j = 0; j < 9; j += 2) {
      a = _mm_loadu_ps(src + i + j);
      b = _mm_loadu_ps(src + i + 21 - j);
      c = _mm_set1_ps(coef[j]);
      sum1 = _mm_add_ps(sum1, _mm_mul_ps(c, _mm_add_ps(a, b)));

      a = _mm_loadu_ps(src + i + j + 1);
      b = _mm_loadu_ps(src + i + 20 - j);
      c = _mm_set1_ps(coef[j + 1]);
      sum2 = _mm_add_ps(sum2, _mm_mul_ps(c, _mm_add_ps(a, b)));
    }
    _mm_storeu_ps(dst + i, _mm_add_ps(sum1, sum2));
    i += 4;
  }
  for (; i < n; ++i) {
    FLOAT sum1 = src[i + 10];
    FLOAT sum2 = 0.0f;
    int j;

    for (j = 0; j < 9; j += 2) {
      sum1 += coef[j] * (src[i + j] + src[i + 21 - j]);
      sum2 += coef[j + 1] * (src[i + j + 1] + src[i + 20 - j]);
    }
    dst[i] = sum1 + sum2;
  }
  _mm256_zeroupper();
}

static void avx2_vbr_k_34_4(__m128 scaled, int idx[4]) {
  FLOAT adj_tmp[4];
  __m128i idxv = _mm_cvttps_epi32(scaled);

  _mm_storeu_si128((__m128i *)idx, idxv);
  adj_tmp[0] = adj43[idx[0]];
  adj_tmp[1] = adj43[idx[1]];
  adj_tmp[2] = adj43[idx[2]];
  adj_tmp[3] = adj43[idx[3]];
  scaled = _mm_add_ps(scaled, _mm_loadu_ps(adj_tmp));
  idxv = _mm_cvttps_epi32(scaled);
  _mm_storeu_si128((__m128i *)idx, idxv);
}

static FLOAT avx2_vbr_calc_sfb_noise_x34(FLOAT const *xr, FLOAT const *xr34,
                                         unsigned int bw, uint8_t sf) {
  __m128 const abs_mask = _mm_castsi128_ps(_mm_set1_epi32(0x7fffffff));
  __m128 const sfpowv = _mm_set1_ps(pow20[sf + Q_MAX2]);
  __m128 const sfpow34v = _mm_set1_ps(ipow20[sf]);
  FLOAT diff[4];
  FLOAT pow_tmp[4];
  int idx[4];
  FLOAT xfsf = 0.0f;
  unsigned int i = bw >> 2u;
  unsigned int const remaining = bw & 0x03u;

  while (i-- > 0) {
    __m128 scaled = _mm_mul_ps(sfpow34v, _mm_loadu_ps(xr34));
    __m128 recon;
    __m128 orig;
    __m128 d;

    avx2_vbr_k_34_4(scaled, idx);

    pow_tmp[0] = pow43[idx[0]];
    pow_tmp[1] = pow43[idx[1]];
    pow_tmp[2] = pow43[idx[2]];
    pow_tmp[3] = pow43[idx[3]];
    recon = _mm_mul_ps(sfpowv, _mm_loadu_ps(pow_tmp));
    orig = _mm_and_ps(_mm_loadu_ps(xr), abs_mask);
    d = _mm_sub_ps(orig, recon);
    _mm_storeu_ps(diff, d);
    xfsf += (diff[0] * diff[0] + diff[1] * diff[1]) +
            (diff[2] * diff[2] + diff[3] * diff[3]);

    xr += 4;
    xr34 += 4;
  }

  if (remaining) {
    FLOAT x[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    switch (remaining) {
    case 3:
      x[2] = ipow20[sf] * xr34[2];
    case 2:
      x[1] = ipow20[sf] * xr34[1];
    case 1:
      x[0] = ipow20[sf] * xr34[0];
    }

    avx2_vbr_k_34_4(_mm_loadu_ps(x), idx);
    x[0] = x[1] = x[2] = x[3] = 0.0f;

    switch (remaining) {
    case 3:
      x[2] = fabsf(xr[2]) - pow20[sf + Q_MAX2] * pow43[idx[2]];
    case 2:
      x[1] = fabsf(xr[1]) - pow20[sf + Q_MAX2] * pow43[idx[1]];
    case 1:
      x[0] = fabsf(xr[0]) - pow20[sf + Q_MAX2] * pow43[idx[0]];
    }
    xfsf += (x[0] * x[0] + x[1] * x[1]) + (x[2] * x[2] + x[3] * x[3]);
  }

  return xfsf;
}

static void avx2_vbr_quantize_x34(int *l3, FLOAT const *xr34, unsigned int bw,
                                  FLOAT sfpow34) {
  __m128 const sfpow34v = _mm_set1_ps(sfpow34);
  unsigned int i = bw >> 2u;
  unsigned int const remaining = bw & 0x03u;

  while (i-- > 0) {
    int idx[4];
    __m128 const scaled = _mm_mul_ps(sfpow34v, _mm_loadu_ps(xr34));

    avx2_vbr_k_34_4(scaled, idx);
    l3[0] = idx[0];
    l3[1] = idx[1];
    l3[2] = idx[2];
    l3[3] = idx[3];

    l3 += 4;
    xr34 += 4;
  }

  if (remaining) {
    FLOAT x[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    int idx[4];
    switch (remaining) {
    case 3:
      x[2] = sfpow34 * xr34[2];
    case 2:
      x[1] = sfpow34 * xr34[1];
    case 1:
      x[0] = sfpow34 * xr34[0];
    }

    avx2_vbr_k_34_4(_mm_loadu_ps(x), idx);

    switch (remaining) {
    case 3:
      l3[2] = idx[2];
    case 2:
      l3[1] = idx[1];
    case 1:
      l3[0] = idx[0];
    }
  }
}

static void avx2_reconstructed_energy_f32(FLOAT const *xr, int const *ix,
                                          FLOAT step, int n,
                                          FLOAT *source_energy,
                                          FLOAT *quant_energy) {
  __m256 const abs_mask = _mm256_castsi256_ps(_mm256_set1_epi32(0x7fffffff));
  __m256 const stepv = _mm256_set1_ps(step);
  FLOAT source_tmp[8];
  FLOAT quant_tmp[8];
  FLOAT source = 0.0f;
  FLOAT quant = 0.0f;
  int i = 0;

  for (; i + 8 <= n; i += 8) {
    __m256 x = _mm256_loadu_ps(xr + i);
    __m256i const idx = _mm256_loadu_si256((__m256i const *)(ix + i));
    __m256 p;
    __m256 r;

    x = _mm256_and_ps(x, abs_mask);
    x = _mm256_mul_ps(x, x);
    _mm256_storeu_ps(source_tmp, x);

    p = _mm256_i32gather_ps(pow43, idx, 4);
    r = _mm256_mul_ps(p, stepv);
    r = _mm256_mul_ps(r, r);
    _mm256_storeu_ps(quant_tmp, r);

    source += source_tmp[0];
    quant += quant_tmp[0];
    source += source_tmp[1];
    quant += quant_tmp[1];
    source += source_tmp[2];
    quant += quant_tmp[2];
    source += source_tmp[3];
    quant += quant_tmp[3];
    source += source_tmp[4];
    quant += quant_tmp[4];
    source += source_tmp[5];
    quant += quant_tmp[5];
    source += source_tmp[6];
    quant += quant_tmp[6];
    source += source_tmp[7];
    quant += quant_tmp[7];
  }

  for (; i < n; ++i) {
    FLOAT const orig = fabsf(xr[i]);
    FLOAT const recon = pow43[ix[i]] * step;

    source += orig * orig;
    quant += recon * recon;
  }

  *source_energy = source;
  *quant_energy = quant;
  _mm256_zeroupper();
}

void lamer_dsp_init_x86_avx2(lamer_dsp *dsp) {
  dsp->name = "avx2";
  dsp->abs_f32 = avx2_abs_f32;
  dsp->abs_max_f32 = avx2_abs_max_f32;
  dsp->max_f32 = avx2_max_f32;
  dsp->sum_sq_f32 = avx2_sum_sq_f32;
  dsp->dot_f32 = avx2_dot_f32;
  dsp->window_mul_f32 = avx2_window_mul_f32;
  dsp->psy_attack_hpf_f32 = avx2_psy_attack_hpf_f32;
  dsp->reconstructed_energy_f32 = avx2_reconstructed_energy_f32;
  if (avx2_experimental_quant_simd_enabled()) {
    dsp->vbr_calc_sfb_noise_x34 = avx2_vbr_calc_sfb_noise_x34;
    dsp->vbr_quantize_x34 = avx2_vbr_quantize_x34;
  }
}
