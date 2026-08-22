#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "lame.h"

static int  tests_run = 0;
static int  tests_failed = 0;
static int  current_test_active = 0;
static int  current_test_failed = 0;

#define TEST(name) do { \
    tests_run++; \
    current_test_active = 1; \
    current_test_failed = 0; \
    printf("  %-60s ", name); \
    fflush(stdout); \
} while (0)

#define PASS()  do { \
    if (current_test_active && !current_test_failed) { \
        puts("ok"); \
        current_test_active = 0; \
    } \
} while (0)

#define FAIL(msg) do { \
    puts("FAIL"); \
    printf("    %s:%d: %s\n", __FILE__, __LINE__, msg); \
    if (!current_test_failed) { \
        tests_failed++; \
        current_test_failed = 1; \
    } \
    current_test_active = 0; \
} while (0)

#define ASSERT(cond, msg) do { \
    if (!(cond)) { FAIL(msg); return; } \
} while (0)

/* ── helpers ────────────────────────────────────────────────────────────── */

static lame_global_flags *
make_gfp(int samplerate, int channels)
{
    lame_global_flags *gfp = lame_init();
    if (!gfp) return NULL;
    lame_set_in_samplerate(gfp, samplerate);
    lame_set_num_channels(gfp, channels);
    return gfp;
}

static lame_global_flags *
make_mono_gfp(void)
{
    return make_gfp(44100, 1);
}

static int
buffer_contains(unsigned char const *buf, size_t len, char const *needle)
{
    size_t const nlen = strlen(needle);
    if (nlen == 0 || len < nlen) {
        return 0;
    }
    for (size_t i = 0; i + nlen <= len; ++i) {
        if (memcmp(buf + i, needle, nlen) == 0) {
            return 1;
        }
    }
    return 0;
}

/* ── tables.c ───────────────────────────────────────────────────────────── */

static void
test_lame_get_bitrate(void)
{
    TEST("lame_get_bitrate valid MPEG-1 indices");
    int expected[] = { 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320 };
    for (int i = 1; i <= 14; i++) {
        int br = lame_get_bitrate(1, i);
        ASSERT(br == expected[i - 1], "unexpected MPEG-1 bitrate");
    }
    PASS();

    TEST("lame_get_bitrate invalid index returns -1");
    ASSERT(lame_get_bitrate(1, 0) == 0, "index 0 is free format (0)");
    ASSERT(lame_get_bitrate(1, 15) == -1, "index 15 should be invalid");
    PASS();

    TEST("lame_get_bitrate invalid version returns -1");
    ASSERT(lame_get_bitrate(-1, 1) == -1, "version -1 should be invalid");
    ASSERT(lame_get_bitrate(3, 1) == -1, "version 3 should be invalid");
    PASS();
}

static void
test_lame_get_samplerate(void)
{
    TEST("lame_get_samplerate valid MPEG-1 indices");
    int expected[] = { 44100, 48000, 32000 };
    for (int i = 0; i < 3; i++) {
        int sr = lame_get_samplerate(1, i);
        ASSERT(sr == expected[i], "unexpected MPEG-1 samplerate");
    }
    PASS();

    TEST("lame_get_samplerate invalid version returns -1");
    ASSERT(lame_get_samplerate(-1, 0) == -1, "version -1 should be invalid");
    ASSERT(lame_get_samplerate(3, 0) == -1, "version 3 should be invalid");
    PASS();

    TEST("lame_get_samplerate invalid index returns -1");
    ASSERT(lame_get_samplerate(1, 3) == -1, "index 3 should be invalid");
    PASS();
}

/* ── version.c ──────────────────────────────────────────────────────────── */

static void
test_version_strings(void)
{
    TEST("get_lame_version returns non-empty string");
    const char *v = get_lame_version();
    ASSERT(v != NULL && strlen(v) > 0, "version string is empty");
    PASS();

    TEST("get_lame_short_version returns non-empty string");
    const char *sv = get_lame_short_version();
    ASSERT(sv != NULL && strlen(sv) > 0, "short version is empty");
    PASS();

    TEST("get_lame_very_short_version returns non-empty string");
    const char *vsv = get_lame_very_short_version();
    ASSERT(vsv != NULL && strlen(vsv) > 0, "very short version is empty");
    PASS();

    TEST("get_psy_version returns non-empty string");
    const char *psy = get_psy_version();
    ASSERT(psy != NULL && strlen(psy) > 0, "psy version is empty");
    PASS();

    TEST("get_lame_url returns non-empty string");
    const char *url = get_lame_url();
    ASSERT(url != NULL && strlen(url) > 0, "URL is empty");
    PASS();

    TEST("version strings start with same number");
    const char *full = get_lame_version();
    const char *shortv = get_lame_short_version();
    /* both start with the same major.minor, e.g. "3.101" */
    ASSERT(full[0] == shortv[0], "version strings should start with same char");
    PASS();
}

static void
test_lame_version_numerical(void)
{
    TEST("get_lame_version_numerical populates struct");
    lame_version_t lv;
    memset(&lv, 0, sizeof(lv));
    get_lame_version_numerical(&lv);
    ASSERT(lv.major > 0, "major version should be > 0");
    ASSERT(lv.minor >= 0, "minor version should be >= 0");
    PASS();
}

/* ── lame_init / encoder error paths ───────────────────────────────────── */

static void
test_lame_init_invalid_params(void)
{
    lame_global_flags *gfp;
    int ret;

    TEST("lame_init_params with valid params returns 0");
    gfp = lame_init();
    ASSERT(gfp != NULL, "lame_init failed");
    lame_set_in_samplerate(gfp, 44100);
    lame_set_num_channels(gfp, 1);
    ret = lame_init_params(gfp);
    ASSERT(ret == 0, "should succeed with valid params");
    lame_close(gfp);
    PASS();

    TEST("lame_init_params with NULL returns error");
    ret = lame_init_params(NULL);
    ASSERT(ret < 0, "should fail with NULL gfp");
    PASS();

    TEST("lame_init multiple times succeeds");
    lame_global_flags *a = lame_init();
    lame_global_flags *b = lame_init();
    lame_global_flags *c = lame_init();
    ASSERT(a && b && c, "multiple lame_init calls should succeed");
    lame_close(a);
    lame_close(b);
    lame_close(c);
    PASS();
}

/* ── lame_encode_buffer variants (P1) ──────────────────────────────────── */

static void
test_encode_short(void)
{
    lame_global_flags *gfp = make_mono_gfp();
    ASSERT(gfp != NULL, "lame_init failed");
    ASSERT(lame_init_params(gfp) >= 0, "lame_init_params failed");

    short   pcm[1152];
    unsigned char buf[8192];
    memset(pcm, 0, sizeof(pcm));
    int ret = lame_encode_buffer(gfp, pcm, pcm, 1152, buf, sizeof(buf));
    ASSERT(ret >= 0, "lame_encode_buffer failed");
    lame_close(gfp);
}

static void
test_encode_float(void)
{
    lame_global_flags *gfp = make_mono_gfp();
    ASSERT(gfp != NULL, "lame_init failed");
    ASSERT(lame_init_params(gfp) >= 0, "lame_init_params failed");

    float   pcm[1152];
    unsigned char buf[8192];
    for (int i = 0; i < 1152; i++) pcm[i] = 0.0f;
    int ret = lame_encode_buffer_float(gfp, pcm, pcm, 1152, buf, sizeof(buf));
    ASSERT(ret >= 0, "lame_encode_buffer_float failed");
    lame_close(gfp);
}

static void
test_encode_int(void)
{
    lame_global_flags *gfp = make_mono_gfp();
    ASSERT(gfp != NULL, "lame_init failed");
    ASSERT(lame_init_params(gfp) >= 0, "lame_init_params failed");

    int     pcm[1152];
    unsigned char buf[8192];
    for (int i = 0; i < 1152; i++) pcm[i] = 0;
    int ret = lame_encode_buffer_int(gfp, pcm, pcm, 1152, buf, sizeof(buf));
    ASSERT(ret >= 0, "lame_encode_buffer_int failed");
    lame_close(gfp);
}

static void
test_encode_long2(void)
{
    lame_global_flags *gfp = make_mono_gfp();
    ASSERT(gfp != NULL, "lame_init failed");
    ASSERT(lame_init_params(gfp) >= 0, "lame_init_params failed");

    long    pcm[1152];
    unsigned char buf[8192];
    for (int i = 0; i < 1152; i++) pcm[i] = 0;
    int ret = lame_encode_buffer_long2(gfp, pcm, pcm, 1152, buf, sizeof(buf));
    ASSERT(ret >= 0, "lame_encode_buffer_long2 failed");
    lame_close(gfp);
}

static void
test_encode_interleaved(void)
{
    lame_global_flags *gfp = lame_init();
    ASSERT(gfp != NULL, "lame_init failed");
    lame_set_in_samplerate(gfp, 44100);
    lame_set_num_channels(gfp, 2);
    ASSERT(lame_init_params(gfp) >= 0, "lame_init_params failed");

    short   pcm[2304];
    unsigned char buf[8192];
    memset(pcm, 0, sizeof(pcm));
    int ret = lame_encode_buffer_interleaved(gfp, pcm, 1152, buf, sizeof(buf));
    ASSERT(ret >= 0, "lame_encode_buffer_interleaved failed");
    lame_close(gfp);
}

static void
test_encode_ieee_float(void)
{
    lame_global_flags *gfp = make_mono_gfp();
    ASSERT(gfp != NULL, "lame_init failed");
    ASSERT(lame_init_params(gfp) >= 0, "lame_init_params failed");

    float   pcm[1152];
    unsigned char buf[8192];
    for (int i = 0; i < 1152; i++) pcm[i] = 0.0f;
    int ret = lame_encode_buffer_ieee_float(gfp, pcm, pcm, 1152, buf, sizeof(buf));
    ASSERT(ret >= 0, "lame_encode_buffer_ieee_float failed");
    lame_close(gfp);
}

static void
test_encode_interleaved_ieee_float(void)
{
    lame_global_flags *gfp = lame_init();
    ASSERT(gfp != NULL, "lame_init failed");
    lame_set_in_samplerate(gfp, 44100);
    lame_set_num_channels(gfp, 2);
    ASSERT(lame_init_params(gfp) >= 0, "lame_init_params failed");

    float   pcm[2304];
    unsigned char buf[8192];
    memset(pcm, 0, sizeof(pcm));
    int ret = lame_encode_buffer_interleaved_ieee_float(gfp, pcm, 1152, buf, sizeof(buf));
    ASSERT(ret >= 0, "lame_encode_buffer_interleaved_ieee_float failed");
    lame_close(gfp);
}

static void
test_encode_ieee_double(void)
{
    lame_global_flags *gfp = make_mono_gfp();
    ASSERT(gfp != NULL, "lame_init failed");
    ASSERT(lame_init_params(gfp) >= 0, "lame_init_params failed");

    double  pcm[1152];
    unsigned char buf[8192];
    for (int i = 0; i < 1152; i++) pcm[i] = 0.0;
    int ret = lame_encode_buffer_ieee_double(gfp, pcm, pcm, 1152, buf, sizeof(buf));
    ASSERT(ret >= 0, "lame_encode_buffer_ieee_double failed");
    lame_close(gfp);
}

static void
test_encode_interleaved_ieee_double(void)
{
    lame_global_flags *gfp = lame_init();
    ASSERT(gfp != NULL, "lame_init failed");
    lame_set_in_samplerate(gfp, 44100);
    lame_set_num_channels(gfp, 2);
    ASSERT(lame_init_params(gfp) >= 0, "lame_init_params failed");

    double  pcm[2304];
    unsigned char buf[8192];
    memset(pcm, 0, sizeof(pcm));
    int ret = lame_encode_buffer_interleaved_ieee_double(gfp, pcm, 1152, buf, sizeof(buf));
    ASSERT(ret >= 0, "lame_encode_buffer_interleaved_ieee_double failed");
    lame_close(gfp);
}

/* ── flush / close paths (P1) ──────────────────────────────────────────── */

static void
test_encode_flush_nogap(void)
{
    lame_global_flags *gfp = make_mono_gfp();
    ASSERT(gfp != NULL, "lame_init failed");
    ASSERT(lame_init_params(gfp) >= 0, "lame_init_params failed");

    unsigned char buf[8192];
    int ret = lame_encode_flush_nogap(gfp, buf, sizeof(buf));
    ASSERT(ret >= 0, "lame_encode_flush_nogap failed");
    lame_close(gfp);

    TEST("lame_encode_flush_nogap succeeds on empty stream"); PASS();
}

static void
test_encode_flush_nogap_after_encode(void)
{
    lame_global_flags *gfp = make_mono_gfp();
    ASSERT(gfp != NULL, "lame_init failed");
    ASSERT(lame_init_params(gfp) >= 0, "lame_init_params failed");

    short   pcm[1152];
    unsigned char buf[8192];
    memset(pcm, 0, sizeof(pcm));
    lame_encode_buffer(gfp, pcm, pcm, 1152, buf, sizeof(buf));

    int ret = lame_encode_flush_nogap(gfp, buf, sizeof(buf));
    ASSERT(ret >= 0, "lame_encode_flush_nogap after encode failed");
    lame_close(gfp);

    TEST("lame_encode_flush_nogap after encode succeeds"); PASS();
}

static void
test_lame_close(void)
{
    lame_global_flags *gfp = lame_init();
    ASSERT(gfp != NULL, "lame_init failed");
    lame_set_in_samplerate(gfp, 44100);
    lame_set_num_channels(gfp, 1);
    ASSERT(lame_init_params(gfp) >= 0, "lame_init_params failed");
    int ret = lame_close(gfp);
    ASSERT(ret == 0, "lame_close after init_params should return 0");
    PASS();
}

/* ── set / get round-trips (P3) ────────────────────────────────────────── */

static void
test_set_get_brate(void)
{
    lame_global_flags *gfp = lame_init();
    ASSERT(gfp != NULL, "lame_init failed");

    lame_set_brate(gfp, 128);
    ASSERT(lame_get_brate(gfp) == 128, "brate round-trip failed");

    lame_set_brate(gfp, 320);
    ASSERT(lame_get_brate(gfp) == 320, "brate round-trip failed");
    lame_close(gfp);
    PASS();
}

static void
test_set_get_vbr(void)
{
    lame_global_flags *gfp = lame_init();
    ASSERT(gfp != NULL, "lame_init failed");

    lame_set_VBR(gfp, vbr_off);
    ASSERT(lame_get_VBR(gfp) == vbr_off, "vbr_off round-trip failed");

    lame_set_VBR(gfp, vbr_rh);
    ASSERT(lame_get_VBR(gfp) == vbr_rh, "vbr_rh round-trip failed");

    lame_set_VBR(gfp, vbr_mtrh);
    ASSERT(lame_get_VBR(gfp) == vbr_mtrh, "vbr_mtrh round-trip failed");

    lame_set_VBR(gfp, vbr_abr);
    ASSERT(lame_get_VBR(gfp) == vbr_abr, "vbr_abr round-trip failed");
    lame_close(gfp);
    PASS();
}

static void
test_set_get_mode(void)
{
    lame_global_flags *gfp = lame_init();
    ASSERT(gfp != NULL, "lame_init failed");

    lame_set_mode(gfp, STEREO);
    ASSERT(lame_get_mode(gfp) == STEREO, "STEREO round-trip failed");

    lame_set_mode(gfp, JOINT_STEREO);
    ASSERT(lame_get_mode(gfp) == JOINT_STEREO, "JOINT_STEREO round-trip failed");

    lame_set_mode(gfp, MONO);
    ASSERT(lame_get_mode(gfp) == MONO, "MONO round-trip failed");
    lame_close(gfp);
    PASS();
}

static void
test_set_get_quality(void)
{
    lame_global_flags *gfp = lame_init();
    ASSERT(gfp != NULL, "lame_init failed");

    for (int q = 0; q <= 9; q++) {
        lame_set_quality(gfp, q);
        ASSERT(lame_get_quality(gfp) == q, "quality round-trip failed");
    }
    lame_close(gfp);
    PASS();
}

static void
test_set_get_samplerate(void)
{
    lame_global_flags *gfp = make_mono_gfp();
    ASSERT(gfp != NULL, "lame_init failed");
    ASSERT(lame_init_params(gfp) >= 0, "lame_init_params failed");

    ASSERT(lame_get_in_samplerate(gfp) > 0, "in samplerate should be > 0");
    ASSERT(lame_get_out_samplerate(gfp) > 0, "out samplerate should be > 0");
    lame_close(gfp);
    PASS();
}

static void
test_set_get_num_channels(void)
{
    lame_global_flags *gfp = make_mono_gfp();
    ASSERT(gfp != NULL, "lame_init failed");
    ASSERT(lame_init_params(gfp) >= 0, "lame_init_params failed");

    ASSERT(lame_get_num_channels(gfp) == 1, "num_channels should be 1");
    lame_close(gfp);
    PASS();
}

static void
test_set_get_frame_size(void)
{
    lame_global_flags *gfp = make_mono_gfp();
    ASSERT(gfp != NULL, "lame_init failed");
    ASSERT(lame_init_params(gfp) >= 0, "lame_init_params failed");

    ASSERT(lame_get_framesize(gfp) > 0, "framesize should be > 0");
    lame_close(gfp);
    PASS();
}

static void
test_set_get_error_protection(void)
{
    lame_global_flags *gfp = lame_init();
    ASSERT(gfp != NULL, "lame_init failed");

    lame_set_error_protection(gfp, 1);
    ASSERT(lame_get_error_protection(gfp) == 1, "error_protection=1 failed");

    lame_set_error_protection(gfp, 0);
    ASSERT(lame_get_error_protection(gfp) == 0, "error_protection=0 failed");
    lame_close(gfp);
    PASS();
}

static void
test_set_get_copyright(void)
{
    lame_global_flags *gfp = lame_init();
    ASSERT(gfp != NULL, "lame_init failed");

    lame_set_copyright(gfp, 1);
    ASSERT(lame_get_copyright(gfp) == 1, "copyright=1 failed");

    lame_set_copyright(gfp, 0);
    ASSERT(lame_get_copyright(gfp) == 0, "copyright=0 failed");
    lame_close(gfp);
    PASS();
}

static void
test_set_get_original(void)
{
    lame_global_flags *gfp = lame_init();
    ASSERT(gfp != NULL, "lame_init failed");

    lame_set_original(gfp, 1);
    ASSERT(lame_get_original(gfp) == 1, "original=1 failed");

    lame_set_original(gfp, 0);
    ASSERT(lame_get_original(gfp) == 0, "original=0 failed");
    lame_close(gfp);
    PASS();
}

static void
test_set_get_extension(void)
{
    lame_global_flags *gfp = lame_init();
    ASSERT(gfp != NULL, "lame_init failed");

    lame_set_extension(gfp, 1);
    ASSERT(lame_get_extension(gfp) == 1, "extension=1 failed");

    lame_set_extension(gfp, 0);
    ASSERT(lame_get_extension(gfp) == 0, "extension=0 failed");
    lame_close(gfp);
    PASS();
}

static void
test_set_get_strict_iso(void)
{
    lame_global_flags *gfp = lame_init();
    ASSERT(gfp != NULL, "lame_init failed");

    lame_set_strict_ISO(gfp, 1);
    ASSERT(lame_get_strict_ISO(gfp) == 1, "strict_ISO=1 failed");

    lame_set_strict_ISO(gfp, 0);
    ASSERT(lame_get_strict_ISO(gfp) == 0, "strict_ISO=0 failed");
    lame_close(gfp);
    PASS();
}

static void
test_set_get_disable_reservoir(void)
{
    lame_global_flags *gfp = lame_init();
    ASSERT(gfp != NULL, "lame_init failed");

    lame_set_disable_reservoir(gfp, 1);
    ASSERT(lame_get_disable_reservoir(gfp) == 1, "disable_reservoir=1 failed");

    lame_set_disable_reservoir(gfp, 0);
    ASSERT(lame_get_disable_reservoir(gfp) == 0, "disable_reservoir=0 failed");
    lame_close(gfp);
    PASS();
}

static void
test_set_get_scale(void)
{
    lame_global_flags *gfp = lame_init();
    ASSERT(gfp != NULL, "lame_init failed");

    lame_set_scale(gfp, 0.5f);
    float s = lame_get_scale(gfp);
    ASSERT(fabsf(s - 0.5f) < 0.001f, "scale round-trip failed");

    lame_set_scale(gfp, 1.0f);
    s = lame_get_scale(gfp);
    ASSERT(fabsf(s - 1.0f) < 0.001f, "scale round-trip failed");
    lame_close(gfp);
    PASS();
}

static void
test_set_get_force_ms(void)
{
    lame_global_flags *gfp = lame_init();
    ASSERT(gfp != NULL, "lame_init failed");

    lame_set_force_ms(gfp, 1);
    ASSERT(lame_get_force_ms(gfp) == 1, "force_ms=1 failed");

    lame_set_force_ms(gfp, 0);
    ASSERT(lame_get_force_ms(gfp) == 0, "force_ms=0 failed");
    lame_close(gfp);
    PASS();
}

static void
test_set_get_lowpassfreq(void)
{
    lame_global_flags *gfp = lame_init();
    ASSERT(gfp != NULL, "lame_init failed");

    lame_set_lowpassfreq(gfp, 8000);
    int lp = lame_get_lowpassfreq(gfp);
    ASSERT(lp == 8000, "lowpassfreq round-trip failed");
    lame_close(gfp);
    PASS();
}

static void
test_set_get_highpassfreq(void)
{
    lame_global_flags *gfp = lame_init();
    ASSERT(gfp != NULL, "lame_init failed");

    lame_set_highpassfreq(gfp, 200);
    int hp = lame_get_highpassfreq(gfp);
    ASSERT(hp == 200, "highpassfreq round-trip failed");
    lame_close(gfp);
    PASS();
}

static void
test_set_get_ath_params(void)
{
    lame_global_flags *gfp = lame_init();
    ASSERT(gfp != NULL, "lame_init failed");

    lame_set_noATH(gfp, 1);
    ASSERT(lame_get_noATH(gfp) == 1, "noATH=1 failed");

    lame_set_ATHonly(gfp, 1);
    ASSERT(lame_get_ATHonly(gfp) == 1, "ATHonly=1 failed");

    lame_set_ATHshort(gfp, 1);
    ASSERT(lame_get_ATHshort(gfp) == 1, "ATHshort=1 failed");

    lame_set_ATHtype(gfp, 3);
    ASSERT(lame_get_ATHtype(gfp) == 3, "ATHtype round-trip failed");
    lame_close(gfp);
    PASS();
}

static void
test_set_get_preset(void)
{
    lame_global_flags *gfp = lame_init();
    ASSERT(gfp != NULL, "lame_init failed");

    lame_set_preset(gfp, 128);
    int ret = lame_init_params(gfp);
    ASSERT(ret >= 0, "lame_init_params with preset 128 failed");
    lame_close(gfp);
    PASS();
}

static void
test_set_get_vbr_quality(void)
{
    lame_global_flags *gfp = lame_init();
    ASSERT(gfp != NULL, "lame_init failed");

    lame_set_VBR_q(gfp, 5);
    ASSERT(lame_get_VBR_q(gfp) == 5, "VBR_q round-trip failed");

    lame_set_VBR_q(gfp, 0);
    ASSERT(lame_get_VBR_q(gfp) == 0, "VBR_q=0 round-trip failed");

    lame_set_VBR_q(gfp, 9);
    ASSERT(lame_get_VBR_q(gfp) == 9, "VBR_q=9 round-trip failed");
    lame_close(gfp);
    PASS();
}

static void
test_set_get_compression_ratio(void)
{
    lame_global_flags *gfp = make_mono_gfp();
    ASSERT(gfp != NULL, "lame_init failed");
    ASSERT(lame_init_params(gfp) >= 0, "lame_init_params failed");

    float cr = lame_get_compression_ratio(gfp);
    ASSERT(cr > 0.0f, "compression ratio should be > 0");
    lame_close(gfp);
    PASS();
}

static void
test_set_get_vbr_min_max(void)
{
    lame_global_flags *gfp = lame_init();
    ASSERT(gfp != NULL, "lame_init failed");

    lame_set_VBR_min_bitrate_kbps(gfp, 32);
    ASSERT(lame_get_VBR_min_bitrate_kbps(gfp) == 32, "VBR min round-trip failed");

    lame_set_VBR_max_bitrate_kbps(gfp, 256);
    ASSERT(lame_get_VBR_max_bitrate_kbps(gfp) == 256, "VBR max round-trip failed");
    lame_close(gfp);
    PASS();
}

static void
test_v0_default_floor_policy(void)
{
    lame_global_flags *gfp;

    TEST("V0 default floor MPEG-1 stereo is 128");
    gfp = make_gfp(44100, 2);
    ASSERT(gfp != NULL, "lame_init failed");
    lame_set_VBR(gfp, vbr_mtrh);
    lame_set_VBR_q(gfp, 0);
    ASSERT(lame_init_params(gfp) >= 0, "lame_init_params failed");
    ASSERT(lame_get_VBR_min_bitrate_kbps(gfp) == 128, "V0 MPEG-1 stereo floor should be 128 kbps");
    lame_close(gfp);
    PASS();

    TEST("V0 default floor MPEG-1 mono is 64");
    gfp = make_gfp(44100, 1);
    ASSERT(gfp != NULL, "lame_init failed");
    lame_set_VBR(gfp, vbr_mtrh);
    lame_set_VBR_q(gfp, 0);
    ASSERT(lame_init_params(gfp) >= 0, "lame_init_params failed");
    ASSERT(lame_get_VBR_min_bitrate_kbps(gfp) == 64, "V0 MPEG-1 mono floor should be 64 kbps");
    lame_close(gfp);
    PASS();

    TEST("V0 default floor low-rate stereo scales to 64");
    gfp = make_gfp(22050, 2);
    ASSERT(gfp != NULL, "lame_init failed");
    lame_set_VBR(gfp, vbr_mtrh);
    lame_set_VBR_q(gfp, 0);
    ASSERT(lame_init_params(gfp) >= 0, "lame_init_params failed");
    ASSERT(lame_get_VBR_min_bitrate_kbps(gfp) == 64, "V0 low-rate stereo floor should be 64 kbps");
    lame_close(gfp);
    PASS();

    TEST("V0 explicit min floor overrides preset default");
    gfp = make_gfp(44100, 2);
    ASSERT(gfp != NULL, "lame_init failed");
    lame_set_VBR(gfp, vbr_mtrh);
    lame_set_VBR_q(gfp, 0);
    lame_set_VBR_min_bitrate_kbps(gfp, 192);
    ASSERT(lame_init_params(gfp) >= 0, "lame_init_params failed");
    ASSERT(lame_get_VBR_min_bitrate_kbps(gfp) == 192, "explicit VBR min should override V0 preset floor");
    lame_close(gfp);
    PASS();

    TEST("V0 preset floor respects explicit max bitrate");
    gfp = make_gfp(44100, 2);
    ASSERT(gfp != NULL, "lame_init failed");
    lame_set_VBR(gfp, vbr_mtrh);
    lame_set_VBR_q(gfp, 0);
    lame_set_VBR_max_bitrate_kbps(gfp, 96);
    ASSERT(lame_init_params(gfp) >= 0, "lame_init_params failed");
    ASSERT(lame_get_VBR_min_bitrate_kbps(gfp) == 96, "V0 preset floor should clamp to explicit max bitrate");
    lame_close(gfp);
    PASS();
}

/* ── ID3 tag round-trip (P2) ────────────────────────────────────────────── */

static void
test_id3tag_roundtrip(void)
{
    lame_global_flags *gfp = lame_init();
    ASSERT(gfp != NULL, "lame_init failed");
    lame_set_in_samplerate(gfp, 44100);
    lame_set_num_channels(gfp, 1);
    lame_set_write_id3tag_automatic(gfp, 0);
    id3tag_add_v2(gfp);

    id3tag_set_title(gfp, "Test Title");
    id3tag_set_artist(gfp, "Test Artist");
    id3tag_set_album(gfp, "Test Album");
    id3tag_set_year(gfp, "2024");
    id3tag_set_comment(gfp, "Test Comment");
    id3tag_set_track(gfp, "7");
    id3tag_set_genre(gfp, "12"); /* Other */

    ASSERT(lame_init_params(gfp) >= 0, "lame_init_params failed");

    short pcm[1152];
    unsigned char buf[8192];
    memset(pcm, 0, sizeof(pcm));

    int ret = lame_encode_buffer(gfp, pcm, pcm, 1152, buf, sizeof(buf));
    ASSERT(ret >= 0, "lame_encode_buffer failed");

    lame_encode_flush_nogap(gfp, buf, sizeof(buf));

    unsigned char tag[4096];
    size_t n = lame_get_id3v2_tag(gfp, tag, sizeof(tag));
    ASSERT(n > 0, "id3v2 tag should be non-empty");
    ASSERT(n < sizeof(tag), "id3v2 tag should fit in buffer");
    ASSERT(n > 10, "id3v2 tag should include a header");
    ASSERT(memcmp(tag, "ID3", 3) == 0, "id3v2 tag should start with ID3");
    ASSERT(buffer_contains(tag, n, "TIT2"), "id3v2 tag should contain TIT2");
    ASSERT(buffer_contains(tag, n, "TPE1"), "id3v2 tag should contain TPE1");
    ASSERT(buffer_contains(tag, n, "TALB"), "id3v2 tag should contain TALB");
    ASSERT(buffer_contains(tag, n, "TRCK"), "id3v2 tag should contain TRCK");
    ASSERT(buffer_contains(tag, n, "TCON"), "id3v2 tag should contain TCON");

    lame_close(gfp);

    TEST("id3tag round-trip: title/artist/album/year/comment/track/genre");
    PASS();
}

static void
test_id3tag_v1_roundtrip(void)
{
    lame_global_flags *gfp = lame_init();
    ASSERT(gfp != NULL, "lame_init failed");
    lame_set_in_samplerate(gfp, 44100);
    lame_set_num_channels(gfp, 1);
    lame_set_write_id3tag_automatic(gfp, 0);

    id3tag_v1_only(gfp);
    id3tag_set_title(gfp, "V1 Title");
    id3tag_set_artist(gfp, "V1 Artist");
    id3tag_set_album(gfp, "V1 Album");
    id3tag_set_year(gfp, "1999");
    id3tag_set_comment(gfp, "V1 Comment");
    id3tag_set_track(gfp, "1");
    id3tag_set_genre(gfp, "Blues");

    ASSERT(lame_init_params(gfp) >= 0, "lame_init_params failed");

    short pcm[1152];
    unsigned char buf[8192];
    memset(pcm, 0, sizeof(pcm));
    lame_encode_buffer(gfp, pcm, pcm, 1152, buf, sizeof(buf));

    unsigned char tag[128];
    size_t n = lame_get_id3v1_tag(gfp, tag, sizeof(tag));
    ASSERT(n > 0, "id3v1 tag should be non-empty");
    ASSERT(n == 128, "id3v1 tag should be exactly 128 bytes");
    ASSERT(memcmp(tag, "TAG", 3) == 0, "id3v1 tag should start with TAG");
    ASSERT(memcmp(tag + 3, "V1 Title", strlen("V1 Title")) == 0, "id3v1 title mismatch");
    ASSERT(memcmp(tag + 33, "V1 Artist", strlen("V1 Artist")) == 0, "id3v1 artist mismatch");
    ASSERT(tag[126] == 1, "id3v1 track byte should be 1");
    ASSERT(tag[127] == 0, "id3v1 genre byte should be Blues");

    lame_encode_flush_nogap(gfp, buf, sizeof(buf));
    lame_close(gfp);

    TEST("id3tag v1-only: tag size and content");
    PASS();
}

/* ── P3 set/get expansion ───────────────────────────────────────────────── */

static void
test_set_get_emphasis(void)
{
    lame_global_flags *gfp = lame_init();
    ASSERT(gfp != NULL, "lame_init failed");

    lame_set_emphasis(gfp, 1);
    ASSERT(lame_get_emphasis(gfp) == 1, "emphasis=1 failed");
    lame_set_emphasis(gfp, 0);
    ASSERT(lame_get_emphasis(gfp) == 0, "emphasis=0 failed");
    lame_close(gfp);
    PASS();
}

static void
test_set_get_scale_left_right(void)
{
    lame_global_flags *gfp = lame_init();
    ASSERT(gfp != NULL, "lame_init failed");

    lame_set_scale_left(gfp, 0.5f);
    ASSERT(fabsf(lame_get_scale_left(gfp) - 0.5f) < 0.001f, "scale_left round-trip");

    lame_set_scale_right(gfp, 1.5f);
    ASSERT(fabsf(lame_get_scale_right(gfp) - 1.5f) < 0.001f, "scale_right round-trip");

    lame_close(gfp);
    PASS();
}

static void
test_set_get_short_blocks(void)
{
    lame_global_flags *gfp = lame_init();
    ASSERT(gfp != NULL, "lame_init failed");

    lame_set_force_short_blocks(gfp, 1);
    ASSERT(lame_get_force_short_blocks(gfp) == 1, "force_short_blocks=1 failed");
    lame_set_force_short_blocks(gfp, 0);
    ASSERT(lame_get_force_short_blocks(gfp) == 0, "force_short_blocks=0 failed");

    lame_set_no_short_blocks(gfp, 1);
    ASSERT(lame_get_no_short_blocks(gfp) == 1, "no_short_blocks=1 failed");
    lame_set_no_short_blocks(gfp, 0);
    ASSERT(lame_get_no_short_blocks(gfp) == 0, "no_short_blocks=0 failed");

    lame_close(gfp);
    PASS();
}

static void
test_set_get_filter_width(void)
{
    lame_global_flags *gfp = lame_init();
    ASSERT(gfp != NULL, "lame_init failed");

    lame_set_lowpasswidth(gfp, 100);
    ASSERT(lame_get_lowpasswidth(gfp) == 100, "lowpasswidth round-trip");

    lame_set_highpasswidth(gfp, 50);
    ASSERT(lame_get_highpasswidth(gfp) == 50, "highpasswidth round-trip");

    lame_close(gfp);
    PASS();
}

static void
test_set_get_replaygain(void)
{
    lame_global_flags *gfp = lame_init();
    ASSERT(gfp != NULL, "lame_init failed");

    lame_set_findReplayGain(gfp, 1);
    ASSERT(lame_get_findReplayGain(gfp) == 1, "findReplayGain=1 failed");
    lame_set_findReplayGain(gfp, 0);
    ASSERT(lame_get_findReplayGain(gfp) == 0, "findReplayGain=0 failed");

    /* may be compiled out; only verify if set succeeds */
    if (lame_set_decode_on_the_fly(gfp, 1) >= 0) {
        ASSERT(lame_get_decode_on_the_fly(gfp) == 1, "decode_on_the_fly=1 failed");
        lame_set_decode_on_the_fly(gfp, 0);
        ASSERT(lame_get_decode_on_the_fly(gfp) == 0, "decode_on_the_fly=0 failed");
    }

    lame_close(gfp);
    PASS();
}

static void
test_set_get_vbr_hard_min(void)
{
    lame_global_flags *gfp = lame_init();
    ASSERT(gfp != NULL, "lame_init failed");

    lame_set_VBR_hard_min(gfp, 1);
    ASSERT(lame_get_VBR_hard_min(gfp) == 1, "VBR_hard_min=1 failed");
    lame_set_VBR_hard_min(gfp, 0);
    ASSERT(lame_get_VBR_hard_min(gfp) == 0, "VBR_hard_min=0 failed");

    lame_close(gfp);
    PASS();
}

/* ── post-encode info queries (P3) ──────────────────────────────────────── */

static void
test_post_encode_info(void)
{
    lame_global_flags *gfp = lame_init();
    ASSERT(gfp != NULL, "lame_init failed");
    lame_set_in_samplerate(gfp, 44100);
    lame_set_num_channels(gfp, 1);
    lame_set_VBR(gfp, vbr_mtrh);
    lame_set_VBR_q(gfp, 9); /* fastest */

    /* encoder queries before init_params */
    int ver = lame_get_version(gfp);
    ASSERT(ver == 0, "version should be 0 before init_params");
    int delay = lame_get_encoder_delay(gfp);
    ASSERT(delay == 0, "delay should be 0 before init_params");
    int padding = lame_get_encoder_padding(gfp);
    ASSERT(padding == 0, "padding should be 0 before init_params");

    ASSERT(lame_init_params(gfp) >= 0, "lame_init_params failed");

    /* after init, some values become available */
    ver = lame_get_version(gfp);
    /* version may be 0 for pure CBR; just check it runs without crash */
    int sz = lame_get_size_mp3buffer(gfp);
    ASSERT(sz > 0, "size_mp3buffer should be > 0 after init");

    short pcm[1152 * 4];
    unsigned char buf[8192];
    memset(pcm, 0, sizeof(pcm));
    int ret = lame_encode_buffer(gfp, pcm, pcm, 1152 * 4, buf, sizeof(buf));
    ASSERT(ret >= 0, "lame_encode_buffer failed");

    /* post-encode queries */
    int frames = lame_get_totalframes(gfp);
    ASSERT(frames > 0, "totalframes should be > 0 after encode");

    lame_encode_flush_nogap(gfp, buf, sizeof(buf));

    int fnum = lame_get_frameNum(gfp);
    ASSERT(fnum >= 0, "frameNum should be >= 0 after flush");
    delay = lame_get_encoder_delay(gfp);
    ASSERT(delay >= 0, "encoder_delay should be >= 0");
    padding = lame_get_encoder_padding(gfp);
    ASSERT(padding >= 0, "encoder_padding should be >= 0");

    size_t ltag = lame_get_lametag_frame(gfp, buf, sizeof(buf));
    ASSERT(ltag > 0, "lametag frame should be non-empty after flush");

    lame_close(gfp);

    TEST("post-encode info: version/delay/padding/totalframes/frameNum/size_mp3buffer/lametag");
    PASS();
}

/* ── Old VBR (vbr_rh) encoding path (P1) ────────────────────────────────── */

static void
test_encode_vbr_rh_mono(void)
{
    lame_global_flags *gfp = lame_init();
    ASSERT(gfp != NULL, "lame_init failed");
    lame_set_in_samplerate(gfp, 44100);
    lame_set_num_channels(gfp, 1);
    lame_set_VBR(gfp, vbr_rh);
    lame_set_VBR_q(gfp, 9);
    ASSERT(lame_init_params(gfp) >= 0, "lame_init_params failed");

    short pcm[1152 * 10];
    unsigned char buf[8192];
    for (int i = 0; i < 1152 * 10; i++)
        pcm[i] = (short)(i * 100);
    int ret = lame_encode_buffer(gfp, pcm, pcm, 1152 * 10, buf, sizeof(buf));
    ASSERT(ret >= 0, "vbr_rh encode failed");
    lame_encode_flush_nogap(gfp, buf, sizeof(buf));
    lame_close(gfp);

    TEST("vbr_rh mono 44100Hz encode"); PASS();
}

static void
test_encode_vbr_rh_stereo(void)
{
    lame_global_flags *gfp = lame_init();
    ASSERT(gfp != NULL, "lame_init failed");
    lame_set_in_samplerate(gfp, 44100);
    lame_set_num_channels(gfp, 2);
    lame_set_VBR(gfp, vbr_rh);
    lame_set_VBR_q(gfp, 9);
    ASSERT(lame_init_params(gfp) >= 0, "lame_init_params failed");

    short pcm[2304 * 10];
    unsigned char buf[8192];
    for (int i = 0; i < 2304 * 10; i++)
        pcm[i] = (short)(i * 50);
    int ret = lame_encode_buffer_interleaved(gfp, pcm, 1152 * 10, buf, sizeof(buf));
    ASSERT(ret >= 0, "vbr_rh stereo encode failed");
    lame_encode_flush_nogap(gfp, buf, sizeof(buf));
    lame_close(gfp);

    TEST("vbr_rh stereo 44100Hz encode"); PASS();
}

static void
test_encode_vbr_rh_low_sr(void)
{
    lame_global_flags *gfp = lame_init();
    ASSERT(gfp != NULL, "lame_init failed");
    lame_set_in_samplerate(gfp, 22050);
    lame_set_num_channels(gfp, 1);
    lame_set_VBR(gfp, vbr_rh);
    lame_set_VBR_q(gfp, 9);
    ASSERT(lame_init_params(gfp) >= 0, "lame_init_params failed");

    short pcm[576 * 20];
    unsigned char buf[8192];
    for (int i = 0; i < 576 * 20; i++)
        pcm[i] = (short)(i * 200);
    int ret = lame_encode_buffer(gfp, pcm, pcm, 576 * 20, buf, sizeof(buf));
    ASSERT(ret >= 0, "vbr_rh low sr encode failed");
    lame_encode_flush_nogap(gfp, buf, sizeof(buf));
    lame_close(gfp);

    TEST("vbr_rh mono 22050Hz encode"); PASS();
}

/* ── Histogram queries (P4) ─────────────────────────────────────────────── */

static void
test_histogram_queries(void)
{
    lame_global_flags *gfp = lame_init();
    ASSERT(gfp != NULL, "lame_init failed");
    lame_set_in_samplerate(gfp, 44100);
    lame_set_num_channels(gfp, 2);
    lame_set_VBR(gfp, vbr_rh);
    lame_set_VBR_q(gfp, 9);
    ASSERT(lame_init_params(gfp) >= 0, "lame_init_params failed");

    short pcm[2304 * 20];
    unsigned char buf[8192];
    for (int i = 0; i < 2304 * 20; i++)
        pcm[i] = (short)(i * 100);
    lame_encode_buffer_interleaved(gfp, pcm, 1152 * 20, buf, sizeof(buf));
    lame_encode_flush_nogap(gfp, buf, sizeof(buf));

    int bitrate_count[14] = {0};
    lame_bitrate_hist(gfp, bitrate_count);
    int total_br = 0;
    for (int i = 0; i < 14; i++) total_br += bitrate_count[i];
    ASSERT(total_br > 0, "bitrate_hist should have frames");

    int stereo_mode_count[4] = {0};
    lame_stereo_mode_hist(gfp, stereo_mode_count);
    int total_sm = 0;
    for (int i = 0; i < 4; i++) total_sm += stereo_mode_count[i];
    ASSERT(total_sm > 0, "stereo_mode_hist should have frames");

    int btype_count[6] = {0};
    lame_block_type_hist(gfp, btype_count);
    int total_bt = 0;
    for (int i = 0; i < 6; i++) total_bt += btype_count[i];
    ASSERT(total_bt > 0, "block_type_hist should have frames");

    int bitrate_stmode[14][4];
    memset(bitrate_stmode, 0, sizeof(bitrate_stmode));
    lame_bitrate_stereo_mode_hist(gfp, bitrate_stmode);

    int bitrate_btype[14][6];
    memset(bitrate_btype, 0, sizeof(bitrate_btype));
    lame_bitrate_block_type_hist(gfp, bitrate_btype);

    lame_close(gfp);

    TEST("histogram queries after encode"); PASS();
}

/* ── print_config / print_internals (P4) ────────────────────────────────── */

static void
test_print_config(void)
{
    lame_global_flags *gfp = lame_init();
    ASSERT(gfp != NULL, "lame_init failed");
    lame_set_in_samplerate(gfp, 44100);
    lame_set_num_channels(gfp, 1);
    lame_set_VBR(gfp, vbr_mtrh);
    lame_set_VBR_q(gfp, 9);
    ASSERT(lame_init_params(gfp) >= 0, "lame_init_params failed");

    lame_print_config(gfp);
    lame_print_internals(gfp);

    short pcm[1152];
    unsigned char buf[8192];
    memset(pcm, 0, sizeof(pcm));
    lame_encode_buffer(gfp, pcm, pcm, 1152, buf, sizeof(buf));

    lame_print_internals(gfp);

    lame_encode_flush_nogap(gfp, buf, sizeof(buf));
    lame_close(gfp);

    TEST("lame_print_config / lame_print_internals"); PASS();
}

/* ── ID3 tag remaining (P2) ─────────────────────────────────────────────── */

static int genre_count;
static char const *last_genre_name;

static void
genre_list_callback(int num, const char *name, void *cookie)
{
    (void)cookie;
    genre_count++;
    if (num == 0) last_genre_name = name;
}

static void
test_id3tag_genre_list(void)
{
    genre_count = 0;
    last_genre_name = NULL;
    id3tag_genre_list(genre_list_callback, NULL);
    ASSERT(genre_count > 0, "genre list should have entries");
    ASSERT(last_genre_name != NULL, "genre 0 should have a name");
    TEST("id3tag_genre_list callback"); PASS();
}

static void
test_id3tag_albumart(void)
{
    lame_global_flags *gfp = lame_init();
    ASSERT(gfp != NULL, "lame_init failed");
    lame_set_in_samplerate(gfp, 44100);
    lame_set_num_channels(gfp, 1);
    lame_set_write_id3tag_automatic(gfp, 0);
    id3tag_add_v2(gfp);

    /* a minimal valid JPEG works; just use a few bytes */
    unsigned char art[] = { 0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 'J','F','I','F',0 };
    int ret = id3tag_set_albumart(gfp, (const char *)art, sizeof(art));
    ASSERT(ret == 0, "id3tag_set_albumart should succeed");

    ASSERT(lame_init_params(gfp) >= 0, "lame_init_params failed");

    short pcm[1152];
    unsigned char buf[8192];
    memset(pcm, 0, sizeof(pcm));
    lame_encode_buffer(gfp, pcm, pcm, 1152, buf, sizeof(buf));
    lame_encode_flush_nogap(gfp, buf, sizeof(buf));

    unsigned char tag[8192];
    size_t n = lame_get_id3v2_tag(gfp, tag, sizeof(tag));
    ASSERT(n > 0, "id3v2 tag should be non-empty with album art");

    lame_close(gfp);
    TEST("id3tag_set_albumart round-trip"); PASS();
}

static void
test_id3tag_padding_control(void)
{
    lame_global_flags *gfp = lame_init();
    ASSERT(gfp != NULL, "lame_init failed");
    lame_set_in_samplerate(gfp, 44100);
    lame_set_num_channels(gfp, 1);
    lame_set_write_id3tag_automatic(gfp, 0);

    id3tag_v2_only(gfp);
    id3tag_set_pad(gfp, 256);
    id3tag_set_title(gfp, "Pad Test");

    ASSERT(lame_init_params(gfp) >= 0, "lame_init_params failed");

    short pcm[1152];
    unsigned char buf[8192];
    memset(pcm, 0, sizeof(pcm));
    lame_encode_buffer(gfp, pcm, pcm, 1152, buf, sizeof(buf));
    lame_encode_flush_nogap(gfp, buf, sizeof(buf));

    unsigned char tag[8192];
    size_t n = lame_get_id3v2_tag(gfp, tag, sizeof(tag));
    ASSERT(n > 0, "id3v2 tag with padding should be non-empty");

    lame_close(gfp);
    TEST("id3tag padding and v2_only"); PASS();
}

static void
test_id3tag_utf8_mode(void)
{
    lame_global_flags *gfp = lame_init();
    ASSERT(gfp != NULL, "lame_init failed");
    lame_set_in_samplerate(gfp, 44100);
    lame_set_num_channels(gfp, 1);
    lame_set_write_id3tag_automatic(gfp, 0);

    id3tag_add_v2_4_UTF8(gfp);
    id3tag_set_title(gfp, "UTF-8 Title");
    id3tag_set_artist(gfp, "UTF-8 Artist");

    ASSERT(lame_init_params(gfp) >= 0, "lame_init_params failed");

    short pcm[1152];
    unsigned char buf[8192];
    memset(pcm, 0, sizeof(pcm));
    lame_encode_buffer(gfp, pcm, pcm, 1152, buf, sizeof(buf));
    lame_encode_flush_nogap(gfp, buf, sizeof(buf));

    unsigned char tag[8192];
    size_t n = lame_get_id3v2_tag(gfp, tag, sizeof(tag));
    ASSERT(n > 0, "id3v2 UTF-8 tag should be non-empty");

    lame_close(gfp);
    TEST("id3tag v2.4 UTF-8 mode"); PASS();
}

static void
test_id3tag_custom_frames(void)
{
    lame_global_flags *gfp = lame_init();
    ASSERT(gfp != NULL, "lame_init failed");
    lame_set_in_samplerate(gfp, 44100);
    lame_set_num_channels(gfp, 1);
    lame_set_write_id3tag_automatic(gfp, 0);
    id3tag_add_v2(gfp);

    id3tag_set_fieldvalue(gfp, "TIT2=Custom Field Title");
    id3tag_set_textinfo_latin1(gfp, "TCOP", "Custom Copyright");
    id3tag_set_comment_latin1(gfp, "eng", "Desc", "A comment");

    ASSERT(lame_init_params(gfp) >= 0, "lame_init_params failed");

    short pcm[1152];
    unsigned char buf[8192];
    memset(pcm, 0, sizeof(pcm));
    lame_encode_buffer(gfp, pcm, pcm, 1152, buf, sizeof(buf));
    lame_encode_flush_nogap(gfp, buf, sizeof(buf));

    unsigned char tag[8192];
    size_t n = lame_get_id3v2_tag(gfp, tag, sizeof(tag));
    ASSERT(n > 0, "id3v2 custom frame tag should be non-empty");

    lame_close(gfp);
    TEST("id3tag custom frames (fieldvalue/textinfo/comment)"); PASS();
}

/* ── remaining P3 set/get round-trips ───────────────────────────────────── */

static void
test_p3_remaining_set_get(void)
{
    lame_global_flags *gfp = lame_init();
    ASSERT(gfp != NULL, "lame_init failed");

    /* num_samples */
    lame_set_num_samples(gfp, 100000);
    ASSERT(lame_get_num_samples(gfp) == 100000UL, "num_samples");

    /* out_samplerate */
    lame_set_out_samplerate(gfp, 22050);
    ASSERT(lame_get_out_samplerate(gfp) == 22050, "out_samplerate");

    /* analysis */
    lame_set_analysis(gfp, 1);
    ASSERT(lame_get_analysis(gfp) == 1, "analysis=1");
    lame_set_analysis(gfp, 0);
    ASSERT(lame_get_analysis(gfp) == 0, "analysis=0");

    /* bWriteVbrTag */
    lame_set_bWriteVbrTag(gfp, 0);
    ASSERT(lame_get_bWriteVbrTag(gfp) == 0, "bWriteVbrTag=0");
    lame_set_bWriteVbrTag(gfp, 1);
    ASSERT(lame_get_bWriteVbrTag(gfp) == 1, "bWriteVbrTag=1");

    /* decode_only */
    lame_set_decode_only(gfp, 1);
    ASSERT(lame_get_decode_only(gfp) == 1, "decode_only=1");
    lame_set_decode_only(gfp, 0);
    ASSERT(lame_get_decode_only(gfp) == 0, "decode_only=0");

    /* free_format */
    lame_set_free_format(gfp, 1);
    ASSERT(lame_get_free_format(gfp) == 1, "free_format=1");
    lame_set_free_format(gfp, 0);
    ASSERT(lame_get_free_format(gfp) == 0, "free_format=0");

    /* nogap_total / nogap_currentindex */
    lame_set_nogap_total(gfp, 3);
    ASSERT(lame_get_nogap_total(gfp) == 3, "nogap_total");
    lame_set_nogap_currentindex(gfp, 1);
    ASSERT(lame_get_nogap_currentindex(gfp) == 1, "nogap_currentindex");

    /* quant_comp / quant_comp_short */
    lame_set_quant_comp(gfp, 5);
    ASSERT(lame_get_quant_comp(gfp) == 5, "quant_comp");
    lame_set_quant_comp_short(gfp, 8);
    ASSERT(lame_get_quant_comp_short(gfp) == 8, "quant_comp_short");

    /* experimentalX/Y/Z */
    lame_set_experimentalX(gfp, 1);
    ASSERT(lame_get_experimentalX(gfp) == 1, "experimentalX");
    lame_set_experimentalY(gfp, 2);
    ASSERT(lame_get_experimentalY(gfp) == 2, "experimentalY");
    lame_set_experimentalZ(gfp, 3);
    ASSERT(lame_get_experimentalZ(gfp) == 3, "experimentalZ");

    /* exp_nspsytune */
    lame_set_exp_nspsytune(gfp, 12345);
    ASSERT(lame_get_exp_nspsytune(gfp) == 12345, "exp_nspsytune");

    /* msfix */
    lame_set_msfix(gfp, 0.7);
    ASSERT(fabs(lame_get_msfix(gfp) - 0.7) < 0.001, "msfix");

    /* VBR_quality (different from VBR_q!) */
    lame_set_VBR_quality(gfp, 3.5f);
    ASSERT(fabs(lame_get_VBR_quality(gfp) - 3.5f) < 0.1f, "VBR_quality");

    /* VBR_mean_bitrate_kbps */
    lame_set_VBR_mean_bitrate_kbps(gfp, 128);
    ASSERT(lame_get_VBR_mean_bitrate_kbps(gfp) == 128, "VBR_mean_bitrate_kbps");

    /* ATHlower */
    lame_set_ATHlower(gfp, -5.0f);
    ASSERT(fabsf(lame_get_ATHlower(gfp) - (-5.0f)) < 0.001f, "ATHlower");

    /* athaa_type */
    lame_set_athaa_type(gfp, 3);
    ASSERT(lame_get_athaa_type(gfp) == 3, "athaa_type");
    lame_set_athaa_type(gfp, 1);
    ASSERT(lame_get_athaa_type(gfp) == 1, "athaa_type=1");

    /* athaa_sensitivity */
    lame_set_athaa_sensitivity(gfp, -10.0f);
    ASSERT(fabsf(lame_get_athaa_sensitivity(gfp) - (-10.0f)) < 0.001f, "athaa_sensitivity");

    /* allow_diff_short */
    lame_set_allow_diff_short(gfp, 1);
    ASSERT(lame_get_allow_diff_short(gfp) == 1, "allow_diff_short=1");
    lame_set_allow_diff_short(gfp, 0);
    ASSERT(lame_get_allow_diff_short(gfp) == 0, "allow_diff_short=0");

    /* useTemporal */
    lame_set_useTemporal(gfp, 1);
    ASSERT(lame_get_useTemporal(gfp) == 1, "useTemporal=1");
    lame_set_useTemporal(gfp, 0);
    ASSERT(lame_get_useTemporal(gfp) == 0, "useTemporal=0");

    /* interChRatio */
    lame_set_interChRatio(gfp, 0.5f);
    ASSERT(fabsf(lame_get_interChRatio(gfp) - 0.5f) < 0.001f, "interChRatio");

    /* compression_ratio set */
    lame_set_compression_ratio(gfp, 11.0f);

    /* asm_optimizations */
    lame_set_asm_optimizations(gfp, 0, 1);
    lame_set_asm_optimizations(gfp, 1, 0);

    /* lame_set_write_id3tag_automatic + getter */
    lame_set_write_id3tag_automatic(gfp, 0);
    ASSERT(lame_get_write_id3tag_automatic(gfp) == 0, "write_id3tag_automatic=0");
    lame_set_write_id3tag_automatic(gfp, 1);
    ASSERT(lame_get_write_id3tag_automatic(gfp) == 1, "write_id3tag_automatic=1");

    /* bWriteVbrTag getter */
    ASSERT(lame_get_bWriteVbrTag(gfp) == 1, "bWriteVbrTag default=1");

    /* mf_samples_to_encode (should be 0 before encode) */
    ASSERT(lame_get_mf_samples_to_encode(gfp) >= 0, "mf_samples_to_encode");

    lame_close(gfp);
    PASS();
}

static void
test_p3_max_samples(void)
{
    lame_global_flags *gfp = lame_init();
    ASSERT(gfp != NULL, "lame_init failed");
    lame_set_in_samplerate(gfp, 44100);
    lame_set_num_channels(gfp, 1);
    ASSERT(lame_init_params(gfp) >= 0, "lame_init_params failed");

    /* maximum_number_of_samples needs init_params first */
    int max_samp = lame_get_maximum_number_of_samples(gfp, 16384);
    ASSERT(max_samp > 0, "maximum_number_of_samples after init");

    short pcm[1152];
    unsigned char buf[8192];
    memset(pcm, 0, sizeof(pcm));
    lame_encode_buffer(gfp, pcm, pcm, 1152, buf, sizeof(buf));
    lame_encode_flush_nogap(gfp, buf, sizeof(buf));
    lame_close(gfp);
    PASS();
}

static void
test_p3_report_callbacks(void)
{
    lame_global_flags *gfp = lame_init();
    ASSERT(gfp != NULL, "lame_init failed");

    /* set and clear callbacks */
    lame_set_errorf(gfp, NULL);
    lame_set_debugf(gfp, NULL);
    lame_set_msgf(gfp, NULL);
    lame_close(gfp);

    TEST("errorf/debugf/msgf callbacks"); PASS();
}

static void
test_p3_post_encode_gain_queries(void)
{
    lame_global_flags *gfp = lame_init();
    ASSERT(gfp != NULL, "lame_init failed");
    lame_set_in_samplerate(gfp, 44100);
    lame_set_num_channels(gfp, 1);
    lame_set_findReplayGain(gfp, 1);
    ASSERT(lame_init_params(gfp) >= 0, "lame_init_params failed");

    short pcm[1152 * 10];
    unsigned char buf[8192];
    for (int i = 0; i < 1152 * 10; i++)
        pcm[i] = (short)(i * 100);
    lame_encode_buffer(gfp, pcm, pcm, 1152 * 10, buf, sizeof(buf));
    lame_encode_flush_nogap(gfp, buf, sizeof(buf));

    int rg = lame_get_RadioGain(gfp);
    ASSERT(rg != 0, "RadioGain should be non-zero after RG analysis");

    /* AudiophileGain is a stub (always 0 in this version) */
    int ag = lame_get_AudiophileGain(gfp);
    ASSERT(ag == 0, "AudiophileGain should be 0 (stub)");

    /* PeakSample/noclip may be 0 or negative if DECODE_ON_THE_FLY not compiled in;
       just call the getters to exercise them, no assertions on values */
    lame_get_PeakSample(gfp);
    lame_get_noclipGainChange(gfp);
    lame_get_noclipScale(gfp);

    lame_close(gfp);
    TEST("post-encode gain queries (Radio/Audiophile/Peak/noclip)"); PASS();
}

/* ── Additional ID3 tag encoding variants (P5) ──────────────────────────── */

static void
test_id3tag_utf16_comment(void)
{
    lame_global_flags *gfp = lame_init();
    ASSERT(gfp != NULL, "lame_init failed");
    lame_set_in_samplerate(gfp, 44100);
    lame_set_num_channels(gfp, 1);
    lame_set_write_id3tag_automatic(gfp, 0);
    id3tag_add_v2(gfp);

    unsigned short utf16_title[] = { 'T', 0, 'e', 0, 's', 0, 't', 0, 0, 0 };
    unsigned short utf16_comment[] = { 'C', 0, 'o', 0, 'm', 0, 'm', 0, 0, 0 };

    id3tag_set_textinfo_utf16(gfp, "TIT2", utf16_title);
    id3tag_set_comment_utf16(gfp, "eng", utf16_comment, utf16_comment);
    id3tag_set_fieldvalue_utf16(gfp, utf16_title);

    ASSERT(lame_init_params(gfp) >= 0, "lame_init_params failed");

    short pcm[1152];
    unsigned char buf[8192];
    memset(pcm, 0, sizeof(pcm));
    lame_encode_buffer(gfp, pcm, pcm, 1152, buf, sizeof(buf));
    lame_encode_flush_nogap(gfp, buf, sizeof(buf));

    unsigned char tag[8192];
    size_t n = lame_get_id3v2_tag(gfp, tag, sizeof(tag));
    ASSERT(n > 0, "id3v2 UTF-16 tag should be non-empty");

    lame_close(gfp);
    TEST("id3tag UTF-16 text/comment/fieldvalue"); PASS();
}

static void
test_id3tag_utf8_comment(void)
{
    lame_global_flags *gfp = lame_init();
    ASSERT(gfp != NULL, "lame_init failed");
    lame_set_in_samplerate(gfp, 44100);
    lame_set_num_channels(gfp, 1);
    lame_set_write_id3tag_automatic(gfp, 0);
    id3tag_add_v2(gfp);

    id3tag_set_textinfo_utf8(gfp, "TIT2", "UTF-8 Title");
    id3tag_set_comment_utf8(gfp, "eng", "Desc", "UTF-8 Comment");

    ASSERT(lame_init_params(gfp) >= 0, "lame_init_params failed");

    short pcm[1152];
    unsigned char buf[8192];
    memset(pcm, 0, sizeof(pcm));
    lame_encode_buffer(gfp, pcm, pcm, 1152, buf, sizeof(buf));
    lame_encode_flush_nogap(gfp, buf, sizeof(buf));

    unsigned char tag[8192];
    size_t n = lame_get_id3v2_tag(gfp, tag, sizeof(tag));
    ASSERT(n > 0, "id3v2 UTF-8 tag should be non-empty");

    lame_close(gfp);
    TEST("id3tag UTF-8 textinfo/comment"); PASS();
}

/* ── Preset variants (P3) ───────────────────────────────────────────────── */

static void
test_presets(void)
{
    lame_global_flags *gfp = lame_init();
    ASSERT(gfp != NULL, "lame_init failed");

    lame_set_preset(gfp, 128);
    lame_set_preset(gfp, STANDARD);
    lame_set_preset(gfp, EXTREME);

    lame_close(gfp);
    PASS();
}

/* ── Remaining public API functions (P5) ────────────────────────────────── */

static void
test_get_lame_os_bitness(void)
{
    const char *s = get_lame_os_bitness();
    ASSERT(s != NULL && s[0] != '\0', "os_bitness should be non-empty");
    PASS();
}

static void
test_id3tag_init(void)
{
    lame_global_flags *gfp = lame_init();
    ASSERT(gfp != NULL, "lame_init failed");
    id3tag_init(gfp);
    id3tag_set_title(gfp, "Init Test");
    id3tag_set_artist(gfp, "Init Artist");
    id3tag_add_v2(gfp);
    lame_set_in_samplerate(gfp, 44100);
    lame_set_num_channels(gfp, 1);
    lame_set_write_id3tag_automatic(gfp, 0);
    ASSERT(lame_init_params(gfp) >= 0, "lame_init_params failed");
    short pcm[1152];
    unsigned char buf[8192];
    memset(pcm, 0, sizeof(pcm));
    lame_encode_buffer(gfp, pcm, pcm, 1152, buf, sizeof(buf));
    lame_encode_flush_nogap(gfp, buf, sizeof(buf));
    unsigned char tag[8192];
    size_t n = lame_get_id3v2_tag(gfp, tag, sizeof(tag));
    ASSERT(n > 0, "id3v2 tag after id3tag_init should be non-empty");
    lame_close(gfp);
    TEST("id3tag_init + metadata + v2 tag"); PASS();
}

static void
test_id3tag_v2_4_utf8_only(void)
{
    lame_global_flags *gfp = lame_init();
    ASSERT(gfp != NULL, "lame_init failed");
    id3tag_v2_4_UTF8_only(gfp);
    id3tag_set_artist(gfp, "UTF8 Artist");
    lame_set_in_samplerate(gfp, 44100);
    lame_set_num_channels(gfp, 1);
    lame_set_write_id3tag_automatic(gfp, 0);
    ASSERT(lame_init_params(gfp) >= 0, "lame_init_params failed");
    short pcm[1152];
    unsigned char buf[8192];
    memset(pcm, 0, sizeof(pcm));
    lame_encode_buffer(gfp, pcm, pcm, 1152, buf, sizeof(buf));
    lame_encode_flush_nogap(gfp, buf, sizeof(buf));
    unsigned char tag[8192];
    size_t n = lame_get_id3v2_tag(gfp, tag, sizeof(tag));
    ASSERT(n > 0, "id3v2.4 UTF-8 only tag non-empty");
    lame_close(gfp);
    TEST("id3tag_v2_4_UTF8_only"); PASS();
}

static void
test_id3tag_space_v1(void)
{
    lame_global_flags *gfp = lame_init();
    ASSERT(gfp != NULL, "lame_init failed");
    id3tag_space_v1(gfp);
    id3tag_set_title(gfp, "Space");
    id3tag_set_artist(gfp, "Test");
    id3tag_v1_only(gfp);
    lame_set_in_samplerate(gfp, 44100);
    lame_set_num_channels(gfp, 1);
    ASSERT(lame_init_params(gfp) >= 0, "lame_init_params failed");
    short pcm[1152 * 10];
    unsigned char buf[8192];
    memset(pcm, 0, sizeof(pcm));
    for (int i = 0; i < 10; i++)
        lame_encode_buffer(gfp, pcm + i * 1152, pcm + i * 1152, 1152, buf, sizeof(buf));
    lame_encode_flush_nogap(gfp, buf, sizeof(buf));
    unsigned char tag[128];
    int n = lame_get_id3v1_tag(gfp, tag, sizeof(tag));
    ASSERT(n == 128, "id3v1 tag should be 128 bytes after space_v1");
    lame_close(gfp);
    TEST("id3tag_space_v1"); PASS();
}

static void
test_id3tag_pad_v2(void)
{
    lame_global_flags *gfp = lame_init();
    ASSERT(gfp != NULL, "lame_init failed");
    id3tag_pad_v2(gfp);
    id3tag_set_artist(gfp, "Pad");
    id3tag_add_v2(gfp);
    lame_set_in_samplerate(gfp, 44100);
    lame_set_num_channels(gfp, 1);
    lame_set_write_id3tag_automatic(gfp, 0);
    ASSERT(lame_init_params(gfp) >= 0, "lame_init_params failed");
    short pcm[1152];
    unsigned char buf[8192];
    memset(pcm, 0, sizeof(pcm));
    lame_encode_buffer(gfp, pcm, pcm, 1152, buf, sizeof(buf));
    lame_encode_flush_nogap(gfp, buf, sizeof(buf));
    unsigned char tag[8192];
    size_t n = lame_get_id3v2_tag(gfp, tag, sizeof(tag));
    ASSERT(n > 0, "id3v2 tag after pad_v2 non-empty");
    lame_close(gfp);
    TEST("id3tag_pad_v2"); PASS();
}

static void
test_lame_init_bitstream(void)
{
    lame_global_flags *gfp = make_mono_gfp();
    ASSERT(gfp != NULL, "lame_init failed");
    ASSERT(lame_init_params(gfp) >= 0, "lame_init_params failed");
    lame_init_bitstream(gfp);
    unsigned char buf[8192];
    int ret = lame_encode_flush_nogap(gfp, buf, sizeof(buf));
    ASSERT(ret >= 0, "flush after init_bitstream");
    lame_close(gfp);
    TEST("lame_init_bitstream"); PASS();
}

static void
test_lame_bitrate_kbps(void)
{
    lame_global_flags *gfp = make_mono_gfp();
    ASSERT(gfp != NULL, "lame_init failed");
    ASSERT(lame_init_params(gfp) >= 0, "lame_init_params failed");

    int bitrate_kbps[14] = {0};
    lame_bitrate_kbps(gfp, bitrate_kbps);
    ASSERT(bitrate_kbps[0] > 0, "bitrate_kbps[0] should be > 0");

    short pcm[1152];
    unsigned char buf[8192];
    memset(pcm, 0, sizeof(pcm));
    lame_encode_buffer(gfp, pcm, pcm, 1152, buf, sizeof(buf));
    lame_close(gfp);
    TEST("lame_bitrate_kbps"); PASS();
}

static void
test_lame_encode_buffer_long(void)
{
    lame_global_flags *gfp = make_mono_gfp();
    ASSERT(gfp != NULL, "lame_init failed");
    ASSERT(lame_init_params(gfp) >= 0, "lame_init_params failed");

    long pcm[1152];
    for (int i = 0; i < 1152; i++)
        pcm[i] = (long)(i * 100);
    unsigned char buf[8192];
    int ret = lame_encode_buffer_long(gfp, pcm, NULL, 1152, buf, sizeof(buf));
    ASSERT(ret >= 0, "lame_encode_buffer_long mono failed");
    lame_close(gfp);
    TEST("lame_encode_buffer_long"); PASS();
}

static void
test_lame_encode_buffer_interleaved_int(void)
{
    lame_global_flags *gfp = lame_init();
    ASSERT(gfp != NULL, "lame_init failed");
    lame_set_in_samplerate(gfp, 44100);
    lame_set_num_channels(gfp, 2);
    ASSERT(lame_init_params(gfp) >= 0, "lame_init_params failed");

    int pcm[1152 * 2];
    for (int i = 0; i < 1152 * 2; i++)
        pcm[i] = i * 65536;
    unsigned char buf[8192];
    int ret = lame_encode_buffer_interleaved_int(gfp, pcm, 1152, buf, sizeof(buf));
    ASSERT(ret >= 0, "lame_encode_buffer_interleaved_int failed");
    lame_close(gfp);
    TEST("lame_encode_buffer_interleaved_int"); PASS();
}

static void
test_lame_encode_flush(void)
{
    lame_global_flags *gfp = make_mono_gfp();
    ASSERT(gfp != NULL, "lame_init failed");
    ASSERT(lame_init_params(gfp) >= 0, "lame_init_params failed");

    short pcm[1152];
    unsigned char buf[8192];
    memset(pcm, 0, sizeof(pcm));
    lame_encode_buffer(gfp, pcm, pcm, 1152, buf, sizeof(buf));

    int ret = lame_encode_flush(gfp, buf, sizeof(buf));
    ASSERT(ret >= 0, "lame_encode_flush after encode failed");
    lame_close(gfp);
    TEST("lame_encode_flush"); PASS();
}

/* ── hip decoder tests (P5) ─────────────────────────────────────────────── */

/* The hip decoder is a stub unless HAVE_MPG123 is defined.
   We still exercise the init/exit and verify the decode returns -1. */
static void
test_hip_decode_stubs(void)
{
    /* init/exit round-trip */
    hip_t hip = hip_decode_init();
    hip_t hip_headers;
    ASSERT(hip != NULL, "hip_decode_init should return non-NULL");

    for (int i = 0; i < 128; i++) {
        hip_t probe = hip_decode_init_gapless();
#ifdef HAVE_MPG123
        ASSERT(probe != NULL, "hip_decode_init_gapless should succeed with mpg123");
        hip_decode_exit(probe);
#else
        ASSERT(probe == NULL, "hip_decode_init_gapless should return NULL without mpg123");
#endif
    }

    hip_t hip2 = hip_decode_init_gapless();

    /* call reporting stubs (no crash) */
    hip_set_errorf(hip, NULL);
    hip_set_debugf(hip, NULL);
    hip_set_msgf(hip, NULL);

    /* Encode a few frames of MP3 data to feed to the decoder */
    lame_global_flags *gfp = lame_init();
    ASSERT(gfp != NULL, "lame_init failed");
    lame_set_in_samplerate(gfp, 44100);
    lame_set_num_channels(gfp, 2);
    lame_set_brate(gfp, 128);
    ASSERT(lame_init_params(gfp) >= 0, "lame_init_params failed");

    short pcm[1152 * 4];
    unsigned char mp3buf[8192];
    size_t mp3len = 0;
    for (int i = 0; i < 1152 * 4; i++)
        pcm[i] = (short)(i * 200);
    for (int i = 0; i < 4; i++) {
        int ret = lame_encode_buffer(gfp, pcm + i * 1152, pcm + i * 1152,
                                     1152, mp3buf + mp3len, (int)(sizeof(mp3buf) - mp3len));
        if (ret > 0) mp3len += (size_t)ret;
    }
    int flush_ret = lame_encode_flush_nogap(gfp, mp3buf + mp3len,
                                            (int)(sizeof(mp3buf) - mp3len));
    if (flush_ret > 0) mp3len += (size_t)flush_ret;
    lame_close(gfp);

    ASSERT(mp3len > 0, "encoded mp3 should be non-empty");

    /*
     * hip_decode() is specified to receive one MPEG frame per call.  This CBR fixture has
     * 417-byte frames plus an optional padding byte; keeping the input to one
     * frame bounds each PCM channel to 1152 samples as documented.
     */
    ASSERT(mp3len >= 3, "encoded MP3 should contain a complete header");
    size_t const mp3frame_len = 417 + ((mp3buf[2] >> 1) & 1);
    short dec_l[1152], dec_r[1152];
    mp3data_struct md;
    memset(&md, 0, sizeof(md));
    ASSERT(mp3len >= mp3frame_len, "encoded MP3 should contain one complete frame");

    int ret;

    ret = hip_decode(hip, mp3buf, mp3frame_len, dec_l, dec_r);
#ifdef HAVE_MPG123
    ASSERT(ret >= 0, "hip_decode should not fail with mpg123");
#else
    ASSERT(ret < 0, "hip_decode should return -1 (no mpg123)");
#endif

    /*
     * Decoder handles are streaming state machines.  The first call has
     * consumed this input, so use a fresh handle for the independent headers
     * check rather than feeding the same stream twice.
     */
    hip_headers = hip_decode_init();
    ASSERT(hip_headers != NULL, "hip_decode_init for headers should succeed");
    ret = hip_decode_headers(hip_headers, mp3buf, mp3frame_len, dec_l, dec_r, &md);
#ifdef HAVE_MPG123
    ASSERT(ret >= 0, "hip_decode_headers should not fail with mpg123");
#else
    ASSERT(ret < 0, "hip_decode_headers should return -1 (no mpg123)");
#endif
    hip_decode_exit(hip_headers);

    if (hip2) {
        ret = hip_decode1(hip2, mp3buf, mp3frame_len, dec_l, dec_r);
#ifdef HAVE_MPG123
        ASSERT(ret >= 0, "hip_decode1 should not fail with mpg123");
#else
        ASSERT(ret < 0, "hip_decode1 should return -1 (no mpg123)");
#endif

        hip_decode_exit(hip2);
        hip2 = hip_decode_init_gapless();
        ASSERT(hip2 != NULL, "hip_decode_init_gapless should remain available");
        ret = hip_decode1_headers(hip2, mp3buf, mp3frame_len, dec_l, dec_r, &md);
#ifdef HAVE_MPG123
        ASSERT(ret >= 0, "hip_decode1_headers should not fail with mpg123");
#else
        ASSERT(ret < 0, "hip_decode1_headers should return -1 (no mpg123)");
#endif

        hip_decode_exit(hip2);
        hip2 = hip_decode_init_gapless();
        ASSERT(hip2 != NULL, "hip_decode_init_gapless should remain available");
        int enc_delay, enc_padding;
        ret = hip_decode1_headersB(hip2, mp3buf, mp3frame_len, dec_l, dec_r,
                                   &md, &enc_delay, &enc_padding);
#ifdef HAVE_MPG123
        ASSERT(ret >= 0, "hip_decode1_headersB should not fail with mpg123");
#else
        ASSERT(ret < 0, "hip_decode1_headersB should return -1 (no mpg123)");
#endif

        hip_decode_exit(hip2);
    }
    hip_decode_exit(hip);
    PASS();
}

static void
test_lame_mp3_tags_fid(void)
{
    lame_global_flags *gfp = lame_init();
    ASSERT(gfp != NULL, "lame_init failed");
    lame_set_in_samplerate(gfp, 44100);
    lame_set_num_channels(gfp, 1);
    lame_set_brate(gfp, 128);
    lame_set_VBR(gfp, vbr_abr);
    lame_set_bWriteVbrTag(gfp, 1);
    ASSERT(lame_init_params(gfp) >= 0, "lame_init_params failed");

    short pcm[1152 * 10];
    unsigned char encbuf[8192 * 2];
    size_t total = 0;
    for (int i = 0; i < 1152 * 10; i++)
        pcm[i] = (short)(i * 100);
    for (int i = 0; i < 10; i++) {
        int ret = lame_encode_buffer(gfp, pcm + i * 1152, NULL, 1152,
                                     encbuf + total, (int)(sizeof(encbuf) - total));
        if (ret > 0) total += (size_t)ret;
    }
    int flush_ret = lame_encode_flush_nogap(gfp, encbuf + total, (int)(sizeof(encbuf) - total));
    if (flush_ret > 0) total += (size_t)flush_ret;
    ASSERT(total > 0, "encoded stream should be non-empty");

    unsigned char lametag[4096];
    size_t lametag_len = lame_get_lametag_frame(gfp, lametag, sizeof(lametag));
    ASSERT(lametag_len > 0, "lame_get_lametag_frame should return data");
    ASSERT(lametag_len <= total, "lametag frame should fit within encoded stream");

    FILE *f = tmpfile();
    ASSERT(f != NULL, "tmpfile failed");
    fwrite(encbuf, 1, total, f);
    fflush(f);

    lame_mp3_tags_fid(gfp, f);

    /* rewind and verify the rewritten header matches the exported LAME tag frame */
    rewind(f);
    unsigned char check[4096];
    size_t nread = fread(check, 1, lametag_len, f);
    ASSERT(nread == lametag_len, "lame_mp3_tags_fid should leave the file readable");
    ASSERT(memcmp(check, lametag, lametag_len) == 0, "lame_mp3_tags_fid should rewrite the LAME tag frame");

    fclose(f);
    lame_close(gfp);
    TEST("lame_mp3_tags_fid VBR tag rewrite"); PASS();
}

/* ── Additional edge case tests (P5) ─────────────────────────────────────── */

static void
test_set_get_athaa(void)
{
    lame_global_flags *gfp = lame_init();
    ASSERT(gfp != NULL, "lame_init failed");

    lame_set_athaa_type(gfp, 3);
    ASSERT(lame_get_athaa_type(gfp) == 3, "athaa_type=3");
    lame_set_athaa_type(gfp, 1);
    ASSERT(lame_get_athaa_type(gfp) == 1, "athaa_type=1");

    lame_set_athaa_sensitivity(gfp, 5.0f);
    ASSERT(fabsf(lame_get_athaa_sensitivity(gfp) - 5.0f) < 0.001f, "athaa_sensitivity");

    /* no getter for asm optimizations; just exercise setter (no crash) */
    lame_set_asm_optimizations(gfp, MMX, 1);
    lame_set_asm_optimizations(gfp, AMD_3DNOW, 0);
    lame_set_asm_optimizations(gfp, SSE, 1);

    lame_close(gfp);
    PASS();
}

static void
test_id3tag_edge_cases(void)
{
    lame_global_flags *gfp = lame_init();
    ASSERT(gfp != NULL, "lame_init failed");

    /* track number 0 should return -1 (out of ID3v1 range) */
    int ret_track = id3tag_set_track(gfp, "0");
    ASSERT(ret_track == -1, "track 0 should be out of range");

    /* 255 is valid (1-255 range); test out-of-range with 256 */
    ret_track = id3tag_set_track(gfp, "256");
    ASSERT(ret_track == -1, "track 256 should be out of range");

    /* valid track number */
    ret_track = id3tag_set_track(gfp, "1");
    ASSERT(ret_track == 0, "track 1 should be valid");

    /* year with various formats */
    id3tag_set_year(gfp, "2024");
    id3tag_set_artist(gfp, "EdgeCase");
    id3tag_set_comment(gfp, "");
    id3tag_add_v2(gfp);

    lame_set_in_samplerate(gfp, 44100);
    lame_set_num_channels(gfp, 1);
    lame_set_write_id3tag_automatic(gfp, 0);
    ASSERT(lame_init_params(gfp) >= 0, "lame_init_params failed");

    short pcm[1152];
    unsigned char buf[8192];
    memset(pcm, 0, sizeof(pcm));
    lame_encode_buffer(gfp, pcm, pcm, 1152, buf, sizeof(buf));
    lame_encode_flush_nogap(gfp, buf, sizeof(buf));

    unsigned char tag[8192];
    size_t n = lame_get_id3v2_tag(gfp, tag, sizeof(tag));
    ASSERT(n > 0, "id3v2 tag should be non-empty");

    lame_close(gfp);
    PASS();
}

static void
test_free_format_encode(void)
{
    lame_global_flags *gfp = lame_init();
    ASSERT(gfp != NULL, "lame_init failed");
    lame_set_in_samplerate(gfp, 44100);
    lame_set_num_channels(gfp, 1);
    lame_set_free_format(gfp, 1);
    ASSERT(lame_get_free_format(gfp) == 1, "free_format=1");
    ASSERT(lame_init_params(gfp) >= 0, "lame_init_params free_format");

    short pcm[1152];
    unsigned char buf[8192];
    memset(pcm, 0, sizeof(pcm));
    int ret = lame_encode_buffer(gfp, pcm, pcm, 1152, buf, sizeof(buf));
    ASSERT(ret >= 0, "free_format encode should succeed");

    ret = lame_encode_flush_nogap(gfp, buf, sizeof(buf));
    ASSERT(ret >= 0, "free_format flush should succeed");
    lame_close(gfp);

    /* also test lame_bitrate_kbps with free_format */
    gfp = lame_init();
    ASSERT(gfp != NULL, "lame_init failed");
    lame_set_in_samplerate(gfp, 44100);
    lame_set_num_channels(gfp, 1);
    lame_set_free_format(gfp, 1);
    ASSERT(lame_init_params(gfp) >= 0, "lame_init_params free_format");

    memset(pcm, 0, sizeof(pcm));
    lame_encode_buffer(gfp, pcm, pcm, 1152, buf, sizeof(buf));
    lame_encode_flush_nogap(gfp, buf, sizeof(buf));

    int bitrate_kbps[14] = {0};
    lame_bitrate_kbps(gfp, bitrate_kbps);
    /* free_format sets bitrate_kbps[0] to avg_bitrate */
    ASSERT(bitrate_kbps[0] > 0, "free_format bitrate_kbps[0] should be > 0");

    lame_close(gfp);
    PASS();
}

/* ── Encoder error path tests (P5) ───────────────────────────────────────── */

static void
test_encode_edge_cases(void)
{
    lame_global_flags *gfp = make_mono_gfp();
    ASSERT(gfp != NULL, "lame_init failed");
    ASSERT(lame_init_params(gfp) >= 0, "lame_init_params failed");

    short pcm[1152];
    unsigned char buf[8192];
    memset(pcm, 0, sizeof(pcm));
    int ret;

    /* nsamples == 0 should return 0 */
    ret = lame_encode_buffer(gfp, pcm, pcm, 0, buf, sizeof(buf));
    ASSERT(ret == 0, "encode with nsamples=0 should return 0");

    /* NULL left buffer (mono) should return 0 */
    ret = lame_encode_buffer(gfp, NULL, NULL, 1152, buf, sizeof(buf));
    ASSERT(ret == 0, "encode with NULL buffer should return 0");

    /* encode a frame, then flush twice (second flush returns 0) */
    ret = lame_encode_buffer(gfp, pcm, pcm, 1152, buf, sizeof(buf));
    ASSERT(ret >= 0, "encode frame");
    ret = lame_encode_flush(gfp, buf, sizeof(buf));
    ASSERT(ret >= 0, "first flush");
    ret = lame_encode_flush(gfp, buf, sizeof(buf));
    ASSERT(ret == 0, "second flush should return 0 (already flushed)");

    lame_close(gfp);
    PASS();
}

static void
test_vbr_mode_variants(void)
{
    static int const modes[] = { vbr_mtrh, vbr_abr, vbr_default };
    for (size_t m = 0; m < sizeof(modes) / sizeof(modes[0]); m++) {
        lame_global_flags *gfp = lame_init();
        ASSERT(gfp != NULL, "lame_init failed");
        lame_set_in_samplerate(gfp, 44100);
        lame_set_num_channels(gfp, 1);
        lame_set_VBR(gfp, modes[m]);
        if (modes[m] == vbr_abr)
            lame_set_brate(gfp, 128);
        ASSERT(lame_init_params(gfp) >= 0, "lame_init_params");

        short pcm[1152];
        unsigned char buf[8192];
        memset(pcm, 0, sizeof(pcm));
        ASSERT(lame_encode_buffer(gfp, pcm, pcm, 1152, buf, sizeof(buf)) >= 0, "encode");

        int flush_ret = lame_encode_flush_nogap(gfp, buf, sizeof(buf));
        ASSERT(flush_ret >= 0, "flush_nogap");

        /* get VBR header frame (Xing/Info tag) */
        unsigned char lametag[4096];
        int n = lame_get_lametag_frame(gfp, lametag, sizeof(lametag));
        ASSERT(n >= 0, "lametag frame");

        lame_close(gfp);
    }
    PASS();
}

static void
test_init_params_edge(void)
{
    /* samplerate = 0 should not fail (silent fallback) */
    lame_global_flags *gfp = lame_init();
    ASSERT(gfp != NULL, "lame_init failed");
    lame_set_in_samplerate(gfp, 0);
    lame_set_num_channels(gfp, 1);
    ASSERT(lame_init_params(gfp) >= 0, "lame_init_params with samplerate=0 should succeed");
    lame_close(gfp);

    /* VBR_q out of range [0..9] should be clamped or silently accepted */
    gfp = lame_init();
    ASSERT(gfp != NULL, "lame_init failed");
    lame_set_in_samplerate(gfp, 44100);
    lame_set_num_channels(gfp, 1);
    lame_set_VBR_q(gfp, 99);
    ASSERT(lame_init_params(gfp) >= 0, "lame_init_params with VBR_q=99 should succeed");
    lame_close(gfp);

    /* out_samplerate explicitly set to 0 should not crash */
    gfp = lame_init();
    ASSERT(gfp != NULL, "lame_init failed");
    lame_set_in_samplerate(gfp, 44100);
    lame_set_out_samplerate(gfp, 0);
    lame_set_num_channels(gfp, 2);
    ASSERT(lame_init_params(gfp) >= 0, "lame_init_params with out_samplerate=0");
    lame_close(gfp);

    /* very low samplerate (8000 Hz) */
    gfp = lame_init();
    ASSERT(gfp != NULL, "lame_init failed");
    lame_set_in_samplerate(gfp, 8000);
    lame_set_num_channels(gfp, 1);
    ASSERT(lame_init_params(gfp) >= 0, "lame_init_params samplerate=8000");
    short pcm[576];
    unsigned char buf[8192];
    memset(pcm, 0, sizeof(pcm));
    ASSERT(lame_encode_buffer(gfp, pcm, pcm, 576, buf, sizeof(buf)) >= 0, "encode 8kHz");
    lame_close(gfp);

    PASS();
}

static void
test_id3tag_year_edge(void)
{
    lame_global_flags *gfp = lame_init();
    ASSERT(gfp != NULL, "lame_init failed");

    /* year "0" should not crash */
    id3tag_set_year(gfp, "0");
    id3tag_set_year(gfp, "");
    id3tag_set_year(gfp, "invalid");
    id3tag_set_title(gfp, "Year Test");
    id3tag_add_v2(gfp);

    lame_set_in_samplerate(gfp, 44100);
    lame_set_num_channels(gfp, 1);
    lame_set_write_id3tag_automatic(gfp, 0);
    ASSERT(lame_init_params(gfp) >= 0, "lame_init_params");

    short pcm[1152];
    unsigned char buf[8192];
    memset(pcm, 0, sizeof(pcm));
    lame_encode_buffer(gfp, pcm, pcm, 1152, buf, sizeof(buf));
    lame_encode_flush_nogap(gfp, buf, sizeof(buf));

    unsigned char tag[8192];
    size_t n = lame_get_id3v2_tag(gfp, tag, sizeof(tag));
    ASSERT(n > 0, "id3v2 tag should be non-empty");

    lame_close(gfp);
    PASS();
}

/* ── ReplayGain at various sample rates (P5) ────────────────────────────── */

static void
test_replaygain_sample_rates(void)
{
    static int const srates[] = { 48000, 32000, 24000, 22050, 16000, 12000, 11025, 8000 };
    for (size_t i = 0; i < sizeof(srates) / sizeof(srates[0]); i++) {
        lame_global_flags *gfp = lame_init();
        ASSERT(gfp != NULL, "lame_init failed");
        lame_set_in_samplerate(gfp, srates[i]);
        lame_set_num_channels(gfp, 1);
        lame_set_findReplayGain(gfp, 1);
        ASSERT(lame_init_params(gfp) >= 0, "lame_init_params");

        short pcm[1152];
        unsigned char buf[8192];
        memset(pcm, 0, sizeof(pcm));
        int ret = lame_encode_buffer(gfp, pcm, pcm, 1152, buf, sizeof(buf));
        if (ret < 0) {
            char msg[64];
            snprintf(msg, sizeof(msg), "encode at %d Hz", srates[i]);
            FAIL(msg);
            lame_close(gfp);
            return;
        }
        lame_close(gfp);
    }
    PASS();
}

static void
test_encode_flush_with_id3v1(void)
{
    lame_global_flags *gfp = lame_init();
    ASSERT(gfp != NULL, "lame_init failed");
    lame_set_in_samplerate(gfp, 44100);
    lame_set_num_channels(gfp, 1);
    lame_set_write_id3tag_automatic(gfp, 1);
    id3tag_set_title(gfp, "Flush ID3v1");
    id3tag_set_artist(gfp, "Test");
    id3tag_v1_only(gfp);
    ASSERT(lame_init_params(gfp) >= 0, "lame_init_params");

    short pcm[1152 * 10];
    unsigned char buf[8192];
    memset(pcm, 0, sizeof(pcm));
    for (int i = 0; i < 10; i++) {
        lame_encode_buffer(gfp, pcm + i * 1152, pcm + i * 1152, 1152, buf, sizeof(buf));
    }
    int ret = lame_encode_flush(gfp, buf, sizeof(buf));
    ASSERT(ret >= 0, "lame_encode_flush with write_id3tag_automatic");

    unsigned char tag[128];
    int n = lame_get_id3v1_tag(gfp, tag, sizeof(tag));
    ASSERT(n == 128, "id3v1 tag should be 128 bytes");

    lame_close(gfp);
    PASS();
}

/* ── main ───────────────────────────────────────────────────────────────── */

int
main(void)
{
    printf("=== P0: tables.c ===\n");
    test_lame_get_bitrate();
    test_lame_get_samplerate();

    printf("\n=== P0: version.c ===\n");
    test_version_strings();
    test_lame_version_numerical();

    printf("\n=== P0: encoder/lame_init error paths ===\n");
    test_lame_init_invalid_params();

    printf("\n=== P1: lame_encode_buffer variants ===\n");
    TEST("lame_encode_buffer (short)"); test_encode_short(); PASS();
    TEST("lame_encode_buffer_float");  test_encode_float(); PASS();
    TEST("lame_encode_buffer_int");    test_encode_int(); PASS();
    TEST("lame_encode_buffer_long2");   test_encode_long2(); PASS();
    TEST("lame_encode_buffer_interleaved");  test_encode_interleaved(); PASS();
    TEST("lame_encode_buffer_ieee_float");   test_encode_ieee_float(); PASS();
    TEST("lame_encode_buffer_interleaved_ieee_float"); test_encode_interleaved_ieee_float(); PASS();
    TEST("lame_encode_buffer_ieee_double");  test_encode_ieee_double(); PASS();
    TEST("lame_encode_buffer_interleaved_ieee_double"); test_encode_interleaved_ieee_double(); PASS();

    printf("\n=== P1: flush / close ===\n");
    test_encode_flush_nogap();
    test_encode_flush_nogap_after_encode();
    TEST("lame_close"); test_lame_close();

    printf("\n=== P3: set / get round-trips ===\n");
    TEST("brate");           test_set_get_brate();
    TEST("vbr mode");        test_set_get_vbr();
    TEST("channel mode");    test_set_get_mode();
    TEST("quality");         test_set_get_quality();
    TEST("samplerate queries"); test_set_get_samplerate();
    TEST("num_channels");    test_set_get_num_channels();
    TEST("framesize");       test_set_get_frame_size();
    TEST("error_protection"); test_set_get_error_protection();
    TEST("copyright");       test_set_get_copyright();
    TEST("original");        test_set_get_original();
    TEST("extension");       test_set_get_extension();
    TEST("strict_ISO");      test_set_get_strict_iso();
    TEST("disable_reservoir"); test_set_get_disable_reservoir();
    TEST("scale");           test_set_get_scale();
    TEST("force_ms");        test_set_get_force_ms();
    TEST("lowpassfreq");     test_set_get_lowpassfreq();
    TEST("highpassfreq");    test_set_get_highpassfreq();
    TEST("ATH parameters");  test_set_get_ath_params();
    TEST("preset");          test_set_get_preset();
    TEST("VBR quality");     test_set_get_vbr_quality();
    TEST("compression_ratio"); test_set_get_compression_ratio();
    TEST("VBR min/max bitrate"); test_set_get_vbr_min_max();
    test_v0_default_floor_policy();

    printf("\n=== P2: ID3 tag round-trips ===\n");
    test_id3tag_roundtrip();
    test_id3tag_v1_roundtrip();
    printf("\n=== P5: ID3 tag encoding variants ===\n");
    test_id3tag_utf16_comment();
    test_id3tag_utf8_comment();

    printf("\n=== P3: additional set/get ===\n");
    TEST("emphasis");         test_set_get_emphasis();
    TEST("scale_left/right"); test_set_get_scale_left_right();
    TEST("short blocks");     test_set_get_short_blocks();
    TEST("filter width");     test_set_get_filter_width();
    TEST("replaygain");       test_set_get_replaygain();
    TEST("VBR hard min");     test_set_get_vbr_hard_min();

    printf("\n=== P3: post-encode info queries ===\n");
    test_post_encode_info();

    printf("\n=== P2: ID3 tag remaining ===\n");
    test_id3tag_genre_list();
    test_id3tag_albumart();
    test_id3tag_padding_control();
    test_id3tag_utf8_mode();
    test_id3tag_custom_frames();

    printf("\n=== P1: old VBR (vbr_rh) ===\n");
    test_encode_vbr_rh_mono();
    test_encode_vbr_rh_stereo();
    test_encode_vbr_rh_low_sr();

    printf("\n=== P4: histogram queries ===\n");
    test_histogram_queries();

    printf("\n=== P4: print_config / print_internals ===\n");
    test_print_config();

    printf("\n=== P3: remaining set/get ===\n");
    TEST("remaining set/get round-trips"); test_p3_remaining_set_get();
    TEST("max samples after init");        test_p3_max_samples();
    TEST("presets");                        test_presets();
    test_p3_report_callbacks();
    test_p3_post_encode_gain_queries();

    printf("\n=== P5: remaining public API functions ===\n");
    TEST("get_lame_os_bitness");                test_get_lame_os_bitness(); PASS();
    TEST("id3tag_init");                        test_id3tag_init(); PASS();
    TEST("id3tag_v2_4_UTF8_only");              test_id3tag_v2_4_utf8_only(); PASS();
    TEST("id3tag_space_v1");                    test_id3tag_space_v1(); PASS();
    TEST("id3tag_pad_v2");                      test_id3tag_pad_v2(); PASS();
    TEST("lame_init_bitstream");                test_lame_init_bitstream(); PASS();
    TEST("lame_bitrate_kbps");                  test_lame_bitrate_kbps(); PASS();
    TEST("lame_encode_buffer_long");            test_lame_encode_buffer_long(); PASS();
    TEST("lame_encode_buffer_interleaved_int"); test_lame_encode_buffer_interleaved_int(); PASS();
    TEST("lame_encode_flush");                  test_lame_encode_flush(); PASS();
    TEST("lame_mp3_tags_fid");                  test_lame_mp3_tags_fid(); PASS();
    TEST("hip_decode stubs");                   test_hip_decode_stubs(); PASS();

    printf("\n=== P5: edge cases ===\n");
    TEST("athaa type/sensitivity + asm optim"); test_set_get_athaa(); PASS();
    TEST("id3tag edge cases");                  test_id3tag_edge_cases(); PASS();
    TEST("free_format encode");                 test_free_format_encode(); PASS();
    TEST("encode edge cases");                  test_encode_edge_cases(); PASS();
    TEST("VBR mode variants");                  test_vbr_mode_variants(); PASS();
    TEST("lame_init_params edge cases");         test_init_params_edge(); PASS();
    TEST("id3tag year edge cases");             test_id3tag_year_edge(); PASS();
    TEST("ReplayGain sample rates");            test_replaygain_sample_rates(); PASS();
    TEST("encode flush with ID3v1");            test_encode_flush_with_id3v1(); PASS();

    printf("\n%s: %d/%d tests passed, %d failed\n",
           tests_failed > 0 ? "FAILED" : "PASSED",
           tests_run - tests_failed, tests_run, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
