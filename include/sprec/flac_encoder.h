/**
 * flac_encoder.h
 * libsprec
 *
 * Created by Árpád Goretity (H2CO3)
 * on Sun 15/04/2012.
 */

#ifndef __SPREC_FLAC_ENCODER_H__
#define __SPREC_FLAC_ENCODER_H__

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

/**
 * Converts a WAV PCM file at the path `wavfile'
 * to a FLAC file with the same sample rate,
 * channel number and bit depth. Writes the result
 * to the file at path `flacfile'.
 * Returns 0 on success, non-0 on error.
 */
int sprec_flac_encode(const char *wavfile, const char *flacfile);



struct sprec_flac_encoder_t;

struct sprec_flac_encoder_t* sprec_flac_create_encoder(uint32_t sample_rate, uint32_t channels, uint32_t bits_per_sample);
int sprec_flac_feed_encoder(struct sprec_flac_encoder_t* encoder, const int32_t buffer[], uint32_t samples);
int sprec_flac_finish_encoder(struct sprec_flac_encoder_t* encoder);
void sprec_flac_destroy_encoder(struct sprec_flac_encoder_t* encoder);


typedef int(*sprec_flac_stream_write_callback_t)(struct sprec_flac_encoder_t* encoder, const uint8_t buffer[], size_t bytes, uint32_t samples, uint32_t current_frame, void* user_data);
typedef int(*sprec_flac_stream_seek_callback_t)(struct sprec_flac_encoder_t* encoder, uint64_t offset, void* user_data);
typedef int(*sprec_flac_stream_tell_callback_t)(struct sprec_flac_encoder_t* encoder, uint64_t* offset, void* user_data);

int sprec_flac_bind_encoder_to_stream(struct sprec_flac_encoder_t* encoder, 
	sprec_flac_stream_write_callback_t write_callback,
	sprec_flac_stream_seek_callback_t seek_callback,
	sprec_flac_stream_tell_callback_t tell_callback,
	void* user_data);



#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* !__SPREC_FLAC_ENCODER_H__ */

