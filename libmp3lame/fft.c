/*
 * Fixed-size Hartley transforms for psychoacoustic analysis.
 *
 * Uses conventional radix-4 butterflies with directly initialized
 * trigonometric lookup tables.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "lame.h"
#include "machine.h"
#include "encoder.h"
#include "util.h"
#include "fft.h"

#define FFT_QUARTER (BLKSIZE / 4)

/*
 * Initialization-only helper.  Keeping permutation generation out of the
 * transform removes the old hard-coded reversal table without putting any
 * bit-manipulation work in the hot path.
 */
static unsigned char reverse_u8(unsigned int x) {
    unsigned int r = 0;
    int i;

    for (i = 0; i < 8; ++i) {
        r = (r << 1) | (x & 1u);
        x >>= 1;
    }

    return (unsigned char)r;
}

/*
 * Initial radix-4 butterfly.
 *
 * The caller supplies samples in the order required by the real Hartley
 * decomposition:
 *
 *     x0, x[N/2], x[N/4], x[3N/4]
 */
static inline void radix4_seed(FLOAT* restrict dst, FLOAT x0, FLOAT x1, FLOAT x2, FLOAT x3) {
    const FLOAT a0 = x0 + x1;
    const FLOAT a1 = x0 - x1;
    const FLOAT a2 = x2 + x3;
    const FLOAT a3 = x2 - x3;

    dst[0] = a0 + a2;
    dst[2] = a0 - a2;
    dst[1] = a1 + a3;
    dst[3] = a1 - a3;
}

/*
 * In-place radix-4 discrete Hartley transform.
 *
 * Input has already undergone the first radix-4 stage and permutation in
 * fft_long()/fft_short().
 *
 * sin_q[k] = sin(2*pi*k/BLKSIZE), 0 <= k <= BLKSIZE/4.
 *
 * cosine values are obtained from:
 *
 *     cos(theta) = sin(pi/2 - theta)
 *
 * so only one small table is required.
 */
static void dht_radix4(FLOAT* restrict x, int n, const FLOAT* restrict sin_q) {
    int span;

    assert(n == BLKSIZE || n == BLKSIZE_s);

    for (span = 16; span <= n; span <<= 2) {
        const int quarter = span >> 2;
        const int half = span >> 1;
        const int three_quarter = quarter + half;
        const int eighth = quarter >> 1;

        /*
         * Twiddle table is expressed on the 1024-point master grid.
         * All transform lengths used here divide BLKSIZE exactly.
         */
        const int twiddle_step = BLKSIZE / span;

        int base;

        for (base = 0; base < n; base += span) {
            FLOAT* const p = x + base;
            int i;

            /*
             * Zero-angle butterfly.
             */
            {
                const FLOAT x0 = p[0];
                const FLOAT x1 = p[quarter];
                const FLOAT x2 = p[half];
                const FLOAT x3 = p[three_quarter];

                const FLOAT a0 = x0 + x1;
                const FLOAT a1 = x0 - x1;
                const FLOAT a2 = x2 + x3;
                const FLOAT a3 = x2 - x3;

                p[0] = a0 + a2;
                p[half] = a0 - a2;
                p[quarter] = a1 + a3;
                p[three_quarter] = a1 - a3;
            }

            /*
             * pi/4 butterfly.
             */
            {
                FLOAT* const q = p + eighth;

                const FLOAT x0 = q[0];
                const FLOAT x1 = q[quarter];
                const FLOAT x2 = q[half];
                const FLOAT x3 = q[three_quarter];

                const FLOAT a0 = x0 + x1;
                const FLOAT a1 = x0 - x1;
                const FLOAT a2 = SQRT2 * x2;
                const FLOAT a3 = SQRT2 * x3;

                q[0] = a0 + a2;
                q[half] = a0 - a2;
                q[quarter] = a1 + a3;
                q[three_quarter] = a1 - a3;
            }

            /*
             * General conjugate-pair butterflies.
             *
             * Looping over one contiguous radix-4 block at a time gives the
             * compiler much cleaner alias/access information than the old
             * strided outer loop.
             */
            for (i = 1; i < eighth; ++i) {
                const int t1 = i * twiddle_step;
                const int t2 = t1 << 1;

                const FLOAT s1 = sin_q[t1];
                const FLOAT c1 = sin_q[FFT_QUARTER - t1];
                const FLOAT s2 = sin_q[t2];
                const FLOAT c2 = sin_q[FFT_QUARTER - t2];

                FLOAT* const f = p + i;
                FLOAT* const g = p + quarter - i;

                FLOAT a;
                FLOAT b;
                FLOAT f0;
                FLOAT f1;
                FLOAT f2;
                FLOAT f3;
                FLOAT g0;
                FLOAT g1;
                FLOAT g2;
                FLOAT g3;

                b = s2 * f[quarter] - c2 * g[quarter];
                a = c2 * f[quarter] + s2 * g[quarter];

                f1 = f[0] - a;
                f0 = f[0] + a;
                g1 = g[0] - b;
                g0 = g[0] + b;

                b = s2 * f[three_quarter] - c2 * g[three_quarter];
                a = c2 * f[three_quarter] + s2 * g[three_quarter];

                f3 = f[half] - a;
                f2 = f[half] + a;
                g3 = g[half] - b;
                g2 = g[half] + b;

                b = s1 * f2 - c1 * g3;
                a = c1 * f2 + s1 * g3;

                f[half] = f0 - a;
                f[0] = f0 + a;
                g[three_quarter] = g1 - b;
                g[quarter] = g1 + b;

                b = c1 * g2 - s1 * f3;
                a = s1 * g2 + c1 * f3;

                g[half] = g0 - a;
                g[0] = g0 + a;
                f[three_quarter] = f1 - b;
                f[quarter] = f1 + b;
            }
        }
    }
}

void fft_short(lame_internal_flags const* const gfc, FLOAT x_real[3][BLKSIZE_s], int chn, const sample_t* const buffer[2]) {
    const PsyConst_t* const psy = gfc->cd_psy;
    const FLOAT* const window_s = psy->window_s;
    const unsigned char* const rev = psy->fft_rev8;
    const sample_t* const src = buffer[chn];

    int b;

    for (b = 0; b < 3; ++b) {
        FLOAT* const x = x_real[b];
        const int k = (576 / 3) * (b + 1);
        int j;

        /*
         * Perform the permutation and first radix-4 stage together.
         * Destinations are now traversed forwards rather than backwards.
         */
        for (j = 0; j < BLKSIZE_s / 8; ++j) {
            const int i = rev[j << 2];
            FLOAT* const dst = x + (j << 2);

            radix4_seed(dst, window_s[i] * src[k + i], window_s[0x7f - i] * src[k + i + 0x80], window_s[i + 0x40] * src[k + i + 0x40], window_s[0x3f - i] * src[k + i + 0xc0]);

            radix4_seed(dst + BLKSIZE_s / 2, window_s[i + 1] * src[k + i + 1], window_s[0x7e - i] * src[k + i + 0x81], window_s[i + 0x41] * src[k + i + 0x41], window_s[0x3e - i] * src[k + i + 0xc1]);
        }

        dht_radix4(x, BLKSIZE_s, psy->fft_sin_q);
    }
}

void fft_long(lame_internal_flags const* const gfc, FLOAT x[BLKSIZE], int chn, const sample_t* const buffer[2]) {
    const PsyConst_t* const psy = gfc->cd_psy;
    const FLOAT* const window = psy->window;
    const unsigned char* const rev = psy->fft_rev8;
    const sample_t* const src = buffer[chn];

    int j;

    /*
     * Fuse window multiplication, permutation and the initial radix-4
     * butterfly.
     */
    for (j = 0; j < BLKSIZE / 8; ++j) {
        const int i = rev[j];
        FLOAT* const dst = x + (j << 2);

        radix4_seed(dst, window[i] * src[i], window[i + 0x200] * src[i + 0x200], window[i + 0x100] * src[i + 0x100], window[i + 0x300] * src[i + 0x300]);

        radix4_seed(dst + BLKSIZE / 2, window[i + 0x001] * src[i + 0x001], window[i + 0x201] * src[i + 0x201], window[i + 0x101] * src[i + 0x101], window[i + 0x301] * src[i + 0x301]);
    }

    dht_radix4(x, BLKSIZE, psy->fft_sin_q);
}

void init_fft(lame_internal_flags* const gfc) {
    PsyConst_t* const psy = gfc->cd_psy;
    int i;

    /*
     * Long-block Blackman window.
     */
    for (i = 0; i < BLKSIZE; ++i) {
        psy->window[i] = 0.42 - 0.5 * cos(2.0 * PI * (i + 0.5) / BLKSIZE) + 0.08 * cos(4.0 * PI * (i + 0.5) / BLKSIZE);
    }

    /*
     * Only half of the symmetric short-block window needs storage.
     */
    for (i = 0; i < BLKSIZE_s / 2; ++i) {
        psy->window_s[i] = 0.5 * (1.0 - cos(2.0 * PI * (i + 0.5) / BLKSIZE_s));
    }

    /*
     * Directly initialized twiddles.
     *
     * No iterative/Buneman-style trigonometric generator is used in
     * the transform hot path.
     */
    for (i = 0; i <= FFT_QUARTER; ++i) {
        psy->fft_sin_q[i] = (FLOAT)sin(2.0 * PI * i / BLKSIZE);
    }

    /*
     * Generate the tiny permutation once per encoder instance rather
     * than carrying the legacy hard-coded table.
     */
    for (i = 0; i < BLKSIZE / 8; ++i) {
        psy->fft_rev8[i] = reverse_u8((unsigned int)i);
    }
}
