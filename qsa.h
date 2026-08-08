/*

Copyright (c) 2026, codename-B
SPDX-License-Identifier: MIT

QSA - "Quantised Sliced Audio"

Based on QOA - the "Quite OK Audio" format by Dominic Szablewski
Copyright (c) 2023, Dominic Szablewski - https://phoboslab.org

-- Data Format

QSA is a mono descendant of QOA for decoders that cannot afford QOA's 3.2
bits per sample. It keeps QOA's sign-sign LMS predictor and QOA's idea of a
4-bit quantiser scale per slice, but fixes the residual at 2 bits and
stretches the slice to 64 samples, landing at roughly 2.1 bits per sample.
The sample rate is carried in the header; the default is 9360 hz, which fits
90 minutes of audio in about 13 MB.

All values are little endian. The file layout is:

struct {
	struct {
		char     magic[4];      // magic bytes "QSA1"
		uint32_t samples;        // total samples in this file
		uint32_t samplerate;     // samples per second
		uint16_t chunk_samples;  // samples per chunk, multiple of 8 and of slice_samples
		uint16_t slice_samples;  // samples per scale, multiple of 4, divides chunk_samples
		uint32_t chunks;         // number of chunks
	} file_header;

	uint32_t index[chunks + 1];  // byte offset of each chunk, plus the file size

	struct {
		uint8_t  bits;           // always 2
		uint8_t  reserved;       // 0
		uint16_t samples;        // samples in this chunk, multiple of slice_samples
		uint8_t  scales[(samples / slice_samples + 1) / 2]; // packed nibbles, low first
		uint8_t  residuals[samples / 4];                    // 2-bit codes, lsb first
		uint8_t  padding[];      // zeroes, to a 4 byte boundary
	} chunks[chunks];
} qsa_file_t;

Every chunk but the last holds exactly chunk_samples samples. The last holds
the remainder rounded up to a whole number of slices, zero padded; those pad
samples are encoded and decoded like any other but fall outside the file's
sample count.

A residual is dequantized as {-4, -1, 1, 4}[code] * qsa_scale_tab[scale],
added to the LMS prediction and clamped to 16 bits.

The LMS filter is QOA's, with two changes. First, a leak: every fourth sample
each weight loses weight >> 7 before the sign-sign update; without it the low
bit depth lets the weights run away. Second, the prediction dot product is
specified as wrapping 32-bit arithmetic, so a fixed-point ARM decoder and this
reference are bit-identical on any input. The filter state is never stored in
the file, so a stream must be decoded from its first chunk.

The encoder picks each slice's scale by exhaustive search, minimising the
noise-shaped cost e^2 + lambda * (e[n] - e[n-1])^2. Penalising the error slope
tilts the quantisation noise floor down to follow the signal spectrum, where
the ear masks it far better than flat MSE's high-frequency hiss. The shaping
memory is encoder-only state; the bitstream and decoder know nothing of it.

*/


/* -----------------------------------------------------------------------------
	Header - Public functions */

#ifndef QSA_H
#define QSA_H

#ifdef __cplusplus
extern "C" {
#endif

#define QSA_MAGIC 0x31415351 /* 'QSA1' */
#define QSA_HEADER_SIZE 20
#define QSA_MIN_FILESIZE 28
#define QSA_DEFAULT_SAMPLERATE 9360
#define QSA_LMS_LEN 4
#define QSA_LMS_LEAK_SHIFT 7
#define QSA_SCALE_COUNT 16
#define QSA_MIN_CHUNK_SAMPLES 8
#define QSA_MAX_CHUNK_SAMPLES 65528
#define QSA_DEFAULT_CHUNK_SAMPLES 2048
#define QSA_DEFAULT_SLICE_SAMPLES 64
#define QSA_DEFAULT_SHAPE_LAMBDA 0.5

#define QSA_CHUNK_SIZE(samples, slice) \
	((4 + (((samples) / (slice) + 1) >> 1) + ((samples) >> 2) + 3) & ~3)

typedef struct {
	int history[QSA_LMS_LEN];
	int weights[QSA_LMS_LEN];
	unsigned int sample_index;
} qsa_lms_t;

typedef struct {
	unsigned int samplerate;
	unsigned int samples;
	unsigned int chunk_samples;
	unsigned int slice_samples;
	unsigned int chunks;
	double shape_lambda;
	#ifdef QSA_RECORD_TOTAL_ERROR
		double error;
	#endif
} qsa_desc;

/* Fills desc with the defaults qsa_encode() needs beyond samples. */
void qsa_desc_init(qsa_desc *desc);
void qsa_lms_init(qsa_lms_t *lms);

unsigned int qsa_max_encoded_size(const qsa_desc *desc);
void *qsa_encode(const short *sample_data, qsa_desc *desc, unsigned int *out_len);

unsigned int qsa_decode_header(const unsigned char *bytes, unsigned int size, qsa_desc *desc);
/* Decodes one chunk, bytes pointing at its header. Returns the samples written,
   which includes the last chunk's padding. */
unsigned int qsa_decode_chunk(const unsigned char *bytes, unsigned int size, const qsa_desc *desc, qsa_lms_t *lms, short *sample_data);
short *qsa_decode(const unsigned char *bytes, unsigned int size, qsa_desc *desc);

#ifndef QSA_NO_STDIO

int qsa_write(const char *filename, const short *sample_data, qsa_desc *desc);
void *qsa_read(const char *filename, qsa_desc *desc);

#endif /* QSA_NO_STDIO */


#ifdef __cplusplus
}
#endif
#endif /* QSA_H */


/* -----------------------------------------------------------------------------
	Implementation */

#ifdef QSA_IMPLEMENTATION
#include <stdlib.h>

#ifndef QSA_MALLOC
	#define QSA_MALLOC(sz) malloc(sz)
	#define QSA_FREE(p) free(p)
#endif

static const int qsa_quant_tab[4] = {-4, -1, 1, 4};

static const int qsa_scale_tab[QSA_SCALE_COUNT] = {
	16, 24, 32, 48, 64, 96, 128, 192,
	256, 384, 512, 768, 1024, 1536, 2048, 3072
};


static inline int qsa_clamp_s16(int v) {
	if ((unsigned int)(v + 32768) > 65535) {
		if (v < -32768) { return -32768; }
		if (v >  32767) { return  32767; }
	}
	return v;
}

static inline unsigned int qsa_read_u16(const unsigned char *bytes) {
	return bytes[0] | ((unsigned int)bytes[1] << 8);
}

static inline unsigned int qsa_read_u32(const unsigned char *bytes) {
	return bytes[0] | ((unsigned int)bytes[1] << 8) |
		((unsigned int)bytes[2] << 16) | ((unsigned int)bytes[3] << 24);
}

static inline void qsa_write_u16(unsigned int v, unsigned char *bytes) {
	bytes[0] = v & 0xff;
	bytes[1] = (v >> 8) & 0xff;
}

static inline void qsa_write_u32(unsigned int v, unsigned char *bytes) {
	bytes[0] = v & 0xff;
	bytes[1] = (v >> 8) & 0xff;
	bytes[2] = (v >> 16) & 0xff;
	bytes[3] = (v >> 24) & 0xff;
}

/* Wrapping 32-bit dot product, per the spec; matches an ARM mul/mla chain. */
static int qsa_lms_predict(const qsa_lms_t *lms) {
	unsigned int prediction = 0;
	for (int i = 0; i < QSA_LMS_LEN; i++) {
		prediction += (unsigned int)lms->weights[i] * (unsigned int)lms->history[i];
	}
	return (int)prediction >> 13;
}

static void qsa_lms_update(qsa_lms_t *lms, int sample, int residual) {
	int delta = residual >> 4;
	int leak = (lms->sample_index & 3) == 0;

	for (int i = 0; i < QSA_LMS_LEN; i++) {
		if (leak) {
			lms->weights[i] -= lms->weights[i] >> QSA_LMS_LEAK_SHIFT;
		}
		lms->weights[i] += lms->history[i] < 0 ? -delta : delta;
	}

	for (int i = 0; i < QSA_LMS_LEN-1; i++) {
		lms->history[i] = lms->history[i+1];
	}
	lms->history[QSA_LMS_LEN-1] = sample;
	lms->sample_index++;
}

void qsa_lms_init(qsa_lms_t *lms) {
	lms->weights[0] = 0;
	lms->weights[1] = 0;
	lms->weights[2] = -(1<<13);
	lms->weights[3] =  (1<<14);

	for (int i = 0; i < QSA_LMS_LEN; i++) {
		lms->history[i] = 0;
	}
	lms->sample_index = 0;
}

void qsa_desc_init(qsa_desc *desc) {
	desc->samplerate = QSA_DEFAULT_SAMPLERATE;
	desc->samples = 0;
	desc->chunk_samples = QSA_DEFAULT_CHUNK_SAMPLES;
	desc->slice_samples = QSA_DEFAULT_SLICE_SAMPLES;
	desc->chunks = 0;
	desc->shape_lambda = QSA_DEFAULT_SHAPE_LAMBDA;
	#ifdef QSA_RECORD_TOTAL_ERROR
		desc->error = 0;
	#endif
}



/* -----------------------------------------------------------------------------
	Encoder */

/* The LMS state plus the noise shaping memory; err_prev never leaves the
encoder, so a trial slice and its winner carry both together. */

typedef struct {
	qsa_lms_t lms;
	int err_prev;
} qsa_enc_state_t;

/* Quantises one slice at a fixed scale. Returns the noise-shaped cost used
for scale selection and stores the plain squared error in *plain_energy. */

static double qsa_encode_slice(const short *samples, unsigned int count, int scale, double lambda, qsa_enc_state_t *state, unsigned char *codes, double *plain_energy) {
	int table[4];
	for (int q = 0; q < 4; q++) {
		table[q] = qsa_quant_tab[q] * qsa_scale_tab[scale];
	}

	double cost = 0;
	*plain_energy = 0;
	for (unsigned int i = 0; i < count; i++) {
		int predicted = qsa_lms_predict(&state->lms);

		int code = 0;
		double best_weighted = 0;
		for (int q = 0; q < 4; q++) {
			int reconstructed = qsa_clamp_s16(predicted + table[q]);
			double error = (double)samples[i] - reconstructed;
			double slope = error - state->err_prev;
			double weighted = error * error + lambda * slope * slope;
			if (q == 0 || weighted < best_weighted) {
				code = q;
				best_weighted = weighted;
			}
		}

		int reconstructed = qsa_clamp_s16(predicted + table[code]);
		double error = (double)samples[i] - reconstructed;
		*plain_energy += error * error;
		cost += best_weighted;
		state->err_prev = (int)error;
		codes[i] = code;

		qsa_lms_update(&state->lms, reconstructed, table[code]);
	}
	return cost;
}

unsigned int qsa_max_encoded_size(const qsa_desc *desc) {
	unsigned int chunks = (desc->samples + desc->chunk_samples - 1) / desc->chunk_samples;
	return QSA_HEADER_SIZE + (chunks + 1) * 4 +
		chunks * QSA_CHUNK_SIZE(desc->chunk_samples, desc->slice_samples);
}

void *qsa_encode(const short *sample_data, qsa_desc *desc, unsigned int *out_len) {
	if (
		desc->samples == 0 ||
		desc->samplerate == 0 ||
		desc->chunk_samples < QSA_MIN_CHUNK_SAMPLES ||
		desc->chunk_samples > QSA_MAX_CHUNK_SAMPLES ||
		(desc->chunk_samples & 7) ||
		desc->slice_samples < 4 ||
		(desc->slice_samples & 3) ||
		desc->chunk_samples % desc->slice_samples ||
		desc->shape_lambda < 0
	) {
		return NULL;
	}

	desc->chunks = (desc->samples + desc->chunk_samples - 1) / desc->chunk_samples;
	#ifdef QSA_RECORD_TOTAL_ERROR
		desc->error = 0;
	#endif

	unsigned int slice = desc->slice_samples;
	unsigned char *bytes = QSA_MALLOC(qsa_max_encoded_size(desc));
	short *chunk = QSA_MALLOC(desc->chunk_samples * sizeof(short));
	unsigned char *codes = QSA_MALLOC(slice);
	unsigned char *best_codes = QSA_MALLOC(desc->chunk_samples);

	qsa_enc_state_t state;
	qsa_lms_init(&state.lms);
	state.err_prev = 0;

	unsigned int index_end = QSA_HEADER_SIZE + (desc->chunks + 1) * 4;
	unsigned int p = index_end;

	for (unsigned int c = 0; c < desc->chunks; c++) {
		unsigned int start = c * desc->chunk_samples;
		unsigned int valid = desc->samples - start;
		if (valid > desc->chunk_samples) { valid = desc->chunk_samples; }
		unsigned int count = ((valid + slice - 1) / slice) * slice;

		for (unsigned int i = 0; i < valid; i++) {
			chunk[i] = sample_data[start + i];
		}
		for (unsigned int i = valid; i < count; i++) {
			chunk[i] = 0;
		}

		qsa_write_u32(p, bytes + QSA_HEADER_SIZE + c * 4);

		unsigned int slice_count = count / slice;
		unsigned int nibble_bytes = (slice_count + 1) >> 1;
		unsigned int chunk_size = QSA_CHUNK_SIZE(count, slice);
		for (unsigned int i = 0; i < chunk_size; i++) {
			bytes[p + i] = 0;
		}
		bytes[p] = 2;
		qsa_write_u16(count, bytes + p + 2);
		unsigned char *nibbles = bytes + p + 4;
		unsigned char *payload = nibbles + nibble_bytes;

		for (unsigned int s = 0; s < slice_count; s++) {
			double best_cost = -1;
			double best_plain = 0;
			int best_scale = 0;
			qsa_enc_state_t best_state = state;

			for (int scale = 0; scale < QSA_SCALE_COUNT; scale++) {
				qsa_enc_state_t trial = state;
				double plain;
				double cost = qsa_encode_slice(
					chunk + s * slice, slice, scale, desc->shape_lambda,
					&trial, codes, &plain
				);
				if (best_cost < 0 || cost < best_cost) {
					best_cost = cost;
					best_plain = plain;
					best_scale = scale;
					best_state = trial;
					for (unsigned int i = 0; i < slice; i++) {
						best_codes[s * slice + i] = codes[i];
					}
				}
			}

			state = best_state;
			#ifdef QSA_RECORD_TOTAL_ERROR
				desc->error += best_plain;
			#else
				(void)best_plain;
			#endif

			if (s & 1) { nibbles[s >> 1] |= best_scale << 4; }
			else       { nibbles[s >> 1] |= best_scale; }
		}

		for (unsigned int i = 0; i < count; i += 4) {
			payload[i >> 2] = best_codes[i] | (best_codes[i + 1] << 2) |
				(best_codes[i + 2] << 4) | (best_codes[i + 3] << 6);
		}
		p += chunk_size;
	}

	qsa_write_u32(QSA_MAGIC, bytes);
	qsa_write_u32(desc->samples, bytes + 4);
	qsa_write_u32(desc->samplerate, bytes + 8);
	qsa_write_u16(desc->chunk_samples, bytes + 12);
	qsa_write_u16(desc->slice_samples, bytes + 14);
	qsa_write_u32(desc->chunks, bytes + 16);
	qsa_write_u32(p, bytes + QSA_HEADER_SIZE + desc->chunks * 4);

	QSA_FREE(best_codes);
	QSA_FREE(codes);
	QSA_FREE(chunk);

	*out_len = p;
	return bytes;
}



/* -----------------------------------------------------------------------------
	Decoder */

unsigned int qsa_decode_header(const unsigned char *bytes, unsigned int size, qsa_desc *desc) {
	if (size < QSA_MIN_FILESIZE || qsa_read_u32(bytes) != QSA_MAGIC) {
		return 0;
	}

	qsa_desc_init(desc);
	desc->samples = qsa_read_u32(bytes + 4);
	desc->samplerate = qsa_read_u32(bytes + 8);
	desc->chunk_samples = qsa_read_u16(bytes + 12);
	desc->slice_samples = qsa_read_u16(bytes + 14);
	desc->chunks = qsa_read_u32(bytes + 16);

	if (
		desc->samples == 0 || desc->chunks == 0 ||
		desc->samplerate == 0 ||
		desc->chunk_samples < QSA_MIN_CHUNK_SAMPLES ||
		(desc->chunk_samples & 7) ||
		desc->slice_samples < 4 ||
		(desc->slice_samples & 3) ||
		desc->chunk_samples % desc->slice_samples ||
		desc->chunks > (size - QSA_HEADER_SIZE) / 4 - 1 ||
		desc->chunks != (desc->samples + desc->chunk_samples - 1) / desc->chunk_samples
	) {
		return 0;
	}

	unsigned int index_end = QSA_HEADER_SIZE + (desc->chunks + 1) * 4;
	if (
		qsa_read_u32(bytes + QSA_HEADER_SIZE) != index_end ||
		qsa_read_u32(bytes + QSA_HEADER_SIZE + desc->chunks * 4) != size
	) {
		return 0;
	}

	return QSA_HEADER_SIZE;
}

unsigned int qsa_decode_chunk(const unsigned char *bytes, unsigned int size, const qsa_desc *desc, qsa_lms_t *lms, short *sample_data) {
	if (size < 4) {
		return 0;
	}

	unsigned int slice = desc->slice_samples;
	unsigned int count = qsa_read_u16(bytes + 2);

	if (
		bytes[0] != 2 || bytes[1] != 0 ||
		count == 0 || slice == 0 || count % slice ||
		count > desc->chunk_samples
	) {
		return 0;
	}

	unsigned int slice_count = count / slice;
	unsigned int nibble_bytes = (slice_count + 1) >> 1;
	if (4 + nibble_bytes + (count >> 2) > size) {
		return 0;
	}

	const unsigned char *nibbles = bytes + 4;
	const unsigned char *residuals = nibbles + nibble_bytes;

	for (unsigned int s = 0; s < slice_count; s++) {
		/* A nibble cannot exceed the table bound, so it needs no check. */
		int scale = (s & 1) ? (nibbles[s >> 1] >> 4) : (nibbles[s >> 1] & 15);

		int table[4];
		for (int q = 0; q < 4; q++) {
			table[q] = qsa_quant_tab[q] * qsa_scale_tab[scale];
		}

		for (unsigned int i = 0; i < slice; i++) {
			unsigned int n = s * slice + i;
			int code = (residuals[n >> 2] >> ((n & 3) * 2)) & 3;
			int dequantized = table[code];
			int reconstructed = qsa_clamp_s16(qsa_lms_predict(lms) + dequantized);
			sample_data[n] = reconstructed;
			qsa_lms_update(lms, reconstructed, dequantized);
		}
	}

	return count;
}

short *qsa_decode(const unsigned char *bytes, unsigned int size, qsa_desc *desc) {
	if (!qsa_decode_header(bytes, size, desc)) {
		return NULL;
	}

	unsigned int slice = desc->slice_samples;
	unsigned int padded = ((desc->samples + slice - 1) / slice) * slice;
	short *sample_data = QSA_MALLOC(padded * sizeof(short));
	qsa_lms_t lms;
	qsa_lms_init(&lms);

	unsigned int sample_index = 0;
	for (unsigned int c = 0; c < desc->chunks; c++) {
		unsigned int start = qsa_read_u32(bytes + QSA_HEADER_SIZE + c * 4);
		unsigned int end = qsa_read_u32(bytes + QSA_HEADER_SIZE + (c + 1) * 4);
		unsigned int remaining = desc->samples - sample_index;
		unsigned int expected = c + 1 < desc->chunks
			? desc->chunk_samples
			: ((remaining + slice - 1) / slice) * slice;

		/* The chunk's own sample count has to be checked before decoding it,
		as that is what bounds the write into sample_data. */
		if (
			(start & 3) || start > end || end > size || end - start < 4 ||
			qsa_read_u16(bytes + start + 2) != expected ||
			qsa_decode_chunk(bytes + start, end - start, desc, &lms, sample_data + sample_index) != expected
		) {
			QSA_FREE(sample_data);
			return NULL;
		}

		sample_index += expected < remaining ? expected : remaining;
	}

	return sample_data;
}



/* -----------------------------------------------------------------------------
	File read/write convenience functions */

#ifndef QSA_NO_STDIO
#include <stdio.h>

int qsa_write(const char *filename, const short *sample_data, qsa_desc *desc) {
	FILE *f = fopen(filename, "wb");
	unsigned int size;
	void *encoded;

	if (!f) {
		return 0;
	}

	encoded = qsa_encode(sample_data, desc, &size);
	if (!encoded) {
		fclose(f);
		return 0;
	}

	fwrite(encoded, 1, size, f);
	fclose(f);

	QSA_FREE(encoded);
	return size;
}

void *qsa_read(const char *filename, qsa_desc *desc) {
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

	data = QSA_MALLOC(size);
	if (!data) {
		fclose(f);
		return NULL;
	}

	bytes_read = fread(data, 1, size, f);
	fclose(f);

	sample_data = qsa_decode(data, bytes_read, desc);
	QSA_FREE(data);
	return sample_data;
}

#endif /* QSA_NO_STDIO */
#endif /* QSA_IMPLEMENTATION */
