/*
 * Fixed-size Hartley transforms for psychoacoustic analysis.
 *
 * The input setup below intentionally retains the legacy fused
 * window/permutation/radix-4 seed path.  Only the transform stages are
 * specialized here.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "lame.h"
#include "machine.h"
#include "encoder.h"
#include "util.h"
#include "fft.h"

typedef struct {
    FLOAT c1;
    FLOAT s1;
    FLOAT c2;
    FLOAT s2;
} dht_twiddle_t;

#include "fft_twiddles.inc"

#define DHT_STAGE(FZ, N, KX, K1, K2, K3, K4, TW)           \
    do {                                                   \
        FLOAT* fi_ = (FZ);                                 \
        FLOAT* gi_ = (FZ) + (KX);                          \
        FLOAT* const end_ = (FZ) + (N);                    \
                                                           \
        do {                                               \
            FLOAT f0_, f1_, f2_, f3_;                      \
                                                           \
            f1_ = fi_[0] - fi_[K1];                        \
            f0_ = fi_[0] + fi_[K1];                        \
            f3_ = fi_[K2] - fi_[K3];                       \
            f2_ = fi_[K2] + fi_[K3];                       \
                                                           \
            fi_[K2] = f0_ - f2_;                           \
            fi_[0] = f0_ + f2_;                            \
            fi_[K3] = f1_ - f3_;                           \
            fi_[K1] = f1_ + f3_;                           \
                                                           \
            f1_ = gi_[0] - gi_[K1];                        \
            f0_ = gi_[0] + gi_[K1];                        \
            f3_ = SQRT2 * gi_[K3];                         \
            f2_ = SQRT2 * gi_[K2];                         \
                                                           \
            gi_[K2] = f0_ - f2_;                           \
            gi_[0] = f0_ + f2_;                            \
            gi_[K3] = f1_ - f3_;                           \
            gi_[K1] = f1_ + f3_;                           \
                                                           \
            fi_ += (K4);                                   \
            gi_ += (K4);                                   \
        } while (fi_ < end_);                              \
                                                           \
        for (int i_ = 1; i_ < (KX); ++i_) {                \
            const dht_twiddle_t* const t_ = &(TW)[i_ - 1]; \
            const FLOAT c1_ = t_->c1;                      \
            const FLOAT s1_ = t_->s1;                      \
            const FLOAT c2_ = t_->c2;                      \
            const FLOAT s2_ = t_->s2;                      \
                                                           \
            fi_ = (FZ) + i_;                               \
            gi_ = (FZ) + (K1) - i_;                        \
                                                           \
            do {                                           \
                FLOAT a_, b_;                              \
                FLOAT f0_, f1_, f2_, f3_;                  \
                FLOAT g0_, g1_, g2_, g3_;                  \
                                                           \
                b_ = s2_ * fi_[K1] - c2_ * gi_[K1];        \
                a_ = c2_ * fi_[K1] + s2_ * gi_[K1];        \
                                                           \
                f1_ = fi_[0] - a_;                         \
                f0_ = fi_[0] + a_;                         \
                g1_ = gi_[0] - b_;                         \
                g0_ = gi_[0] + b_;                         \
                                                           \
                b_ = s2_ * fi_[K3] - c2_ * gi_[K3];        \
                a_ = c2_ * fi_[K3] + s2_ * gi_[K3];        \
                                                           \
                f3_ = fi_[K2] - a_;                        \
                f2_ = fi_[K2] + a_;                        \
                g3_ = gi_[K2] - b_;                        \
                g2_ = gi_[K2] + b_;                        \
                                                           \
                b_ = s1_ * f2_ - c1_ * g3_;                \
                a_ = c1_ * f2_ + s1_ * g3_;                \
                                                           \
                fi_[K2] = f0_ - a_;                        \
                fi_[0] = f0_ + a_;                         \
                gi_[K3] = g1_ - b_;                        \
                gi_[K1] = g1_ + b_;                        \
                                                           \
                b_ = c1_ * g2_ - s1_ * f3_;                \
                a_ = s1_ * g2_ + c1_ * f3_;                \
                                                           \
                gi_[K2] = g0_ - a_;                        \
                gi_[0] = g0_ + a_;                         \
                fi_[K3] = f1_ - b_;                        \
                fi_[K1] = f1_ + b_;                        \
                                                           \
                fi_ += (K4);                               \
                gi_ += (K4);                               \
            } while (fi_ < end_);                          \
        }                                                  \
    } while (0)

static void dht_256(FLOAT* restrict x) {
    DHT_STAGE(x, 256, 2, 4, 8, 12, 16, dht_tw16);
    DHT_STAGE(x, 256, 8, 16, 32, 48, 64, dht_tw64);
    DHT_STAGE(x, 256, 32, 64, 128, 192, 256, dht_tw256);
}

static void dht_1024(FLOAT* restrict x) {
    DHT_STAGE(x, 1024, 2, 4, 8, 12, 16, dht_tw16);
    DHT_STAGE(x, 1024, 8, 16, 32, 48, 64, dht_tw64);
    DHT_STAGE(x, 1024, 32, 64, 128, 192, 256, dht_tw256);
    DHT_STAGE(x, 1024, 128, 256, 512, 768, 1024, dht_tw1024);
}

#undef DHT_STAGE

static const unsigned char rv_tbl[] = {0x00, 0x80, 0x40, 0xc0, 0x20, 0xa0, 0x60, 0xe0, 0x10, 0x90, 0x50, 0xd0, 0x30, 0xb0, 0x70, 0xf0, 0x08, 0x88, 0x48, 0xc8, 0x28, 0xa8, 0x68, 0xe8, 0x18, 0x98,
                                       0x58, 0xd8, 0x38, 0xb8, 0x78, 0xf8, 0x04, 0x84, 0x44, 0xc4, 0x24, 0xa4, 0x64, 0xe4, 0x14, 0x94, 0x54, 0xd4, 0x34, 0xb4, 0x74, 0xf4, 0x0c, 0x8c, 0x4c, 0xcc,
                                       0x2c, 0xac, 0x6c, 0xec, 0x1c, 0x9c, 0x5c, 0xdc, 0x3c, 0xbc, 0x7c, 0xfc, 0x02, 0x82, 0x42, 0xc2, 0x22, 0xa2, 0x62, 0xe2, 0x12, 0x92, 0x52, 0xd2, 0x32, 0xb2,
                                       0x72, 0xf2, 0x0a, 0x8a, 0x4a, 0xca, 0x2a, 0xaa, 0x6a, 0xea, 0x1a, 0x9a, 0x5a, 0xda, 0x3a, 0xba, 0x7a, 0xfa, 0x06, 0x86, 0x46, 0xc6, 0x26, 0xa6, 0x66, 0xe6,
                                       0x16, 0x96, 0x56, 0xd6, 0x36, 0xb6, 0x76, 0xf6, 0x0e, 0x8e, 0x4e, 0xce, 0x2e, 0xae, 0x6e, 0xee, 0x1e, 0x9e, 0x5e, 0xde, 0x3e, 0xbe, 0x7e, 0xfe};

#define ch01(index) (buffer[chn][index])

#define ml00(f) (window[i] * f(i))
#define ml10(f) (window[i + 0x200] * f(i + 0x200))
#define ml20(f) (window[i + 0x100] * f(i + 0x100))
#define ml30(f) (window[i + 0x300] * f(i + 0x300))

#define ml01(f) (window[i + 0x001] * f(i + 0x001))
#define ml11(f) (window[i + 0x201] * f(i + 0x201))
#define ml21(f) (window[i + 0x101] * f(i + 0x101))
#define ml31(f) (window[i + 0x301] * f(i + 0x301))

#define ms00(f) (window_s[i] * f(i + k))
#define ms10(f) (window_s[0x7f - i] * f(i + k + 0x80))
#define ms20(f) (window_s[i + 0x40] * f(i + k + 0x40))
#define ms30(f) (window_s[0x3f - i] * f(i + k + 0xc0))

#define ms01(f) (window_s[i + 0x01] * f(i + k + 0x01))
#define ms11(f) (window_s[0x7e - i] * f(i + k + 0x81))
#define ms21(f) (window_s[i + 0x41] * f(i + k + 0x41))
#define ms31(f) (window_s[0x3e - i] * f(i + k + 0xc1))

void fft_short(lame_internal_flags const* const gfc, FLOAT x_real[3][BLKSIZE_s], int chn, const sample_t* const buffer[2]) {
    int i;
    int j;
    int b;

#define window_s gfc->cd_psy->window_s
#define window gfc->cd_psy->window

    for (b = 0; b < 3; b++) {
        FLOAT* x = &x_real[b][BLKSIZE_s / 2];
        short const k = (576 / 3) * (b + 1);
        j = BLKSIZE_s / 8 - 1;
        do {
            FLOAT f0, f1, f2, f3, w;

            i = rv_tbl[j << 2];

            f0 = ms00(ch01);
            w = ms10(ch01);
            f1 = f0 - w;
            f0 = f0 + w;
            f2 = ms20(ch01);
            w = ms30(ch01);
            f3 = f2 - w;
            f2 = f2 + w;

            x -= 4;
            x[0] = f0 + f2;
            x[2] = f0 - f2;
            x[1] = f1 + f3;
            x[3] = f1 - f3;

            f0 = ms01(ch01);
            w = ms11(ch01);
            f1 = f0 - w;
            f0 = f0 + w;
            f2 = ms21(ch01);
            w = ms31(ch01);
            f3 = f2 - w;
            f2 = f2 + w;

            x[BLKSIZE_s / 2 + 0] = f0 + f2;
            x[BLKSIZE_s / 2 + 2] = f0 - f2;
            x[BLKSIZE_s / 2 + 1] = f1 + f3;
            x[BLKSIZE_s / 2 + 3] = f1 - f3;
        } while (--j >= 0);

#undef window
#undef window_s

        dht_256(x);
        /* BLKSIZE_s/2 because of 3DNow! ASM routine */
    }
}

void fft_long(lame_internal_flags const* const gfc, FLOAT x[BLKSIZE], int chn, const sample_t* const buffer[2]) {
    int i;
    int jj = BLKSIZE / 8 - 1;
    x += BLKSIZE / 2;

#define window_s gfc->cd_psy->window_s
#define window gfc->cd_psy->window

    do {
        FLOAT f0, f1, f2, f3, w;

        i = rv_tbl[jj];
        f0 = ml00(ch01);
        w = ml10(ch01);
        f1 = f0 - w;
        f0 = f0 + w;
        f2 = ml20(ch01);
        w = ml30(ch01);
        f3 = f2 - w;
        f2 = f2 + w;

        x -= 4;
        x[0] = f0 + f2;
        x[2] = f0 - f2;
        x[1] = f1 + f3;
        x[3] = f1 - f3;

        f0 = ml01(ch01);
        w = ml11(ch01);
        f1 = f0 - w;
        f0 = f0 + w;
        f2 = ml21(ch01);
        w = ml31(ch01);
        f3 = f2 - w;
        f2 = f2 + w;

        x[BLKSIZE / 2 + 0] = f0 + f2;
        x[BLKSIZE / 2 + 2] = f0 - f2;
        x[BLKSIZE / 2 + 1] = f1 + f3;
        x[BLKSIZE / 2 + 3] = f1 - f3;
    } while (--jj >= 0);

#undef window
#undef window_s

    dht_1024(x);
    /* BLKSIZE/2 because of 3DNow! ASM routine */
}

void init_fft(lame_internal_flags* const gfc) {
    int i;

    /* The type of window used here will make no real difference, but */
    /* in the interest of merging nspsytune stuff - switch to blackman window */
    for (i = 0; i < BLKSIZE; i++)
        /* blackman window */
        gfc->cd_psy->window[i] = 0.42 - 0.5 * cos(2 * PI * (i + .5) / BLKSIZE) + 0.08 * cos(4 * PI * (i + .5) / BLKSIZE);

    for (i = 0; i < BLKSIZE_s / 2; i++)
        gfc->cd_psy->window_s[i] = 0.5 * (1.0 - cos(2.0 * PI * (i + 0.5) / BLKSIZE_s));
}
