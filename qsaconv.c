/*

Copyright (c) 2023, Dominic Szablewski - https://phoboslab.org
SPDX-License-Identifier: MIT


Command line tool to convert FLAC, MP3, WAV to QSA and QSA to WAV

Define QSACONV_HAS_DRMP3 and QSACONV_HAS_DRFLAC for MP3 and FLAC support
 -"dr_mp3.h" (https://github.com/mackron/dr_libs/blob/master/dr_mp3.h)
 -"dr_flac.h" (https://github.com/mackron/dr_libs/blob/master/dr_flac.h)

Compile with:
	gcc qsaconv.c -std=gnu99 -lm -O3 -o qsaconv

*/

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <time.h>

#ifdef QSACONV_HAS_DRMP3
	/* https://github.com/mackron/dr_libs/blob/master/dr_mp3.h */
	#define DR_MP3_IMPLEMENTATION
	#include "dr_mp3.h"
#endif

#ifdef QSACONV_HAS_DRFLAC
	/* https://github.com/mackron/dr_libs/blob/master/dr_flac.h */
	#define DR_FLAC_IMPLEMENTATION
	#include "dr_flac.h"
#endif

#define QSA_IMPLEMENTATION
#define QSA_RECORD_TOTAL_ERROR
#include "qsa.h"

#define QSACONV_STRINGIFY(x) #x
#define QSACONV_TOSTRING(x) QSACONV_STRINGIFY(x)
#define QSACONV_ABORT(...) \
	printf("Abort at line " QSACONV_TOSTRING(__LINE__) ": " __VA_ARGS__); \
	printf("\n"); \
	exit(1)
#define QSACONV_ASSERT(TEST, ...) \
	if (!(TEST)) { \
		QSACONV_ABORT(__VA_ARGS__); \
	}

#define QSACONV_STR_ENDS_WITH(S, E) (strcmp(S + strlen(S) - (sizeof(E)-1), E) == 0)

typedef struct {
	unsigned int samplerate;
	unsigned int samples;
	unsigned int channels;
} qsaconv_pcm_desc;



/* -----------------------------------------------------------------------------
	WAV reader / writer */

#define QSACONV_CHUNK_ID(S) \
	(((unsigned int)(S[3])) << 24 | ((unsigned int)(S[2])) << 16 | \
	 ((unsigned int)(S[1])) <<  8 | ((unsigned int)(S[0])))

void qsaconv_fwrite_u32_le(unsigned int v, FILE *fh) {
	unsigned char buf[sizeof(unsigned int)];
	buf[0] = 0xff & (v      );
	buf[1] = 0xff & (v >>  8);
	buf[2] = 0xff & (v >> 16);
	buf[3] = 0xff & (v >> 24);
	int wrote = fwrite(buf, sizeof(unsigned int), 1, fh);
	QSACONV_ASSERT(wrote, "Write error");
}

void qsaconv_fwrite_u16_le(unsigned short v, FILE *fh) {
	unsigned char buf[sizeof(unsigned short)];
	buf[0] = 0xff & (v      );
	buf[1] = 0xff & (v >>  8);
	int wrote = fwrite(buf, sizeof(unsigned short), 1, fh);
	QSACONV_ASSERT(wrote, "Write error");
}

unsigned int qsaconv_fread_u32_le(FILE *fh) {
	unsigned char buf[sizeof(unsigned int)];
	int read = fread(buf, sizeof(unsigned int), 1, fh);
	QSACONV_ASSERT(read, "Read error or unexpected end of file");
	return (buf[3] << 24) | (buf[2] << 16) | (buf[1] << 8) | buf[0];
}

unsigned short qsaconv_fread_u16_le(FILE *fh) {
	unsigned char buf[sizeof(unsigned short)];
	int read = fread(buf, sizeof(unsigned short), 1, fh);
	QSACONV_ASSERT(read, "Read error or unexpected end of file");
	return (buf[1] << 8) | buf[0];
}

int qsaconv_wav_write(const char *path, short *sample_data, qsaconv_pcm_desc *desc) {
	unsigned int data_size = desc->samples * desc->channels * sizeof(short);
	unsigned int samplerate = desc->samplerate;
	short bits_per_sample = 16;
	short channels = desc->channels;

	/* Lifted from https://www.jonolick.com/code.html - public domain
	Made endian agnostic using qsaconv_fwrite() */
	FILE *fh = fopen(path, "wb");
	QSACONV_ASSERT(fh, "Can't open %s for writing", path);
	fwrite("RIFF", 1, 4, fh);
	qsaconv_fwrite_u32_le(data_size + 44 - 8, fh);
	fwrite("WAVEfmt \x10\x00\x00\x00\x01\x00", 1, 14, fh);
	qsaconv_fwrite_u16_le(channels, fh);
	qsaconv_fwrite_u32_le(samplerate, fh);
	qsaconv_fwrite_u32_le(channels * samplerate * bits_per_sample/8, fh);
	qsaconv_fwrite_u16_le(channels * bits_per_sample/8, fh);
	qsaconv_fwrite_u16_le(bits_per_sample, fh);
	fwrite("data", 1, 4, fh);
	qsaconv_fwrite_u32_le(data_size, fh);
	fwrite((void*)sample_data, data_size, 1, fh);
	fclose(fh);
	return data_size  + 44 - 8;
}

short *qsaconv_wav_read(const char *path, qsaconv_pcm_desc *desc) {
	FILE *fh = fopen(path, "rb");
	QSACONV_ASSERT(fh, "Can't open %s for reading", path);

	unsigned int container_type = qsaconv_fread_u32_le(fh);
	QSACONV_ASSERT(container_type == QSACONV_CHUNK_ID("RIFF"), "Not a RIFF container");

	unsigned int wav_size = qsaconv_fread_u32_le(fh);
	unsigned int wavid = qsaconv_fread_u32_le(fh);
	QSACONV_ASSERT(wavid == QSACONV_CHUNK_ID("WAVE"), "No WAVE id found");

	unsigned int data_size = 0;
	unsigned int format_length = 0;
	unsigned int format_type = 0;
	unsigned int channels = 0;
	unsigned int samplerate = 0;
	unsigned int byte_rate = 0;
	unsigned int block_align = 0;
	unsigned int bits_per_sample = 0;

	/* Find the fmt and data chunk, skip all others */
	while (1) {
		unsigned int chunk_type = qsaconv_fread_u32_le(fh);
		unsigned int chunk_size = qsaconv_fread_u32_le(fh);

		if (chunk_type == QSACONV_CHUNK_ID("fmt ")) {
			QSACONV_ASSERT(chunk_size == 16 || chunk_size == 18, "WAV fmt chunk size missmatch");

			format_type = qsaconv_fread_u16_le(fh);
			channels = qsaconv_fread_u16_le(fh);
			samplerate = qsaconv_fread_u32_le(fh);
			byte_rate = qsaconv_fread_u32_le(fh);
			block_align = qsaconv_fread_u16_le(fh);
			bits_per_sample = qsaconv_fread_u16_le(fh);

			if (chunk_size == 18) {
				unsigned short extra_params = qsaconv_fread_u16_le(fh);
				QSACONV_ASSERT(extra_params == 0, "WAV fmt extra params not supported");
			}
		}
		else if (chunk_type == QSACONV_CHUNK_ID("data")) {
			data_size = chunk_size;
			break;
		}
		else {
			int seek_result = fseek(fh, chunk_size, SEEK_CUR);
			QSACONV_ASSERT(seek_result == 0, "Malformed RIFF header");
		}
	}

	QSACONV_ASSERT(format_type == 1, "Type in fmt chunk is not PCM");
	QSACONV_ASSERT(bits_per_sample == 16, "Bits per samples != 16");
	QSACONV_ASSERT(data_size, "No data chunk");

	unsigned char *wav_bytes = malloc(data_size);
	QSACONV_ASSERT(wav_bytes, "Malloc for %d bytes failed", data_size);
	int read = fread(wav_bytes, data_size, 1, fh);
	QSACONV_ASSERT(read, "Read error or unexpected end of file for %d bytes", data_size);
	fclose(fh);

	desc->samplerate = samplerate;
	desc->samples = data_size / (channels * (bits_per_sample/8));
	desc->channels = channels;

	return (short*)wav_bytes;
}



/* -----------------------------------------------------------------------------
	MP3 decode wrapper */

#ifdef QSACONV_HAS_DRMP3
	short *qsaconv_mp3_read(const char *path, qsaconv_pcm_desc *desc) {
		drmp3_uint64 samples;

		drmp3_config mp3;
		short* sample_data = drmp3_open_file_and_read_pcm_frames_s16(path, &mp3, &samples, NULL);
		QSACONV_ASSERT(sample_data, "Can't decode MP3");

		desc->samplerate = mp3.sampleRate;
		desc->channels = mp3.channels;
		desc->samples = samples;

		return sample_data;
	}
#endif



/* -----------------------------------------------------------------------------
	FLAC decode wrapper */

#ifdef QSACONV_HAS_DRFLAC
	short *qsaconv_flac_read(const char *path, qsaconv_pcm_desc *desc) {
		unsigned int channels;
		unsigned int samplerate;
		drflac_uint64 samples;
		short* sample_data = drflac_open_file_and_read_pcm_frames_s16(path, &channels, &samplerate, &samples, NULL);
		QSACONV_ASSERT(sample_data, "Can't decode FLAC");

		desc->samplerate = samplerate;
		desc->channels = channels;
		desc->samples = samples;

		return sample_data;
	}
#endif



/* -----------------------------------------------------------------------------
	Main */

int main(int argc, char **argv) {
	QSACONV_ASSERT(argc >= 3,
		"\nUsage: qsaconv in.{wav,mp3,flac,qsa} out.{wav,qsa}"
		"\n       [--chunk-samples N] [--slice-samples N] [--shape N]"
	)

	qsaconv_pcm_desc desc;
	short *sample_data = NULL;

	qsa_desc qsa;
	qsa_desc_init(&qsa);

	for (int i = 3; i < argc; i += 2) {
		QSACONV_ASSERT(i + 1 < argc, "Missing value for %s", argv[i]);
		if (strcmp(argv[i], "--chunk-samples") == 0) {
			qsa.chunk_samples = atoi(argv[i + 1]);
		}
		else if (strcmp(argv[i], "--slice-samples") == 0) {
			qsa.slice_samples = atoi(argv[i + 1]);
		}
		else if (strcmp(argv[i], "--shape") == 0) {
			qsa.shape_lambda = atof(argv[i + 1]);
		}
		else {
			QSACONV_ABORT("Unknown option %s", argv[i]);
		}
	}


	/* Decode input */

	clock_t start_decode = clock();

	if (QSACONV_STR_ENDS_WITH(argv[1], ".wav")) {
		sample_data = qsaconv_wav_read(argv[1], &desc);
	}
	else if (QSACONV_STR_ENDS_WITH(argv[1], ".mp3")) {
		#ifdef QSACONV_HAS_DRMP3
			sample_data = qsaconv_mp3_read(argv[1], &desc);
		#else
			QSACONV_ABORT("qsaconv was not compiled with an MP3 decoder (QSACONV_HAS_DRMP3)");
		#endif
	}
	else if (QSACONV_STR_ENDS_WITH(argv[1], ".flac")) {
		#ifdef QSACONV_HAS_DRFLAC
			sample_data = qsaconv_flac_read(argv[1], &desc);
		#else
			QSACONV_ABORT("qsaconv was not compiled with a FLAC decoder (QSACONV_HAS_DRFLAC)");
		#endif
	}
	else if (QSACONV_STR_ENDS_WITH(argv[1], ".qsa")) {
		qsa_desc source;
		qsa_desc_init(&source);
		sample_data = qsa_read(argv[1], &source);
		desc.channels = 1;
		desc.samplerate = source.samplerate;
		desc.samples = source.samples;
	}
	else {
		QSACONV_ABORT("Unknown file type for %s", argv[1]);
	}

	clock_t end_decode = clock();

	QSACONV_ASSERT(sample_data, "Can't load/decode %s", argv[1]);

	printf(
		"%s: channels: %d, samplerate: %d hz, samples per channel: %d, duration: %d sec (took %.2f seconds)\n",
		argv[1], desc.channels, desc.samplerate, desc.samples, desc.samples/desc.samplerate,
		(double)(end_decode - start_decode) / CLOCKS_PER_SEC
	);


	/* Encode output */

	clock_t start_encode = clock();

	int bytes_written = 0;
	double psnr = INFINITY;
	if (QSACONV_STR_ENDS_WITH(argv[2], ".wav")) {
		bytes_written = qsaconv_wav_write(argv[2], sample_data, &desc);
	}
	else if (QSACONV_STR_ENDS_WITH(argv[2], ".qsa")) {
		QSACONV_ASSERT(desc.channels == 1, "QSA is mono only");
		qsa.samplerate = desc.samplerate;
		qsa.samples = desc.samples;
		bytes_written = qsa_write(argv[2], sample_data, &qsa);
		#ifdef QSA_RECORD_TOTAL_ERROR
			if (qsa.error) {
				psnr = -20.0 * log10(sqrt(qsa.error/qsa.samples) / 32768.0);
			}
		#endif
	}
	else {
		QSACONV_ABORT("Unknown file type for %s", argv[2]);
	}

	clock_t end_encode = clock();

	QSACONV_ASSERT(bytes_written, "Can't write/encode %s", argv[2]);
	free(sample_data);

	printf(
		"%s: size: %d kb (%d bytes) = %.2f kbit/s, psnr: %.2f db (took %.2f seconds)\n",
		argv[2], bytes_written/1024, bytes_written,
		(((float)bytes_written*8)/((float)desc.samples/(float)desc.samplerate))/1024, psnr,
		(double)(end_encode - start_encode) / CLOCKS_PER_SEC
	);

	return 0;
}
