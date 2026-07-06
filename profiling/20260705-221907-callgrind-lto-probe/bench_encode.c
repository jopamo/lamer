#include "lame.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__GNUC__) || defined(__clang__)
#define NOINLINE __attribute__((noinline))
#else
#define NOINLINE
#endif

typedef struct {
    short *samples;
    size_t frames;
    int channels;
    int sample_rate;
} pcm_data;

static uint16_t read_le16(FILE *fp)
{
    unsigned char b[2];
    if (fread(b, 1, sizeof(b), fp) != sizeof(b)) {
        fprintf(stderr, "short read\n");
        exit(2);
    }
    return (uint16_t) b[0] | ((uint16_t) b[1] << 8);
}

static uint32_t read_le32(FILE *fp)
{
    unsigned char b[4];
    if (fread(b, 1, sizeof(b), fp) != sizeof(b)) {
        fprintf(stderr, "short read\n");
        exit(2);
    }
    return (uint32_t) b[0]
         | ((uint32_t) b[1] << 8)
         | ((uint32_t) b[2] << 16)
         | ((uint32_t) b[3] << 24);
}

static void skip_bytes(FILE *fp, uint32_t n)
{
    if (fseek(fp, (long) n, SEEK_CUR) != 0) {
        fprintf(stderr, "seek failed\n");
        exit(2);
    }
}

static void load_wav(char const *path, pcm_data *out)
{
    FILE *fp = fopen(path, "rb");
    uint16_t audio_format = 0;
    uint16_t channels = 0;
    uint32_t sample_rate = 0;
    uint16_t bits_per_sample = 0;
    uint32_t data_size = 0;
    int have_fmt = 0;
    int have_data = 0;

    if (!fp) {
        perror(path);
        exit(2);
    }
    if (read_le32(fp) != 0x46464952u || read_le32(fp) == 0 || read_le32(fp) != 0x45564157u) {
        fprintf(stderr, "not a PCM WAVE file: %s\n", path);
        exit(2);
    }

    while (!have_data) {
        uint32_t chunk_id;
        uint32_t chunk_size;
        long data_pos;

        chunk_id = read_le32(fp);
        chunk_size = read_le32(fp);
        data_pos = ftell(fp);
        if (data_pos < 0) {
            fprintf(stderr, "ftell failed\n");
            exit(2);
        }

        if (chunk_id == 0x20746d66u) {
            audio_format = read_le16(fp);
            channels = read_le16(fp);
            sample_rate = read_le32(fp);
            (void) read_le32(fp);
            (void) read_le16(fp);
            bits_per_sample = read_le16(fp);
            have_fmt = 1;
        }
        else if (chunk_id == 0x61746164u) {
            data_size = chunk_size;
            have_data = 1;
            break;
        }

        if (fseek(fp, data_pos + (long) ((chunk_size + 1u) & ~1u), SEEK_SET) != 0) {
            fprintf(stderr, "seek failed\n");
            exit(2);
        }
    }

    if (!have_fmt || !have_data || audio_format != 1 || channels != 2 || bits_per_sample != 16) {
        fprintf(stderr, "unsupported WAV format\n");
        exit(2);
    }

    out->frames = data_size / (size_t) (channels * (bits_per_sample / 8));
    out->channels = channels;
    out->sample_rate = (int) sample_rate;
    out->samples = (short *) malloc(data_size);
    if (!out->samples) {
        fprintf(stderr, "oom\n");
        exit(2);
    }
    if (fread(out->samples, 1, data_size, fp) != data_size) {
        fprintf(stderr, "failed to read PCM payload\n");
        exit(2);
    }

    fclose(fp);
}

static uint64_t fnv1a64_update(uint64_t hash, unsigned char const *buf, size_t len)
{
    size_t i;
    for (i = 0; i < len; ++i) {
        hash ^= buf[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

typedef struct {
    uint64_t hash;
    uint64_t total_bytes;
} encode_result;

static NOINLINE encode_result run_encode(short const *samples, size_t frames, int sample_rate, int channels, int repeats)
{
    lame_t gfp;
    encode_result result;
    unsigned char *mp3buf;
    size_t const chunk_frames = 1152;
    size_t const mp3buf_size = (size_t) (1.25 * chunk_frames + 7200);
    int r;

    result.hash = 1469598103934665603ull;
    result.total_bytes = 0;

    gfp = lame_init();
    if (!gfp) {
        fprintf(stderr, "lame_init failed\n");
        exit(3);
    }
    if (lame_set_num_channels(gfp, channels) < 0
        || lame_set_in_samplerate(gfp, sample_rate) < 0
        || lame_set_brate(gfp, 128) < 0
        || lame_set_quality(gfp, 2) < 0
        || lame_set_VBR(gfp, vbr_off) < 0
        || lame_set_bWriteVbrTag(gfp, 0) < 0) {
        fprintf(stderr, "lame pre-init configuration failed\n");
        exit(3);
    }
    lame_set_write_id3tag_automatic(gfp, 0);
    if (lame_init_params(gfp) < 0) {
        fprintf(stderr, "lame init params failed\n");
        exit(3);
    }

    mp3buf = (unsigned char *) malloc(mp3buf_size);
    if (!mp3buf) {
        fprintf(stderr, "oom\n");
        exit(2);
    }

    for (r = 0; r < repeats; ++r) {
        size_t pos = 0;
        while (pos < frames) {
            int encoded;
            int to_encode = (int) ((frames - pos > chunk_frames) ? chunk_frames : (frames - pos));
            encoded = lame_encode_buffer_interleaved(gfp,
                                                     (short int *) (samples + pos * (size_t) channels),
                                                     to_encode,
                                                     mp3buf,
                                                     (int) mp3buf_size);
            if (encoded < 0) {
                fprintf(stderr, "encode failed: %d\n", encoded);
                exit(3);
            }
            result.total_bytes += (uint64_t) encoded;
            result.hash = fnv1a64_update(result.hash, mp3buf, (size_t) encoded);
            pos += (size_t) to_encode;
        }
    }

    {
        int encoded = lame_encode_flush(gfp, mp3buf, (int) mp3buf_size);
        if (encoded < 0) {
            fprintf(stderr, "flush failed: %d\n", encoded);
            exit(3);
        }
        result.total_bytes += (uint64_t) encoded;
        result.hash = fnv1a64_update(result.hash, mp3buf, (size_t) encoded);
    }

    free(mp3buf);
    lame_close(gfp);
    return result;
}

int main(int argc, char **argv)
{
    pcm_data pcm;
    encode_result result;
    int repeats;

    if (argc != 3) {
        fprintf(stderr, "usage: %s INPUT.wav REPEATS\n", argv[0]);
        return 2;
    }

    repeats = atoi(argv[2]);
    if (repeats <= 0) {
        fprintf(stderr, "bad repeat count\n");
        return 2;
    }

    memset(&pcm, 0, sizeof(pcm));
    load_wav(argv[1], &pcm);
    result = run_encode(pcm.samples, pcm.frames, pcm.sample_rate, pcm.channels, repeats);
    printf("frames=%zu repeats=%d bytes=%llu hash=%016llx\n",
           pcm.frames,
           repeats,
           (unsigned long long) result.total_bytes,
           (unsigned long long) result.hash);
    free(pcm.samples);
    return 0;
}
