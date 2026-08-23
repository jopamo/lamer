#include "lame.h"

typedef void (*abi_function)(void);

int main(void) {
    static abi_function const symbols[] = {
        (abi_function)lame_close,
        (abi_function)lame_encode_buffer,
        (abi_function)lame_encode_buffer_float,
        (abi_function)lame_encode_buffer_int,
        (abi_function)lame_encode_flush,
        (abi_function)lame_get_encoder_delay,
        (abi_function)lame_get_framesize,
        (abi_function)lame_init,
        (abi_function)lame_init_params,
        (abi_function)lame_set_VBR,
        (abi_function)lame_set_VBR_mean_bitrate_kbps,
        (abi_function)lame_set_VBR_quality,
        (abi_function)lame_set_bWriteVbrTag,
        (abi_function)lame_set_brate,
        (abi_function)lame_set_copyright,
        (abi_function)lame_set_disable_reservoir,
        (abi_function)lame_set_in_samplerate,
        (abi_function)lame_set_lowpassfreq,
        (abi_function)lame_set_mode,
        (abi_function)lame_set_num_channels,
        (abi_function)lame_set_original,
        (abi_function)lame_set_out_samplerate,
        (abi_function)lame_set_quality,
    };

    for (unsigned int i = 0; i < sizeof(symbols) / sizeof(symbols[0]); ++i) {
        if (!symbols[i])
            return 1;
    }
    return 0;
}
