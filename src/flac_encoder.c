/**
 * flac_encoder.c
 * libsprec
 *
 * Created by Árpád Goretity (H2CO3)
 * on Sun 15/04/2012.
 */

#include <assert.h>

#include <stdlib.h>
#include <unistd.h>
#include <FLAC/all.h>
#include <sprec/flac_encoder.h>
#include <sprec/wav.h>

#include "CompileTimeAssert.h"



/* Check types */

CASSERT(sizeof(FLAC__byte) == sizeof(uint8_t), falc_encoder_c);
CASSERT(sizeof(int32_t) == sizeof(FLAC__int32), flac_encoder_c);
CASSERT(sizeof(FLAC__uint64) == sizeof(uint64_t), flac_encoder_c);

/****************************************************************************/


#define BUFFSIZE (1 << 16)

/**
 * Search a NUL-terminated C string in a byte array
 * Returns: location of the string, or
 * NULL if not found
 */
char *memstr(char *haystack, char *needle, int size);

/**
 * BUFFSIZE samples * 2 bytes per sample * 2 channels
 */
static FLAC__byte buffer[BUFFSIZE * 2 * 2];
/**
 * BUFFSIZE samples * 2 channels
 */
static FLAC__int32 pcm[BUFFSIZE * 2];



/****************************************************************************/

typedef struct sprec_flac_encoder_t
{
	FLAC__StreamEncoder* encoder;

	sprec_flac_stream_write_callback_t write_callback;
	sprec_flac_stream_seek_callback_t seek_callback;
	sprec_flac_stream_tell_callback_t tell_callback;
	void* callbacks_user_data;
} sprec_flac_encoder_t;


sprec_flac_encoder_t* sprec_flac_create_encoder(uint32_t sample_rate, 
	uint32_t channels, 
	uint32_t bits_per_sample)
{
	sprec_flac_encoder_t *encoder = (sprec_flac_encoder_t*)calloc(1, sizeof(sprec_flac_encoder_t));

	if (!encoder)
		return NULL;

	encoder->encoder = FLAC__stream_encoder_new();

	if (!encoder->encoder)
	{
		free(encoder);
		return NULL;
	}


	FLAC__stream_encoder_set_channels(encoder->encoder, channels);
	FLAC__stream_encoder_set_bits_per_sample(encoder->encoder, bits_per_sample);
	FLAC__stream_encoder_set_sample_rate(encoder->encoder, sample_rate);

	FLAC__stream_encoder_set_compression_level(encoder->encoder, 5);

	return encoder;
}

int sprec_flac_feed_encoder(sprec_flac_encoder_t* encoder, 
	const int32_t buffer[], uint32_t samples)
{
	return FLAC__stream_encoder_process_interleaved(encoder->encoder, buffer, samples);
}

int sprec_flac_finish_encoder(sprec_flac_encoder_t* encoder)
{
	if (!encoder)
		return 1;
	return FLAC__stream_encoder_finish(encoder->encoder);
}

void sprec_flac_destroy_encoder(sprec_flac_encoder_t* encoder)
{
	if (encoder)
	{
		if (encoder->encoder)
			FLAC__stream_encoder_delete(encoder->encoder);

		free(encoder);
	}
}


FLAC__StreamEncoderWriteStatus internal_stream_write_callback(
	const FLAC__StreamEncoder *encoder, 
	const FLAC__byte buffer[], 
	size_t bytes, 
	unsigned samples, 
	unsigned current_frame, 
	void *client_data) 
{
	sprec_flac_encoder_t* en = (sprec_flac_encoder_t*)client_data;

	assert(en);
	assert(en->write_callback);

	int res = (en->write_callback)(en, buffer, bytes, samples, current_frame, 
		en->callbacks_user_data);

	if (res)
		return FLAC__STREAM_ENCODER_WRITE_STATUS_FATAL_ERROR;
	else
		return FLAC__STREAM_ENCODER_WRITE_STATUS_OK;
}

FLAC__StreamEncoderSeekStatus internal_stream_seek_callback(
	const FLAC__StreamEncoder *encoder, 
	FLAC__uint64 absolute_byte_offset, 
	void *client_data)
{
	sprec_flac_encoder_t* en = (sprec_flac_encoder_t*)client_data;

	int res = (*en->seek_callback)(en, absolute_byte_offset, 
		en->callbacks_user_data);

	if (res)
		return FLAC__STREAM_ENCODER_SEEK_STATUS_ERROR;
	else
		return FLAC__STREAM_ENCODER_SEEK_STATUS_OK;
}


FLAC__StreamEncoderTellStatus internal_stream_tell_callback(
	const FLAC__StreamEncoder *encoder, 
	FLAC__uint64 *absolute_byte_offset, 
	void *client_data)
{
	sprec_flac_encoder_t* en = (sprec_flac_encoder_t*)client_data;

	int res = (*en->tell_callback)(en, absolute_byte_offset, 
		en->callbacks_user_data);

	if (res)
		return FLAC__STREAM_ENCODER_TELL_STATUS_ERROR;
	else
		return FLAC__STREAM_ENCODER_TELL_STATUS_OK;
}

int sprec_flac_bind_encoder_to_stream(sprec_flac_encoder_t* encoder,
	sprec_flac_stream_write_callback_t write_callback,
	sprec_flac_stream_seek_callback_t seek_callback,
	sprec_flac_stream_tell_callback_t tell_callback,
	void* user_data)
{
	if (!encoder || !write_callback)
		return 1;

	if (seek_callback && !tell_callback)
		return 2;

	encoder->write_callback = write_callback;
	encoder->seek_callback = seek_callback;
	encoder->tell_callback = tell_callback;
	encoder->callbacks_user_data = user_data;

	FLAC__StreamEncoderInitStatus status = FLAC__stream_encoder_init_stream(
		encoder->encoder,
		internal_stream_write_callback,
		seek_callback ? internal_stream_seek_callback : NULL,
		tell_callback ? internal_stream_tell_callback : NULL,
		NULL,
		encoder);

	if (FLAC__STREAM_ENCODER_INIT_STATUS_OK != status)
		return 3;

	return 0;
}


/****************************************************************************/

int sprec_flac_encode(const char *wavfile, const char *flacfile)
{
	sprec_flac_encoder_t* ienc;
	FILE *infile;
	char *data_location;
	uint32_t sample_rate;
	uint32_t total_samples;
	uint32_t channels;
	uint32_t bits_per_sample;
	uint32_t data_offset;
	int err;

	if (!wavfile || !flacfile)
	{
		return -1;
	}
	
	/**
	 * Remove the original file, if present, in order
	 * not to leave chunks of old data inside
	 */
	remove(flacfile);

	/**
	 * Read the first 64kB of the file. This somewhat guarantees
	 * that we will find the beginning of the data section, even
	 * if the WAV header is non-standard and contains
	 * other garbage before the data (NB Apple's 4kB FLLR section!)
	 */
	infile = fopen(wavfile, "rb");
	if (!infile)
	{
		return -1;
	}
	fread(buffer, BUFFSIZE, 1, infile);

	/**
	 * Search the offset of the data section
	 */
	data_location = memstr((char *)buffer, "data", BUFFSIZE);
	if (!data_location)
	{
		fclose(infile);
		return -1;
	}
	data_offset = data_location - (char *)buffer;

	/**
	 * For an explanation on why the 4 + 4 byte extra offset is there,
	 * see the comment for calculating the number of total_samples.
	 */
	fseek(infile, data_offset + 4 + 4, SEEK_SET);
	
	struct sprec_wav_header *hdr = sprec_wav_header_from_data((char *)buffer);
	if (!hdr)
	{
		fclose(infile);
		return -1;
	}

	/**
	 * Sample rate must be between 16000 and 44000
	 * for the Google Speech APIs.
	 * There should be two channels.
	 * Sample depth is 16 bit signed, little endian.
	 */
	sample_rate = hdr->sample_rate;
	channels = hdr->number_of_channels;
	bits_per_sample = hdr->bits_per_sample;
	
	/**
	 * hdr->file_size contains actual file size - 8 bytes.
	 * the eight bytes at position `data_offset' are:
	 * 'data' then a 32-bit unsigned int, representing
	 * the length of the data section.
	 */
	total_samples = ((hdr->file_size + 8) - (data_offset + 4 + 4)) / (channels * bits_per_sample / 8);

	/**
	 * Create and initialize the FLAC encoder
	 */

	ienc = sprec_flac_create_encoder(sample_rate, channels, bits_per_sample);
	
	if (!ienc)
	{
		fclose(infile);
		free(hdr);
		return -1;
	}

	FLAC__stream_encoder_set_total_samples_estimate(ienc->encoder, total_samples);

	err = FLAC__stream_encoder_init_file(ienc->encoder, flacfile, NULL, NULL);
	if (err)
	{
		fclose(infile);
		free(hdr);
		FLAC__stream_encoder_delete(ienc->encoder);
		return -1;
	}

	/**
	 * Feed the PCM data to the encoder in 64kB chunks
	 */
	size_t left = total_samples;
	while (left > 0)
	{
		size_t need = left > BUFFSIZE ? BUFFSIZE : left;
		fread(buffer, channels * bits_per_sample / 8, need, infile);

		size_t i;
		for (i = 0; i < need * channels; i++)
		{
			if (bits_per_sample == 16)
			{
				/**
				 * 16 bps, signed little endian
				 */
				pcm[i] = *(int16_t *)(buffer + i * 2);
			}
			else
			{
				/**
				 * 8 bps, unsigned
				 */
				pcm[i] = *(uint8_t *)(buffer + i);
			}
		}
		
		FLAC__bool succ = sprec_flac_feed_encoder(ienc, pcm, need);
		if (!succ)
		{
			fclose(infile);
			free(hdr);
			sprec_flac_destroy_encoder(ienc);
			return -1;
		}

		left -= need;
	}

	/**
	 * Write out/finalize the file
	 */
	sprec_flac_finish_encoder(ienc);

	/**
	 * Clean up
	 */
	sprec_flac_destroy_encoder(ienc);
	fclose(infile);
	free(hdr);
	
	return 0;
}

char *memstr(char *haystack, char *needle, int size)
{
	char *p;
	char needlesize = strlen(needle);

	for (p = haystack; p <= haystack - needlesize + size; p++)
	{
		if (memcmp(p, needle, needlesize) == 0)
		{
			/**
			 * Found it
			 */
			return p;
		}
	}
	
	/**
	 * Not found
	 */
	return NULL;
}

