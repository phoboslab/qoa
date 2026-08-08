/*

Copyright (c) 2023, Dominic Szablewski - https://phoboslab.org
SPDX-License-Identifier: MIT


Command line tool to play QSA files

Requires:
	-"sokol_audio.h" (https://github.com/floooh/sokol/blob/master/sokol_audio.h)

Compile with:
	gcc qsaplay.c -std=gnu99 -lasound -pthread -O3 -o qsaplay

*/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SOKOL_AUDIO_IMPL
#include "sokol_audio.h"

#define QSA_IMPLEMENTATION
#include "qsa.h"


/* -----------------------------------------------------------------------------
	qsaplay */

/* qsaplay decodes a QSA file one chunk at a time. The whole file is kept in
memory - QSA runs at about 2.7 kB per second, so even an hour is under 10 MB.

The LMS filter state is never stored in the file, so an honest seek has to
re-decode from the first chunk. That decode is far faster than real time,
which makes seeking effectively free. */

typedef struct {
	qsa_desc info;
	const unsigned char *bytes;
	unsigned int size;

	qsa_lms_t lms;
	unsigned int next_chunk;
	unsigned int sample_pos;

	unsigned int sample_data_pos;
	unsigned int sample_data_len;
	short *sample_data;
} qsaplay_desc;

qsaplay_desc *qsaplay_open(const char *path) {
	FILE *file = fopen(path, "rb");
	if (!file) {
		return NULL;
	}

	fseek(file, 0, SEEK_END);
	long size = ftell(file);
	if (size <= 0) {
		fclose(file);
		return NULL;
	}
	fseek(file, 0, SEEK_SET);

	unsigned char *bytes = malloc(size);
	int read = bytes && fread(bytes, size, 1, file);
	fclose(file);
	if (!read) {
		free(bytes);
		return NULL;
	}

	qsa_desc info;
	if (!qsa_decode_header(bytes, size, &info)) {
		free(bytes);
		return NULL;
	}

	qsaplay_desc *qp = malloc(sizeof(qsaplay_desc) + info.chunk_samples * sizeof(short));
	memset(qp, 0, sizeof(qsaplay_desc));

	qp->info = info;
	qp->bytes = bytes;
	qp->size = size;
	qp->sample_data = (short *)(((unsigned char *)qp) + sizeof(qsaplay_desc));
	qsa_lms_init(&qp->lms);
	return qp;
}

void qsaplay_close(qsaplay_desc *qp) {
	free((void *)qp->bytes);
	free(qp);
}

unsigned int qsaplay_decode_chunk(qsaplay_desc *qp) {
	if (qp->next_chunk >= qp->info.chunks) {
		return 0;
	}
	unsigned int start = qsa_read_u32(qp->bytes + QSA_HEADER_SIZE + qp->next_chunk * 4);
	unsigned int end = qsa_read_u32(qp->bytes + QSA_HEADER_SIZE + (qp->next_chunk + 1) * 4);
	if ((start & 3) || start > end || end > qp->size || end - start < 4) {
		return 0;
	}

	unsigned int decoded = qsa_decode_chunk(
		qp->bytes + start, end - start, &qp->info, &qp->lms, qp->sample_data
	);

	/* The last chunk is padded up to a whole slice; those samples are not
	part of the file and must not be played. */
	unsigned int remaining = qp->info.samples - qp->next_chunk * qp->info.chunk_samples;
	if (decoded > remaining) {
		decoded = remaining;
	}

	qp->next_chunk++;
	qp->sample_data_pos = 0;
	qp->sample_data_len = decoded;
	return decoded;
}

void qsaplay_rewind(qsaplay_desc *qp) {
	qsa_lms_init(&qp->lms);
	qp->next_chunk = 0;
	qp->sample_pos = 0;
	qp->sample_data_len = 0;
	qp->sample_data_pos = 0;
}

unsigned int qsaplay_decode(qsaplay_desc *qp, float *sample_data, int num_samples) {
	int src_index = qp->sample_data_pos;
	int dst_index = 0;
	for (int i = 0; i < num_samples; i++) {

		/* Do we have to decode more samples? */
		if (qp->sample_data_len - qp->sample_data_pos == 0) {
			if (!qsaplay_decode_chunk(qp)) {
				// Loop to the beginning
				qsaplay_rewind(qp);
				if (!qsaplay_decode_chunk(qp)) {
					sample_data[dst_index++] = 0;
					continue;
				}
			}
			src_index = 0;
		}

		/* Normalize to -1..1 floats and write to dest */
		sample_data[dst_index++] = qp->sample_data[src_index++] / 32768.0;
		qp->sample_data_pos++;
		qp->sample_pos++;
	}
	return num_samples;
}

double qsaplay_get_duration(qsaplay_desc *qp) {
	return (double)qp->info.samples / (double)qp->info.samplerate;
}

double qsaplay_get_time(qsaplay_desc *qp) {
	return (double)qp->sample_pos / (double)qp->info.samplerate;
}

int qsaplay_get_chunk(qsaplay_desc *qp) {
	return qp->sample_pos / qp->info.chunk_samples;
}

void qsaplay_seek_chunk(qsaplay_desc *qp, int chunk) {
	if (chunk < 0) {
		chunk = 0;
	}
	if ((unsigned int)chunk >= qp->info.chunks) {
		chunk = qp->info.chunks - 1;
	}

	/* The LMS state is not stored in the file, so re-decode from the first
	chunk up to the target to reconstruct it. */
	qsaplay_rewind(qp);
	while (qp->next_chunk < (unsigned int)chunk) {
		if (!qsaplay_decode_chunk(qp)) {
			return;
		}
	}
	qp->sample_pos = chunk * qp->info.chunk_samples;
	qp->sample_data_len = 0;
	qp->sample_data_pos = 0;
}




/* -----------------------------------------------------------------------------
	The application code */

/* getch() for windows/mac/linux */

#if defined(_WIN32)
	#include <conio.h>
#else
	#if defined(__APPLE__)
		#include <unistd.h>
	#endif
	#include <termios.h>
	int getch(void) {
		struct termios oldattr, newattr;
		int ch;
		tcgetattr(STDIN_FILENO, &oldattr);
		newattr = oldattr;
		newattr.c_lflag &= ~(ICANON | ECHO);
		tcsetattr(STDIN_FILENO, TCSANOW, &newattr);
		ch = getchar();
		tcsetattr(STDIN_FILENO, TCSANOW, &oldattr);
		return ch;
	}
#endif


/* Sokol Audio callback. This gets called whenever sokol needs more samples to
hand over to the platform's audio API. All decoding is done here. */

static void sokol_audio_cb(float* sample_data, int num_samples, int num_channels, void *user_data) {
	qsaplay_desc *qsaplay = (qsaplay_desc *)user_data;
	if (num_channels != 1) {
		printf("Audio cb channels %d, but QSA is mono\n", num_channels);
		exit(1);
	}

	qsaplay_decode(qsaplay, sample_data, num_samples);

	printf("\r %6.2f / %.2f sec", qsaplay_get_time(qsaplay), qsaplay_get_duration(qsaplay));
	fflush(stdout);
}


int main(int argc, char **argv) {
	if (argc < 2) {
		printf("Usage: qsaplay <file.qsa>\n");
		exit(1);
	}

	qsaplay_desc *qsaplay = qsaplay_open(argv[1]);

	if (!qsaplay) {
		printf("Failed to load %s\n", argv[1]);
		exit(1);
	}

	printf(
		"%s: mono, samplerate: %d hz, samples: %d, duration: %d sec\n",
		argv[1],
		qsaplay->info.samplerate,
		qsaplay->info.samples,
		qsaplay->info.samples/qsaplay->info.samplerate
	);
	printf("Controls: [x] rewind / [c] skip / [q] quit\n");

	saudio_setup(&(saudio_desc){
		.sample_rate = qsaplay->info.samplerate,
		.num_channels = 1,
		.stream_userdata_cb = sokol_audio_cb,
		.user_data = qsaplay
	});

	int wants_to_quit = 0;
	while (!wants_to_quit) {
		char c = getch();
		switch (c) {
			case 'c': qsaplay_seek_chunk(qsaplay, qsaplay_get_chunk(qsaplay) + 48); break;
			case 'x': qsaplay_seek_chunk(qsaplay, qsaplay_get_chunk(qsaplay) - 48); break;
			case 'q': wants_to_quit = 1; break;
		}
	}

	saudio_shutdown();
	qsaplay_close(qsaplay);

	printf("\n");
	return 0;
}
