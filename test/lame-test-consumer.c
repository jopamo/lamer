/*
 * Small test-only library consumer.
 *
 * This is deliberately not a product frontend. It exists only to keep one
 * plain C consumer available when debugging the public encoder API without
 * pulling the production application back into the tree.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lame.h"

#include <limits.h>

#define INPUT_FRAMES 1152
#define OUTPUT_BUFFER_SIZE 9000

typedef struct {
    FILE* file;
    unsigned int channels;
    unsigned int sample_rate;
    unsigned int bits_per_sample;
    uint32_t data_bytes;
} wav_input;

static int read_bytes(FILE* file, unsigned char* buffer, size_t size) {
    return fread(buffer, 1, size, file) == size;
}

static uint16_t read_le16(unsigned char const* bytes) {
    return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
}

static uint32_t read_le32(unsigned char const* bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static int open_wav(char const* path, wav_input* input) {
    unsigned char header[12];
    unsigned char chunk[8];
    unsigned char format[16];
    int have_format = 0;

    memset(input, 0, sizeof(*input));
    input->file = fopen(path, "rb");
    if (!input->file || !read_bytes(input->file, header, sizeof(header)) || memcmp(header, "RIFF", 4) != 0 || memcmp(header + 8, "WAVE", 4) != 0) {
        fprintf(stderr, "not a RIFF/WAVE file: %s\n", path);
        if (input->file)
            fclose(input->file);
        return 0;
    }

    while (read_bytes(input->file, chunk, sizeof(chunk))) {
        uint32_t size = read_le32(chunk + 4);
        long payload = ftell(input->file);
        uint64_t next_chunk;
        if (payload < 0)
            break;

        if (memcmp(chunk, "fmt ", 4) == 0) {
            if (size < sizeof(format) || !read_bytes(input->file, format, sizeof(format)))
                break;
            if (read_le16(format) != 1 || read_le16(format + 2) < 1 || read_le16(format + 2) > 2) {
                break;
            }
            input->channels = read_le16(format + 2);
            input->sample_rate = read_le32(format + 4);
            input->bits_per_sample = read_le16(format + 14);
            if (input->bits_per_sample != 8 && input->bits_per_sample != 16 && input->bits_per_sample != 24 && input->bits_per_sample != 32) {
                break;
            }
            have_format = 1;
        }
        else if (memcmp(chunk, "data", 4) == 0) {
            input->data_bytes = size;
            if (have_format && input->data_bytes > 0)
                return 1;
            break;
        }

        next_chunk = (uint64_t)payload + size + (size & 1u);
        if (next_chunk > (uint64_t)LONG_MAX || fseek(input->file, (long)next_chunk, SEEK_SET) != 0)
            break;
    }

    fclose(input->file);
    input->file = NULL;
    fprintf(stderr, "unsupported or incomplete WAV file: %s\n", path);
    return 0;
}

static int16_t sample_to_short(unsigned char const* sample, unsigned int bits) {
    int32_t value;

    if (bits == 8)
        return (int16_t)(((int)sample[0] - 128) * 256);
    if (bits == 16)
        return (int16_t)read_le16(sample);
    if (bits == 24) {
        value = (int32_t)sample[0] | ((int32_t)sample[1] << 8) | ((int32_t)sample[2] << 16);
        if (value & 0x00800000)
            value |= (int32_t)0xff000000;
        return (int16_t)(value >> 8);
    }

    value = (int32_t)read_le32(sample);
    if (value > INT16_MAX * 65536)
        return INT16_MAX;
    if (value < INT16_MIN * 65536)
        return INT16_MIN;
    return (int16_t)(value >> 16);
}

static int write_encoded(FILE* output, lame_t encoder, short* left, short* right, int frames, unsigned char* buffer) {
    int bytes = lame_encode_buffer(encoder, left, right, frames, buffer, OUTPUT_BUFFER_SIZE);
    if (bytes < 0 || (bytes > 0 && fwrite(buffer, 1, (size_t)bytes, output) != (size_t)bytes))
        return 0;
    return 1;
}

static size_t mp3_frame_size(unsigned char const* header, size_t remaining) {
    static int const mpeg1_bitrates[] = {32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320};
    static int const mpeg2_bitrates[] = {8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160};
    static int const mpeg1_rates[] = {44100, 48000, 32000};
    static int const mpeg2_rates[] = {22050, 24000, 16000};
    unsigned int version;
    unsigned int bitrate_index;
    unsigned int rate_index;
    int bitrate;
    int rate;
    size_t length;

    if (remaining < 4 || header[0] != 0xff || (header[1] & 0xe0) != 0xe0)
        return 0;
    version = (header[1] >> 3) & 3;
    if (version == 1 || ((header[1] >> 1) & 3) != 1)
        return 0;

    bitrate_index = header[2] >> 4;
    rate_index = (header[2] >> 2) & 3;
    if (bitrate_index == 0 || bitrate_index == 15 || rate_index == 3)
        return 0;
    if (version == 3) {
        bitrate = mpeg1_bitrates[bitrate_index - 1];
        rate = mpeg1_rates[rate_index];
    }
    else {
        if (bitrate_index > sizeof(mpeg2_bitrates) / sizeof(mpeg2_bitrates[0]))
            return 0;
        bitrate = mpeg2_bitrates[bitrate_index - 1];
        rate = mpeg2_rates[rate_index];
        if (version == 0)
            rate /= 2;
    }

    length = (size_t)((version == 3 ? 144000 : 72000) * bitrate / rate);
    if (header[2] & 2)
        ++length;
    return length >= 4 && length <= remaining ? length : 0;
}

static int validate_mp3_file(char const* path) {
    FILE* file;
    unsigned char* bytes;
    long file_size;
    size_t size;
    size_t offset = 0;
    size_t frames = 0;

    file = fopen(path, "rb");
    if (!file)
        return 0;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return 0;
    }
    file_size = ftell(file);
    if (file_size < 1 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return 0;
    }
    size = (size_t)file_size;
    bytes = (unsigned char*)malloc(size);
    if (!bytes || fread(bytes, 1, size, file) != size) {
        free(bytes);
        fclose(file);
        return 0;
    }
    fclose(file);

    while (offset < size) {
        size_t length;
        length = mp3_frame_size(bytes + offset, size - offset);
        if (!length) {
            free(bytes);
            return 0;
        }
        offset += length;
        ++frames;
    }

    free(bytes);
    return frames > 0 && offset == size;
}

int main(int argc, char** argv) {
    char const* input_path;
    char const* output_path;
    int vbr = 0;
    wav_input input;
    FILE* output;
    lame_t encoder;
    unsigned char raw[INPUT_FRAMES * 2 * 4];
    unsigned char encoded[OUTPUT_BUFFER_SIZE];
    short left[INPUT_FRAMES];
    short right[INPUT_FRAMES];
    unsigned int bytes_per_sample;
    uint32_t remaining;

    if (argc == 3) {
        input_path = argv[1];
        output_path = argv[2];
    }
    else if (argc == 4 && strcmp(argv[1], "--vbr") == 0) {
        vbr = 1;
        input_path = argv[2];
        output_path = argv[3];
    }
    else {
        fprintf(stderr, "usage: %s [--vbr] INPUT.wav OUTPUT.mp3\n", argv[0]);
        return 2;
    }

    if (!open_wav(input_path, &input))
        return 1;
    output = fopen(output_path, "wb");
    if (!output) {
        perror(output_path);
        fclose(input.file);
        return 1;
    }

    encoder = lame_init();
    bytes_per_sample = input.bits_per_sample / 8;
    remaining = input.data_bytes;
    if (!encoder || lame_set_num_channels(encoder, (int)input.channels) < 0 || lame_set_in_samplerate(encoder, (int)input.sample_rate) < 0 || lame_set_bWriteVbrTag(encoder, 0) < 0 ||
        (vbr ? lame_set_VBR(encoder, vbr_default) < 0 || lame_set_VBR_quality(encoder, 4.0f) < 0 : lame_set_brate(encoder, 128) < 0) || lame_init_params(encoder) < 0) {
        fprintf(stderr, "failed to initialize Lamer\n");
        if (encoder)
            lame_close(encoder);
        fclose(output);
        fclose(input.file);
        return 1;
    }

    while (remaining > 0) {
        size_t frame_bytes = (size_t)input.channels * bytes_per_sample;
        unsigned int frames = remaining / frame_bytes;
        if (frames > INPUT_FRAMES)
            frames = INPUT_FRAMES;
        if (frames == 0 || fread(raw, frame_bytes, frames, input.file) != frames) {
            fprintf(stderr, "short WAV payload\n");
            lame_close(encoder);
            fclose(output);
            fclose(input.file);
            return 1;
        }

        for (unsigned int frame = 0; frame < frames; ++frame) {
            unsigned char const* sample = raw + frame * frame_bytes;
            left[frame] = sample_to_short(sample, input.bits_per_sample);
            right[frame] = input.channels == 2 ? sample_to_short(sample + bytes_per_sample, input.bits_per_sample) : left[frame];
        }
        if (!write_encoded(output, encoder, left, right, (int)frames, encoded)) {
            fprintf(stderr, "Lamer encoding failed\n");
            lame_close(encoder);
            fclose(output);
            fclose(input.file);
            return 1;
        }
        remaining -= (uint32_t)(frames * frame_bytes);
    }

    {
        int bytes = lame_encode_flush(encoder, encoded, OUTPUT_BUFFER_SIZE);
        if (bytes < 0 || (bytes > 0 && fwrite(encoded, 1, (size_t)bytes, output) != (size_t)bytes)) {
            fprintf(stderr, "Lamer flush failed\n");
            lame_close(encoder);
            fclose(output);
            fclose(input.file);
            return 1;
        }
    }

    lame_close(encoder);
    fclose(output);
    fclose(input.file);
    if (!validate_mp3_file(output_path)) {
        fprintf(stderr, "invalid MP3 output: %s\n", output_path);
        return 1;
    }
    if (remove(output_path) != 0) {
        perror(output_path);
        return 1;
    }
    return 0;
}
