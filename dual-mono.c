#include "string.h"
#include "stdlib.h"
#include "stdio.h"
#include "fcntl.h"
#include "unistd.h"
#include "math.h"
#include "signal.h"

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;
typedef int s32;

#pragma pack(push, 1)

struct riff_header {
	char id[4];
	u32 size;
	char format[4];
};

struct fmt_chunk {
	char id[4];
	u32 size;
	u16 audio_format;
	u16 channels;
	u32 sample_rate;
	u32 bytes_per_second;
	u16 bytes_per_frame;
	u16 bits_per_sample;
};

struct data_chunk {
	char id[4];
	u32 size;
};

#pragma pack(pop)

#define MIN_WAVE_FILE_SIZE sizeof(struct riff_header) + sizeof(struct fmt_chunk) + sizeof(struct data_chunk)

volatile sig_atomic_t aborted = 0;

void handle_signal(int signal) {

	if (signal == SIGINT)
		aborted = 1;

}

int main(int argc, char *argv[]) {

	if (argc < 3)
		return -1;

	char *input_file_path = argv[1];
	char *output_file_path = argv[2];

	FILE *input_file = fopen(input_file_path, "r");
	if (input_file == NULL)
		return -1;

	int result = 0;

	result = fseeko(input_file, 0, SEEK_END);
	if (result != 0)
		return -1;

	size_t input_file_size = ftello(input_file);
	if (input_file_size < MIN_WAVE_FILE_SIZE)
		return -1;

	rewind(input_file);

	struct riff_header riff;

	result = fread(&riff, sizeof(riff), 1, input_file);
	if (result != 1)
		return -1;

	result = memcmp("RIFF", riff.id, 4);
	if (result != 0)
		return -1;

	result = memcmp("WAVE", riff.format, 4);
	if (result != 0)
		return -1;

	if (riff.size != input_file_size - 8)
		return -1;

	struct fmt_chunk fmt;

	result = fread(&fmt, sizeof(fmt), 1, input_file);
	if (result != 1)
		return -1;

	result = memcmp("fmt ", fmt.id, 4);
	if (result != 0)
		return -1;

	if (fmt.audio_format != 1)
		return -1;

	if (fmt.channels != 2)
		return -1;

// FIXME support other bit depths
	if (fmt.bits_per_sample != 24)
		return -1;

	if (fmt.size != 16)
		return -1;

	u32 bytes_per_second = fmt.bytes_per_second;
	u16 bytes_per_frame = fmt.bytes_per_frame;

	u16 bytes_per_sample = 8 * fmt.bits_per_sample;

	struct data_chunk data;

	do {

		result = fread(&data, sizeof(data), 1, input_file);
		if (result != 1)
			return -1;

		result = memcmp("data", data.id, 4);
		if (result != 0)
			fseeko(input_file, data.size, SEEK_CUR);

	} while (result != 0);

	u32 data_size = data.size;

	u64 n_frames = data_size / bytes_per_frame;

	printf("Audio format: %d\n", fmt.audio_format);
	printf("Channels: %d\n", fmt.channels);
	printf("Sample rate: %d\n", fmt.sample_rate);
	printf("Bytes per second: %d\n", fmt.bytes_per_second);
	printf("Bytes per frame: %d\n", fmt.bytes_per_frame);
	printf("Bits per sample: %d\n", fmt.bits_per_sample);
	printf("Frame count: %lld\n", n_frames);

	FILE *output_file = fopen(output_file_path, "wx");
	if (output_file == NULL)
		return -1;

	riff.size = sizeof(riff) + sizeof(fmt) + sizeof(data) + n_frames * sizeof(s32);

	result = fwrite(&riff, sizeof(riff), 1, output_file);
	if (result != 1)
		return -1;

	fmt.channels = 1;
	fmt.bytes_per_second = sizeof(s32) * fmt.sample_rate;
	fmt.bytes_per_frame = sizeof(s32);
	fmt.bits_per_sample = 8 * sizeof(s32);

	result = fwrite(&fmt, sizeof(fmt), 1, output_file);
	if (result != 1)
		return -1;

	data.size = n_frames * sizeof(s32);

	result = fwrite(&data, sizeof(data), 1, output_file);
	if (result != 1)
		return -1;

	sig_t signal_result = signal(SIGINT, handle_signal);
	if (signal_result == SIG_ERR)
		return -1;

	u64 clipped_samples = 0;

	printf("Working...\n");

	for (u64 i = 0; i < n_frames; ++i) {

		if (aborted) {

			printf("Aborted, %lld samples processed.\n", i);

			rewind(output_file);

			riff.size = sizeof(riff) + sizeof(fmt) + sizeof(data) + i * sizeof(s32);

			result = fwrite(&riff, sizeof(riff), 1, output_file);
			if (result != 1)
				return -1;

			fseeko(output_file, sizeof(fmt), SEEK_CUR);

			data.size = i * sizeof(s32);

			result = fwrite(&data, sizeof(data), 1, output_file);
			if (result != 1)
				return -1;

			break;

		}

		u8 frame[6];

		result = fread(&frame, sizeof(frame), 1, input_file);
		if (result != 1)
			return -1;

		s32 sample = (frame[0] << 8 | frame[1] << 16 | frame[2] << 24) >> 8;

		if (abs(sample) >= 0x7fffff) {
			sample = 10 * ((frame[3] << 8 | frame[4] << 16 | frame[5] << 24) >> 8);
			++clipped_samples;
		}

		// lossless, will never clip when the the second channel is -20dB
		sample <<= 4;

		result = fwrite(&sample, sizeof(sample), 1, output_file);
		if (result != 1)
			return -1;

	}

	printf("%lld samples were recovered.\n", clipped_samples);

	fclose(input_file);

	fclose(output_file);

	return 0;

}
