/*

SPDX-License-Identifier: MIT


Round trip test for qoavbr.h, pinned to the QAV1 reference encoder.

The test signal is generated with integer arithmetic only, so the same signal
can be reproduced outside C. The golden hashes below were taken from the
reference encoder and its decoder running on that signal; a mismatch means
qoavbr.h no longer agrees with the format.

Compile with:
	gcc qoavbrtest.c -std=c99 -lm -O3 -o qoavbrtest

Pass --dump-wav <path> to write the test signal out, e.g. to re-derive the
golden hashes from the reference encoder.

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define QOAVBR_IMPLEMENTATION
#define QOAVBR_RECORD_TOTAL_ERROR
#include "qoavbr.h"

#define QOAVBRTEST_SAMPLES 83154

#define QOAVBRTEST_ENCODED_SIZE 18580
#define QOAVBRTEST_ENCODED_HASH 0xbb5d70f4ac5992aaULL
#define QOAVBRTEST_DECODED_HASH 0x313a18b3a6c07007ULL

static int failures = 0;

#define QOAVBRTEST_CHECK(TEST, ...) \
	if (!(TEST)) { \
		printf("FAIL " __VA_ARGS__); \
		printf("\n"); \
		failures++; \
	}


static short qoavbrtest_signal(unsigned int i, unsigned int *seed) {
	*seed = *seed * 1103515245u + 12345u;
	int noise = (int)((*seed >> 17) & 0x3fff) - 8192;

	int period = 48 + (int)((i >> 10) % 400);
	int phase = (int)(i % (unsigned int)period) * 2;
	int triangle = (phase < period ? phase : period * 2 - phase) * 2 - period;

	int loud = (int)((i >> 12) % 6);
	return (short)qoavbr_clamp_s16(
		(triangle * 24000 / period) * (loud + 1) / 8 + (noise >> (5 - loud))
	);
}

static unsigned long long qoavbrtest_hash(const void *data, unsigned int size) {
	const unsigned char *bytes = data;
	unsigned long long hash = 14695981039346656037ULL;
	for (unsigned int i = 0; i < size; i++) {
		hash = (hash ^ bytes[i]) * 1099511628211ULL;
	}
	return hash;
}

static void qoavbrtest_write_wav(const char *path, const short *samples, unsigned int count) {
	unsigned int data_size = count * sizeof(short);
	unsigned char header[44] = {0};
	FILE *f = fopen(path, "wb");
	if (!f) {
		printf("Can't open %s for writing\n", path);
		exit(1);
	}

	memcpy(header, "RIFF", 4);
	qoavbr_write_u32(data_size + 36, header + 4);
	memcpy(header + 8, "WAVEfmt ", 8);
	qoavbr_write_u32(16, header + 16);
	qoavbr_write_u16(1, header + 20);
	qoavbr_write_u16(1, header + 22);
	qoavbr_write_u32(QOAVBR_SAMPLERATE, header + 24);
	qoavbr_write_u32(QOAVBR_SAMPLERATE * 2, header + 28);
	qoavbr_write_u16(2, header + 32);
	qoavbr_write_u16(16, header + 34);
	memcpy(header + 36, "data", 4);
	qoavbr_write_u32(data_size, header + 40);

	fwrite(header, 1, sizeof(header), f);
	for (unsigned int i = 0; i < count; i++) {
		unsigned char sample[2];
		qoavbr_write_u16((unsigned short)samples[i], sample);
		fwrite(sample, 1, 2, f);
	}
	fclose(f);
}

int main(int argc, char **argv) {
	short *samples = malloc(QOAVBRTEST_SAMPLES * sizeof(short));
	unsigned int seed = 1;
	for (unsigned int i = 0; i < QOAVBRTEST_SAMPLES; i++) {
		samples[i] = qoavbrtest_signal(i, &seed);
	}

	if (argc == 3 && strcmp(argv[1], "--dump-wav") == 0) {
		qoavbrtest_write_wav(argv[2], samples, QOAVBRTEST_SAMPLES);
		printf("wrote %s: %d samples\n", argv[2], QOAVBRTEST_SAMPLES);
		free(samples);
		return 0;
	}

	qoavbr_desc desc;
	qoavbr_desc_init(&desc);
	desc.samples = QOAVBRTEST_SAMPLES;

	unsigned int size = 0;
	unsigned char *encoded = qoavbr_encode(samples, &desc, &size);
	QOAVBRTEST_CHECK(encoded, "encode returned NULL");
	if (!encoded) {
		return 1;
	}

	unsigned int chunks = (QOAVBRTEST_SAMPLES + QOAVBR_DEFAULT_CHUNK_SAMPLES - 1) / QOAVBR_DEFAULT_CHUNK_SAMPLES;
	QOAVBRTEST_CHECK(desc.chunks == chunks, "chunks %d, want %d", desc.chunks, chunks);
	QOAVBRTEST_CHECK(size <= qoavbr_max_encoded_size(&desc), "size %d over the bound", size);
	QOAVBRTEST_CHECK(size == QOAVBRTEST_ENCODED_SIZE, "size %d, want %d", size, QOAVBRTEST_ENCODED_SIZE);

	unsigned long long encoded_hash = qoavbrtest_hash(encoded, size);
	QOAVBRTEST_CHECK(
		encoded_hash == QOAVBRTEST_ENCODED_HASH,
		"encoded hash 0x%016llx, want 0x%016llx", encoded_hash, QOAVBRTEST_ENCODED_HASH
	);

	int mode_counts[4] = {0};
	for (unsigned int c = 0; c < desc.chunks; c++) {
		unsigned int start = qoavbr_read_u32(encoded + QOAVBR_HEADER_SIZE + c * 4);
		unsigned int end = qoavbr_read_u32(encoded + QOAVBR_HEADER_SIZE + (c + 1) * 4);
		unsigned int count = qoavbr_read_u16(encoded + start + 2);
		int bits = encoded[start];

		QOAVBRTEST_CHECK((start & 3) == 0, "chunk %d offset %d not aligned", c, start);
		QOAVBRTEST_CHECK(bits >= 1 && bits <= 3, "chunk %d bits %d", c, bits);
		QOAVBRTEST_CHECK(end - start == QOAVBR_CHUNK_SIZE(count, bits), "chunk %d size %d", c, end - start);
		mode_counts[bits]++;
	}
	QOAVBRTEST_CHECK(mode_counts[1] == 15, "1-bit chunks %d, want 15", mode_counts[1]);
	QOAVBRTEST_CHECK(mode_counts[2] == 21, "2-bit chunks %d, want 21", mode_counts[2]);
	QOAVBRTEST_CHECK(mode_counts[3] == 5, "3-bit chunks %d, want 5", mode_counts[3]);

	qoavbr_desc decoded_desc;
	short *decoded = qoavbr_decode(encoded, size, &decoded_desc);
	QOAVBRTEST_CHECK(decoded, "decode returned NULL");
	if (!decoded) {
		return 1;
	}

	QOAVBRTEST_CHECK(decoded_desc.samples == desc.samples, "decoded %d samples", decoded_desc.samples);
	QOAVBRTEST_CHECK(decoded_desc.chunk_samples == desc.chunk_samples, "decoded chunk_samples %d", decoded_desc.chunk_samples);

	unsigned long long decoded_hash = qoavbrtest_hash(decoded, decoded_desc.samples * sizeof(short));
	QOAVBRTEST_CHECK(
		decoded_hash == QOAVBRTEST_DECODED_HASH,
		"decoded hash 0x%016llx, want 0x%016llx", decoded_hash, QOAVBRTEST_DECODED_HASH
	);

	double signal_energy = 0;
	double error_energy = 0;
	for (unsigned int i = 0; i < decoded_desc.samples; i++) {
		double difference = samples[i] - decoded[i];
		signal_energy += (double)samples[i] * samples[i];
		error_energy += difference * difference;
	}
	double snr = 10.0 * log10(signal_energy / error_energy);
	QOAVBRTEST_CHECK(snr > 12.0, "snr %.2f db too low", snr);

	/* The encoder's own error is measured over padded chunks, so it can only
	exceed the decoder's by the last chunk's padding. */
	QOAVBRTEST_CHECK(desc.error >= error_energy, "encoder error below the decoder's");

	unsigned char truncated[QOAVBR_MIN_FILESIZE + 8];
	memcpy(truncated, encoded, sizeof(truncated));
	QOAVBRTEST_CHECK(
		qoavbr_decode(truncated, sizeof(truncated), &decoded_desc) == NULL,
		"decoded a truncated file"
	);

	double bitrate = size * 8.0 / (desc.samples / (double)desc.samplerate) / 1000.0;
	printf(
		"%d samples, %d bytes, %.2f kbit/s, snr %.2f db, chunks 1-bit=%d 2-bit=%d 3-bit=%d\n",
		desc.samples, size, bitrate, snr, mode_counts[1], mode_counts[2], mode_counts[3]
	);
	printf(failures ? "%d checks failed\n" : "all checks passed\n", failures);

	free(decoded);
	free(encoded);
	free(samples);
	return failures ? 1 : 0;
}
