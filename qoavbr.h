/*

SPDX-License-Identifier: MIT

QAV1 - "Quite OK Audio", variable bitrate

-- Data Format

QAV1 is a mono, 12800 hz sibling of QOA aimed at decoders that cannot afford
QOA's 3.2 bits per sample. It keeps QOA's sign-sign LMS predictor but spends
1, 2 or 3 bits per residual, picked per chunk from the signal's complexity, so
a whole file lands on a chosen average bit rate.

All values are little endian. The file layout is:

struct {
	struct {
		char     magic[4];      // magic bytes "QAV1"
		uint32_t samples;        // total samples in this file
		uint16_t chunk_samples;  // samples per chunk, multiple of 8
		uint16_t flags;          // 0
		uint32_t chunks;         // number of chunks
	} file_header;

	uint32_t index[chunks + 1];  // byte offset of each chunk, plus the file size

	struct {
		uint8_t  bits;           // 1, 2 or 3
		uint8_t  scale;          // index into qoavbr_scale_tab
		uint16_t samples;        // samples in this chunk, multiple of 8
		uint8_t  residuals[samples * bits / 8];
		uint8_t  padding[];      // zeroes, to a 4 byte boundary
	} chunks[chunks];
} qoavbr_file_t;

Every chunk but the last holds exactly chunk_samples samples. The last holds
the remainder rounded up to a multiple of 8, zero padded; those pad samples are
encoded and decoded like any other but fall outside the file's sample count.

Residuals are packed least significant bits first. A residual is dequantized as
qoavbr_quant_tab[bits - 1][residual] * qoavbr_scale_tab[scale], added to the LMS
prediction and clamped to 16 bits.

The LMS filter is QOA's, plus a leak: every fourth sample each weight loses
weight >> 7 before the sign-sign update. Without it the low bit depths let the
weights run away. The filter state is never stored in the file, so a stream must
be decoded from its first chunk.

*/



/* -----------------------------------------------------------------------------
	Header - Public functions */

#ifndef QOAVBR_H
#define QOAVBR_H

#ifdef __cplusplus
extern "C" {
#endif

#define QOAVBR_MAGIC 0x31564151 /* 'QAV1' */
#define QOAVBR_HEADER_SIZE 16
#define QOAVBR_MIN_FILESIZE 24
#define QOAVBR_SAMPLERATE 12800
#define QOAVBR_LMS_LEN 4
#define QOAVBR_LMS_LEAK_SHIFT 7
#define QOAVBR_SCALE_COUNT 16
#define QOAVBR_MIN_CHUNK_SAMPLES 8
#define QOAVBR_MAX_CHUNK_SAMPLES 65528
#define QOAVBR_DEFAULT_CHUNK_SAMPLES 2048
#define QOAVBR_DEFAULT_TARGET_BITS 1.75
#define QOAVBR_DEFAULT_HIGH_FRACTION 0.125

#define QOAVBR_CHUNK_SIZE(samples, bits) ((4 + (((samples) * (bits)) >> 3) + 3) & ~3)

typedef struct {
	int history[QOAVBR_LMS_LEN];
	int weights[QOAVBR_LMS_LEN];
	unsigned int sample_index;
} qoavbr_lms_t;

typedef struct {
	unsigned int samplerate;
	unsigned int samples;
	unsigned int chunk_samples;
	unsigned int chunks;
	double target_bits;
	double high_fraction;
	#ifdef QOAVBR_RECORD_TOTAL_ERROR
		double error;
	#endif
} qoavbr_desc;

/* Fills desc with the defaults qoavbr_encode() needs beyond samples. */
void qoavbr_desc_init(qoavbr_desc *desc);
void qoavbr_lms_init(qoavbr_lms_t *lms);

unsigned int qoavbr_max_encoded_size(const qoavbr_desc *desc);
void *qoavbr_encode(const short *sample_data, qoavbr_desc *desc, unsigned int *out_len);

unsigned int qoavbr_decode_header(const unsigned char *bytes, unsigned int size, qoavbr_desc *desc);
/* Decodes one chunk, bytes pointing at its header. Returns the samples written,
   which includes the last chunk's padding. */
unsigned int qoavbr_decode_chunk(const unsigned char *bytes, unsigned int size, qoavbr_lms_t *lms, short *sample_data);
short *qoavbr_decode(const unsigned char *bytes, unsigned int size, qoavbr_desc *desc);

#ifndef QOAVBR_NO_STDIO

int qoavbr_write(const char *filename, const short *sample_data, qoavbr_desc *desc);
void *qoavbr_read(const char *filename, qoavbr_desc *desc);

#endif /* QOAVBR_NO_STDIO */


#ifdef __cplusplus
}
#endif
#endif /* QOAVBR_H */


/* -----------------------------------------------------------------------------
	Implementation */

#ifdef QOAVBR_IMPLEMENTATION
#include <math.h>
#include <stdlib.h>

#ifndef QOAVBR_MALLOC
	#define QOAVBR_MALLOC(sz) malloc(sz)
	#define QOAVBR_FREE(p) free(p)
#endif


/* The quant_tab holds the dequantized residual for each bit depth, as a
multiple of the scalefactor. Each row is ascending, which the encoder's binary
search relies on. */

static const int qoavbr_quant_tab[3][8] = {
	{-2, 2},
	{-4, -1, 1, 4},
	{-7, -4, -2, -1, 1, 2, 4, 7}
};

static const int qoavbr_scale_tab[QOAVBR_SCALE_COUNT] = {
	16, 24, 32, 48, 64, 96, 128, 192,
	256, 384, 512, 768, 1024, 1536, 2048, 3072
};

/* The outer quant_tab entry per bit depth, i.e. the reach of a single
residual at scalefactor 1. */

static const int qoavbr_reach_tab[3] = {2, 4, 7};


static inline int qoavbr_clamp_s16(int v) {
	if ((unsigned int)(v + 32768) > 65535) {
		if (v < -32768) { return -32768; }
		if (v >  32767) { return  32767; }
	}
	return v;
}

static inline unsigned int qoavbr_read_u16(const unsigned char *bytes) {
	return bytes[0] | ((unsigned int)bytes[1] << 8);
}

static inline unsigned int qoavbr_read_u32(const unsigned char *bytes) {
	return bytes[0] | ((unsigned int)bytes[1] << 8) |
		((unsigned int)bytes[2] << 16) | ((unsigned int)bytes[3] << 24);
}

static inline void qoavbr_write_u16(unsigned int v, unsigned char *bytes) {
	bytes[0] = v & 0xff;
	bytes[1] = (v >> 8) & 0xff;
}

static inline void qoavbr_write_u32(unsigned int v, unsigned char *bytes) {
	bytes[0] = v & 0xff;
	bytes[1] = (v >> 8) & 0xff;
	bytes[2] = (v >> 16) & 0xff;
	bytes[3] = (v >> 24) & 0xff;
}

static int qoavbr_lms_predict(const qoavbr_lms_t *lms) {
	int prediction = 0;
	for (int i = 0; i < QOAVBR_LMS_LEN; i++) {
		prediction += lms->weights[i] * lms->history[i];
	}
	return prediction >> 13;
}

static void qoavbr_lms_update(qoavbr_lms_t *lms, int sample, int residual) {
	int delta = residual >> 4;
	int leak = (lms->sample_index & 3) == 0;

	for (int i = 0; i < QOAVBR_LMS_LEN; i++) {
		if (leak) {
			lms->weights[i] -= lms->weights[i] >> QOAVBR_LMS_LEAK_SHIFT;
		}
		lms->weights[i] += lms->history[i] < 0 ? -delta : delta;
	}

	for (int i = 0; i < QOAVBR_LMS_LEN-1; i++) {
		lms->history[i] = lms->history[i+1];
	}
	lms->history[QOAVBR_LMS_LEN-1] = sample;
	lms->sample_index++;
}

void qoavbr_lms_init(qoavbr_lms_t *lms) {
	lms->weights[0] = 0;
	lms->weights[1] = 0;
	lms->weights[2] = -(1<<13);
	lms->weights[3] =  (1<<14);

	for (int i = 0; i < QOAVBR_LMS_LEN; i++) {
		lms->history[i] = 0;
	}
	lms->sample_index = 0;
}

void qoavbr_desc_init(qoavbr_desc *desc) {
	desc->samplerate = QOAVBR_SAMPLERATE;
	desc->samples = 0;
	desc->chunk_samples = QOAVBR_DEFAULT_CHUNK_SAMPLES;
	desc->chunks = 0;
	desc->target_bits = QOAVBR_DEFAULT_TARGET_BITS;
	desc->high_fraction = QOAVBR_DEFAULT_HIGH_FRACTION;
	#ifdef QOAVBR_RECORD_TOTAL_ERROR
		desc->error = 0;
	#endif
}



/* -----------------------------------------------------------------------------
	Encoder */

typedef struct {
	double complexity;
	unsigned int index;
} qoavbr_rank_t;

static int qoavbr_rank_cmp(const void *a, const void *b) {
	const qoavbr_rank_t *ra = a;
	const qoavbr_rank_t *rb = b;
	if (ra->complexity < rb->complexity) { return -1; }
	if (ra->complexity > rb->complexity) { return  1; }
	return ra->index < rb->index ? -1 : 1;
}

static int qoavbr_int_cmp(const void *a, const void *b) {
	int va = *(const int *)a;
	int vb = *(const int *)b;
	return va < vb ? -1 : (va > vb);
}

/* Spends the bit budget on the chunks that need it: the quietest
low_fraction get 1 bit, the busiest high_fraction get 3, the rest get 2. */

static void qoavbr_choose_modes(const short *sample_data, const qoavbr_desc *desc, qoavbr_rank_t *ranks, unsigned char *modes) {
	unsigned int chunks = desc->chunks;
	double low_fraction = 2.0 + desc->high_fraction - desc->target_bits;

	for (unsigned int c = 0; c < chunks; c++) {
		unsigned int start = c * desc->chunk_samples;
		unsigned int len = desc->samples - start;
		if (len > desc->chunk_samples) { len = desc->chunk_samples; }

		double total = 0;
		for (unsigned int i = 1; i < len; i++) {
			total += abs(sample_data[start + i] - sample_data[start + i - 1]);
		}

		ranks[c].complexity = len < 2 ? 0.0 : total / (len - 1);
		ranks[c].index = c;
		modes[c] = 2;
	}

	qsort(ranks, chunks, sizeof(qoavbr_rank_t), qoavbr_rank_cmp);

	/* nearbyint() matches the round-half-to-even of the reference encoder */
	unsigned int low_count = (unsigned int)nearbyint(chunks * low_fraction);
	unsigned int high_count = (unsigned int)nearbyint(chunks * desc->high_fraction);

	for (unsigned int i = 0; i < low_count; i++) {
		modes[ranks[i].index] = 1;
	}
	for (unsigned int i = chunks - high_count; i < chunks; i++) {
		modes[ranks[i].index] = 3;
	}
}

/* Centers the scalefactor search on the 90th percentile of the chunk's first
differences, so a single residual can just about cover a typical step. */

static int qoavbr_scale_center(const short *samples, unsigned int count, int bits, int *differences) {
	unsigned int diff_count = count - 1;
	for (unsigned int i = 0; i < diff_count; i++) {
		differences[i] = abs(samples[i + 1] - samples[i]);
	}
	qsort(differences, diff_count, sizeof(int), qoavbr_int_cmp);

	int percentile = diff_count ? differences[(diff_count * 9) / 10] : 0;
	int target = percentile / qoavbr_reach_tab[bits - 1];
	if (target < 1) { target = 1; }

	int center = 0;
	int best = abs(qoavbr_scale_tab[0] - target);
	for (int s = 1; s < QOAVBR_SCALE_COUNT; s++) {
		int distance = abs(qoavbr_scale_tab[s] - target);
		if (distance < best) {
			best = distance;
			center = s;
		}
	}
	return center;
}

static long long qoavbr_encode_codes(const short *samples, unsigned int count, int bits, int scale, qoavbr_lms_t *lms, unsigned char *codes) {
	int table[8];
	int size = 1 << bits;
	for (int q = 0; q < size; q++) {
		table[q] = qoavbr_quant_tab[bits - 1][q] * qoavbr_scale_tab[scale];
	}

	long long error_energy = 0;
	for (unsigned int i = 0; i < count; i++) {
		int sample = samples[i];
		int predicted = qoavbr_lms_predict(lms);
		int wanted = sample - predicted;

		int position = 0;
		while (position < size && table[position] < wanted) { position++; }

		int code = position < size ? position : position - 1;
		int reconstructed = qoavbr_clamp_s16(predicted + table[code]);
		int error = abs(sample - reconstructed);

		if (position > 0 && position < size) {
			int alternative = qoavbr_clamp_s16(predicted + table[position - 1]);
			if (abs(sample - alternative) < error) {
				code = position - 1;
				reconstructed = alternative;
			}
		}

		codes[i] = code;
		long long difference = sample - reconstructed;
		error_energy += difference * difference;

		qoavbr_lms_update(lms, reconstructed, table[code]);
	}
	return error_energy;
}

static void qoavbr_pack_codes(const unsigned char *codes, unsigned int count, int bits, unsigned char *bytes) {
	unsigned int accumulator = 0;
	int bit_count = 0;
	unsigned int p = 0;

	for (unsigned int i = 0; i < count; i++) {
		accumulator |= (unsigned int)codes[i] << bit_count;
		bit_count += bits;
		while (bit_count >= 8) {
			bytes[p++] = accumulator & 0xff;
			accumulator >>= 8;
			bit_count -= 8;
		}
	}
}

unsigned int qoavbr_max_encoded_size(const qoavbr_desc *desc) {
	unsigned int chunks = (desc->samples + desc->chunk_samples - 1) / desc->chunk_samples;
	return QOAVBR_HEADER_SIZE + (chunks + 1) * 4 +
		chunks * QOAVBR_CHUNK_SIZE(desc->chunk_samples, 3);
}

void *qoavbr_encode(const short *sample_data, qoavbr_desc *desc, unsigned int *out_len) {
	double low_fraction = 2.0 + desc->high_fraction - desc->target_bits;
	if (
		desc->samples == 0 ||
		desc->samplerate != QOAVBR_SAMPLERATE ||
		desc->chunk_samples < QOAVBR_MIN_CHUNK_SAMPLES ||
		desc->chunk_samples > QOAVBR_MAX_CHUNK_SAMPLES ||
		(desc->chunk_samples & 7) ||
		desc->high_fraction < 0.0 || desc->high_fraction > 1.0 ||
		low_fraction < 0.0 || low_fraction > 1.0 - desc->high_fraction
	) {
		return NULL;
	}

	desc->chunks = (desc->samples + desc->chunk_samples - 1) / desc->chunk_samples;
	#ifdef QOAVBR_RECORD_TOTAL_ERROR
		desc->error = 0;
	#endif

	unsigned char *bytes = QOAVBR_MALLOC(qoavbr_max_encoded_size(desc));
	unsigned char *modes = QOAVBR_MALLOC(desc->chunks);
	qoavbr_rank_t *ranks = QOAVBR_MALLOC(desc->chunks * sizeof(qoavbr_rank_t));
	short *chunk = QOAVBR_MALLOC(desc->chunk_samples * sizeof(short));
	int *differences = QOAVBR_MALLOC(desc->chunk_samples * sizeof(int));
	unsigned char *codes = QOAVBR_MALLOC(desc->chunk_samples);
	unsigned char *best_codes = QOAVBR_MALLOC(desc->chunk_samples);

	qoavbr_choose_modes(sample_data, desc, ranks, modes);

	qoavbr_lms_t lms;
	qoavbr_lms_init(&lms);

	unsigned int index_end = QOAVBR_HEADER_SIZE + (desc->chunks + 1) * 4;
	unsigned int p = index_end;

	for (unsigned int c = 0; c < desc->chunks; c++) {
		unsigned int start = c * desc->chunk_samples;
		unsigned int valid = desc->samples - start;
		if (valid > desc->chunk_samples) { valid = desc->chunk_samples; }
		unsigned int count = (valid + 7) & ~7u;

		for (unsigned int i = 0; i < valid; i++) {
			chunk[i] = sample_data[start + i];
		}
		for (unsigned int i = valid; i < count; i++) {
			chunk[i] = 0;
		}

		int bits = modes[c];
		int center = qoavbr_scale_center(chunk, count, bits, differences);
		int first = center > 0 ? center - 1 : 0;
		int last = center + 2 < QOAVBR_SCALE_COUNT ? center + 2 : QOAVBR_SCALE_COUNT;

		long long best_error = -1;
		int best_scale = first;
		qoavbr_lms_t best_lms = lms;

		for (int scale = first; scale < last; scale++) {
			qoavbr_lms_t candidate_lms = lms;
			long long error = qoavbr_encode_codes(chunk, count, bits, scale, &candidate_lms, codes);

			if (best_error < 0 || error < best_error) {
				best_error = error;
				best_scale = scale;
				best_lms = candidate_lms;
				for (unsigned int i = 0; i < count; i++) {
					best_codes[i] = codes[i];
				}
			}
		}

		lms = best_lms;
		#ifdef QOAVBR_RECORD_TOTAL_ERROR
			desc->error += (double)best_error;
		#endif

		qoavbr_write_u32(p, bytes + QOAVBR_HEADER_SIZE + c * 4);

		unsigned int chunk_size = QOAVBR_CHUNK_SIZE(count, bits);
		for (unsigned int i = 0; i < chunk_size; i++) {
			bytes[p + i] = 0;
		}
		bytes[p] = bits;
		bytes[p + 1] = best_scale;
		qoavbr_write_u16(count, bytes + p + 2);
		qoavbr_pack_codes(best_codes, count, bits, bytes + p + 4);
		p += chunk_size;
	}

	qoavbr_write_u32(QOAVBR_MAGIC, bytes);
	qoavbr_write_u32(desc->samples, bytes + 4);
	qoavbr_write_u16(desc->chunk_samples, bytes + 8);
	qoavbr_write_u16(0, bytes + 10);
	qoavbr_write_u32(desc->chunks, bytes + 12);
	qoavbr_write_u32(p, bytes + QOAVBR_HEADER_SIZE + desc->chunks * 4);

	QOAVBR_FREE(best_codes);
	QOAVBR_FREE(codes);
	QOAVBR_FREE(differences);
	QOAVBR_FREE(chunk);
	QOAVBR_FREE(ranks);
	QOAVBR_FREE(modes);

	*out_len = p;
	return bytes;
}



/* -----------------------------------------------------------------------------
	Decoder */

unsigned int qoavbr_decode_header(const unsigned char *bytes, unsigned int size, qoavbr_desc *desc) {
	if (size < QOAVBR_MIN_FILESIZE || qoavbr_read_u32(bytes) != QOAVBR_MAGIC) {
		return 0;
	}

	qoavbr_desc_init(desc);
	desc->samples = qoavbr_read_u32(bytes + 4);
	desc->chunk_samples = qoavbr_read_u16(bytes + 8);
	desc->chunks = qoavbr_read_u32(bytes + 12);

	if (
		desc->samples == 0 || desc->chunks == 0 ||
		desc->chunk_samples < QOAVBR_MIN_CHUNK_SAMPLES ||
		(desc->chunk_samples & 7) ||
		qoavbr_read_u16(bytes + 10) != 0 ||
		desc->chunks > (size - QOAVBR_HEADER_SIZE) / 4 - 1 ||
		desc->chunks != (desc->samples + desc->chunk_samples - 1) / desc->chunk_samples
	) {
		return 0;
	}

	unsigned int index_end = QOAVBR_HEADER_SIZE + (desc->chunks + 1) * 4;
	if (
		qoavbr_read_u32(bytes + QOAVBR_HEADER_SIZE) != index_end ||
		qoavbr_read_u32(bytes + QOAVBR_HEADER_SIZE + desc->chunks * 4) != size
	) {
		return 0;
	}

	return QOAVBR_HEADER_SIZE;
}

unsigned int qoavbr_decode_chunk(const unsigned char *bytes, unsigned int size, qoavbr_lms_t *lms, short *sample_data) {
	if (size < 4) {
		return 0;
	}

	int bits = bytes[0];
	int scale = bytes[1];
	unsigned int count = qoavbr_read_u16(bytes + 2);

	if (
		bits < 1 || bits > 3 || scale >= QOAVBR_SCALE_COUNT ||
		count == 0 || (count & 7) ||
		4 + ((count * bits) >> 3) > size
	) {
		return 0;
	}

	int table[8];
	for (int q = 0; q < (1 << bits); q++) {
		table[q] = qoavbr_quant_tab[bits - 1][q] * qoavbr_scale_tab[scale];
	}

	const unsigned char *residuals = bytes + 4;
	unsigned int accumulator = 0;
	int bit_count = 0;
	int mask = (1 << bits) - 1;

	for (unsigned int i = 0; i < count; i++) {
		if (bit_count < bits) {
			accumulator |= (unsigned int)*residuals++ << bit_count;
			bit_count += 8;
		}

		int dequantized = table[accumulator & mask];
		accumulator >>= bits;
		bit_count -= bits;

		int reconstructed = qoavbr_clamp_s16(qoavbr_lms_predict(lms) + dequantized);
		sample_data[i] = reconstructed;
		qoavbr_lms_update(lms, reconstructed, dequantized);
	}

	return count;
}

short *qoavbr_decode(const unsigned char *bytes, unsigned int size, qoavbr_desc *desc) {
	if (!qoavbr_decode_header(bytes, size, desc)) {
		return NULL;
	}

	short *sample_data = QOAVBR_MALLOC(((desc->samples + 7) & ~7u) * sizeof(short));
	qoavbr_lms_t lms;
	qoavbr_lms_init(&lms);

	unsigned int sample_index = 0;
	for (unsigned int c = 0; c < desc->chunks; c++) {
		unsigned int start = qoavbr_read_u32(bytes + QOAVBR_HEADER_SIZE + c * 4);
		unsigned int end = qoavbr_read_u32(bytes + QOAVBR_HEADER_SIZE + (c + 1) * 4);
		unsigned int remaining = desc->samples - sample_index;
		unsigned int expected = c + 1 < desc->chunks
			? desc->chunk_samples
			: (remaining + 7) & ~7u;

		/* The chunk's own sample count has to be checked before decoding it,
		as that is what bounds the write into sample_data. */
		if (
			(start & 3) || start > end || end > size || end - start < 4 ||
			qoavbr_read_u16(bytes + start + 2) != expected ||
			qoavbr_decode_chunk(bytes + start, end - start, &lms, sample_data + sample_index) != expected
		) {
			QOAVBR_FREE(sample_data);
			return NULL;
		}

		sample_index += expected < remaining ? expected : remaining;
	}

	return sample_data;
}



/* -----------------------------------------------------------------------------
	File read/write convenience functions */

#ifndef QOAVBR_NO_STDIO
#include <stdio.h>

int qoavbr_write(const char *filename, const short *sample_data, qoavbr_desc *desc) {
	FILE *f = fopen(filename, "wb");
	unsigned int size;
	void *encoded;

	if (!f) {
		return 0;
	}

	encoded = qoavbr_encode(sample_data, desc, &size);
	if (!encoded) {
		fclose(f);
		return 0;
	}

	fwrite(encoded, 1, size, f);
	fclose(f);

	QOAVBR_FREE(encoded);
	return size;
}

void *qoavbr_read(const char *filename, qoavbr_desc *desc) {
	FILE *f = fopen(filename, "rb");
	int size, bytes_read;
	void *data;
	short *sample_data;

	if (!f) {
		return NULL;
	}

	fseek(f, 0, SEEK_END);
	size = ftell(f);
	if (size <= 0) {
		fclose(f);
		return NULL;
	}
	fseek(f, 0, SEEK_SET);

	data = QOAVBR_MALLOC(size);
	if (!data) {
		fclose(f);
		return NULL;
	}

	bytes_read = fread(data, 1, size, f);
	fclose(f);

	sample_data = qoavbr_decode(data, bytes_read, desc);
	QOAVBR_FREE(data);
	return sample_data;
}

#endif /* QOAVBR_NO_STDIO */
#endif /* QOAVBR_IMPLEMENTATION */
