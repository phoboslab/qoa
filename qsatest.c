/*

SPDX-License-Identifier: MIT


Round trip test for qsa.h, pinned to the QSA1 reference encoder
(GBVideo's tools/qav2_encode.c, whose bitstream is identical apart
from the magic bytes).

The test signal is generated with integer arithmetic only, so the same signal
can be reproduced outside C. The golden hashes below were taken from the
reference encoder and its decoder running on that signal; a mismatch means
qsa.h no longer agrees with the format.

Compile with:
	gcc qsatest.c -std=c99 -lm -O3 -o qsatest

Pass --dump-wav <path> to write the test signal out, e.g. to re-derive the
golden hashes from the reference encoder.

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define QSA_IMPLEMENTATION
#define QSA_RECORD_TOTAL_ERROR
#include "qsa.h"

#define QSATEST_SAMPLES 83154

#define QSATEST_ENCODED_SIZE 21804
#define QSATEST_ENCODED_HASH 0x2de10d8e3920c38bULL
#define QSATEST_DECODED_HASH 0x32d191214f99a7b8ULL

static int failures = 0;

#define QSATEST_CHECK(TEST, ...) \
	if (!(TEST)) { \
		printf("FAIL " __VA_ARGS__); \
		printf("\n"); \
		failures++; \
	}


static short qsatest_signal(unsigned int i, unsigned int *seed) {
	*seed = *seed * 1103515245u + 12345u;
	int noise = (int)((*seed >> 17) & 0x3fff) - 8192;

	int period = 48 + (int)((i >> 10) % 400);
	int phase = (int)(i % (unsigned int)period) * 2;
	int triangle = (phase < period ? phase : period * 2 - phase) * 2 - period;

	int loud = (int)((i >> 12) % 6);
	return (short)qsa_clamp_s16(
		(triangle * 24000 / period) * (loud + 1) / 8 + (noise >> (5 - loud))
	);
}

static unsigned long long qsatest_hash(const void *data, unsigned int size) {
	const unsigned char *bytes = data;
	unsigned long long hash = 14695981039346656037ULL;
	for (unsigned int i = 0; i < size; i++) {
		hash = (hash ^ bytes[i]) * 1099511628211ULL;
	}
	return hash;
}

static void qsatest_write_wav(const char *path, const short *samples, unsigned int count) {
	unsigned int data_size = count * sizeof(short);
	unsigned char header[44] = {0};
	FILE *f = fopen(path, "wb");
	if (!f) {
		printf("Can't open %s for writing\n", path);
		exit(1);
	}

	memcpy(header, "RIFF", 4);
	qsa_write_u32(data_size + 36, header + 4);
	memcpy(header + 8, "WAVEfmt ", 8);
	qsa_write_u32(16, header + 16);
	qsa_write_u16(1, header + 20);
	qsa_write_u16(1, header + 22);
	qsa_write_u32(QSA_DEFAULT_SAMPLERATE, header + 24);
	qsa_write_u32(QSA_DEFAULT_SAMPLERATE * 2, header + 28);
	qsa_write_u16(2, header + 32);
	qsa_write_u16(16, header + 34);
	memcpy(header + 36, "data", 4);
	qsa_write_u32(data_size, header + 40);

	fwrite(header, 1, sizeof(header), f);
	for (unsigned int i = 0; i < count; i++) {
		unsigned char sample[2];
		qsa_write_u16((unsigned short)samples[i], sample);
		fwrite(sample, 1, 2, f);
	}
	fclose(f);
}

int main(int argc, char **argv) {
	short *samples = malloc(QSATEST_SAMPLES * sizeof(short));
	unsigned int seed = 1;
	for (unsigned int i = 0; i < QSATEST_SAMPLES; i++) {
		samples[i] = qsatest_signal(i, &seed);
	}

	if (argc == 3 && strcmp(argv[1], "--dump-wav") == 0) {
		qsatest_write_wav(argv[2], samples, QSATEST_SAMPLES);
		printf("wrote %s: %d samples\n", argv[2], QSATEST_SAMPLES);
		free(samples);
		return 0;
	}

	qsa_desc desc;
	qsa_desc_init(&desc);
	desc.samples = QSATEST_SAMPLES;

	unsigned int size = 0;
	unsigned char *encoded = qsa_encode(samples, &desc, &size);
	QSATEST_CHECK(encoded, "encode returned NULL");
	if (!encoded) {
		return 1;
	}

	unsigned int chunks = (QSATEST_SAMPLES + QSA_DEFAULT_CHUNK_SAMPLES - 1) / QSA_DEFAULT_CHUNK_SAMPLES;
	QSATEST_CHECK(desc.chunks == chunks, "chunks %d, want %d", desc.chunks, chunks);
	QSATEST_CHECK(size <= qsa_max_encoded_size(&desc), "size %d over the bound", size);
	QSATEST_CHECK(size == QSATEST_ENCODED_SIZE, "size %d, want %d", size, QSATEST_ENCODED_SIZE);

	unsigned long long encoded_hash = qsatest_hash(encoded, size);
	QSATEST_CHECK(
		encoded_hash == QSATEST_ENCODED_HASH,
		"encoded hash 0x%016llx, want 0x%016llx", encoded_hash, QSATEST_ENCODED_HASH
	);

	for (unsigned int c = 0; c < desc.chunks; c++) {
		unsigned int start = qsa_read_u32(encoded + QSA_HEADER_SIZE + c * 4);
		unsigned int end = qsa_read_u32(encoded + QSA_HEADER_SIZE + (c + 1) * 4);
		unsigned int count = qsa_read_u16(encoded + start + 2);

		QSATEST_CHECK((start & 3) == 0, "chunk %d offset %d not aligned", c, start);
		QSATEST_CHECK(encoded[start] == 2, "chunk %d bits %d", c, encoded[start]);
		QSATEST_CHECK(encoded[start + 1] == 0, "chunk %d reserved byte %d", c, encoded[start + 1]);
		QSATEST_CHECK(
			end - start == QSA_CHUNK_SIZE(count, desc.slice_samples),
			"chunk %d size %d", c, end - start
		);
	}

	qsa_desc decoded_desc;
	short *decoded = qsa_decode(encoded, size, &decoded_desc);
	QSATEST_CHECK(decoded, "decode returned NULL");
	if (!decoded) {
		return 1;
	}

	QSATEST_CHECK(decoded_desc.samples == desc.samples, "decoded %d samples", decoded_desc.samples);
	QSATEST_CHECK(decoded_desc.chunk_samples == desc.chunk_samples, "decoded chunk_samples %d", decoded_desc.chunk_samples);
	QSATEST_CHECK(decoded_desc.slice_samples == desc.slice_samples, "decoded slice_samples %d", decoded_desc.slice_samples);

	unsigned long long decoded_hash = qsatest_hash(decoded, decoded_desc.samples * sizeof(short));
	QSATEST_CHECK(
		decoded_hash == QSATEST_DECODED_HASH,
		"decoded hash 0x%016llx, want 0x%016llx", decoded_hash, QSATEST_DECODED_HASH
	);

	double signal_energy = 0;
	double error_energy = 0;
	for (unsigned int i = 0; i < decoded_desc.samples; i++) {
		double difference = samples[i] - decoded[i];
		signal_energy += (double)samples[i] * samples[i];
		error_energy += difference * difference;
	}
	double snr = 10.0 * log10(signal_energy / error_energy);
	QSATEST_CHECK(snr > 12.0, "snr %.2f db too low", snr);

	/* The encoder's own error is measured over padded chunks, so it can only
	exceed the decoder's by the last chunk's padding. */
	QSATEST_CHECK(desc.error >= error_energy, "encoder error below the decoder's");

	unsigned char truncated[QSA_MIN_FILESIZE + 8];
	memcpy(truncated, encoded, sizeof(truncated));
	QSATEST_CHECK(
		qsa_decode(truncated, sizeof(truncated), &decoded_desc) == NULL,
		"decoded a truncated file"
	);

	double bitrate = size * 8.0 / (desc.samples / (double)desc.samplerate) / 1000.0;
	printf(
		"%d samples, %d bytes, %.2f kbit/s, %.3f bits/sample, snr %.2f db\n",
		desc.samples, size, bitrate, size * 8.0 / desc.samples, snr
	);
	printf(failures ? "%d checks failed\n" : "all checks passed\n", failures);

	free(decoded);
	free(encoded);
	free(samples);
	return failures ? 1 : 0;
}
