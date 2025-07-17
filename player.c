#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <alsa/asoundlib.h>
#include <termios.h>
#include <unistd.h>

#include <pthread.h>

#include "biquad.h"

#define CHUNK_FRAMES 4096

// no support for IEEE float
typedef struct {
	char chunk_id[4];
	uint32_t chunk_size;
	char format[4];
	char subchunk1_id[4];
	uint32_t subchunk1_size;
	uint16_t audio_format;
	uint16_t num_channels;
	uint32_t sample_rate;
	uint32_t byte_rate; // per sec
	uint16_t block_align;
	uint16_t bps;
	char subchunk2_id[4];
	uint32_t subchunk2_size;
} WAVHeader;

struct audio_info {
	uint8_t *pcm_data;
	snd_pcm_t *pcm_handle;
	WAVHeader *audio;
	size_t frame_size;
	size_t frames_played;
	size_t total_frames;
	size_t audio_size;
};

enum FilterType {
	BQ_NONE = 0,
	BQ_PEAKING = 1,
	BQ_LOWSHELF,
	BQ_HIGHSHELF,
	BQ_LOWPASS,
	BQ_HIGHPASS,
};

// masking -
// char *buf = mmap;
// int32_t = *(int32_t *) buf;
// ((*buf) & 0x80) >>> 8;

// TODO(daria): select audio folders
// TODO(daria): saved precomputed numbers for biquad

struct termios orig_termios;

void
disable_raw_mode()
{
	tcsetattr(0, TCSANOW, &orig_termios);
}

void
enable_raw_mode()
{
	struct termios new_termios;
	tcgetattr(0, &orig_termios);
	memcpy(&new_termios, &orig_termios, sizeof(new_termios));

	atexit(disable_raw_mode);
	cfmakeraw(&new_termios);
	tcsetattr(0, TCSANOW, &new_termios);
}

static inline void
clear_screen()
{
	printf("\e[1;1H\e[2J");
}

int
kbhit()
{
	struct pollfd pfd = { .fd = STDIN_FILENO, .events = POLLIN };
	int ret_val;

	if ((ret_val = poll(&pfd, 1, 0)) > 0 && (pfd.revents & POLLIN)) {
		char c;
		read(STDIN_FILENO, &c, 1);
		if (c == ' ') { // SPACE
			return 1;
		} else if (c == 'q' || c == 'Q') {
			return 2;
		} else if (c == '<') {
			return 3;
		} else if (c == '>') {
			return 4;
		}
	}

	return 0;
}

void
audio_play(struct audio_info *info)
{
	char key;
	size_t frames_per_sec = info->audio->sample_rate ;//* info->audio->num_channels;
	uint32_t five_sec = 5 * frames_per_sec;

	size_t audio_duration = info->audio_size / info->audio->byte_rate;
	size_t audio_minutes = audio_duration / 60;
	size_t audio_seconds = audio_duration % 60;

	size_t duration_played;

	size_t fs = info->audio->sample_rate;
	size_t channels = info->audio->num_channels;

	// TODO(daria): make the eq modifiable
	struct Biquad eq[3][2];
	float gains[3] = { 1.0f, 5.0f, 5.0f };
	float freqs[3] = { 100.f, 1000.f, 10000.f };

	for (int b = 0; b < 3; b++)
		for (size_t c = 0; c < channels; c++)
			bq_lowpass(&eq[b][c], freqs[b], (float) fs, 1.0f);
			//bq_lowshelf(&eq[b][c], gains[b], freqs[b], (float)fs, 1.0f);

	static uint8_t buffer[CHUNK_FRAMES * 8];
	static float fbuf[CHUNK_FRAMES * 8];

	while (info->frames_played < info->total_frames)
	{
RESUME_AUDIO:
		key = kbhit();
		if (key == 1) { goto PAUSE_AUDIO; }
		else if (key == 2) { goto END_AUDIO; }
		else if (key == 3)
		{
			snd_pcm_drop(info->pcm_handle);

			signed int duration_pos = info->frames_played - five_sec;
			if (duration_pos < 0) {
				info->frames_played = 0;
			} else {
				info->frames_played -= five_sec;
			}

			snd_pcm_prepare(info->pcm_handle);
		} else if (key == 4) { // >
			snd_pcm_drop(info->pcm_handle);

			info->frames_played += five_sec;
			if (info->frames_played > info->total_frames) {
				info->frames_played = info->total_frames;
			}

			snd_pcm_prepare(info->pcm_handle);
		}

		size_t frames_left = info->total_frames - info->frames_played;
		size_t chunk = (frames_left > CHUNK_FRAMES) ? CHUNK_FRAMES : frames_left;
		size_t bytes_per_sample = info->audio->bps / 8;
		size_t copy_bytes = chunk * channels * bytes_per_sample; // sizeof(int16_t)

		uint8_t *chunk_ptr = (info->pcm_data + (info->frames_played * info->frame_size));
		memcpy(buffer, chunk_ptr, copy_bytes);

		uint8_t bps = info->audio->bps;
		for (size_t i = 0; i < chunk * channels; i++)
		{
			if (bps == 16)
			{
				int16_t s = ((int16_t *)buffer)[i];
				fbuf[i] = s / 32768.0f;
			}
			else if (bps == 24)
			{
				uint8_t *p = &buffer[i * 3];
				int32_t s = (p[0] | (p[1] << 8) | (p[2] << 16));
				if (s & 0x800000) s|= ~0xffffff;
				fbuf[i] = s / 8388608.0f;
			}
			else
			{
				int32_t s = ((int32_t *) buffer)[i];
				fbuf[i] = s / 2147483648.0f;
			}
		}

		for (size_t i = 0; i < chunk; i++)
		{
			for (size_t ch = 0; ch < channels; ch++)
			{
				int idx = i * channels + ch;
				float x = fbuf[idx];
				//x = bq_process(&eq[0][ch], x);
				//x = bq_process(&eq[1][ch], x);
				x = bq_process(&eq[2][ch], x);
				fbuf[idx] = x;
			}
		}

		for (size_t i = 0; i < chunk * channels; i++)
		{
			float x = fmaxf(-1.0f, fminf(1.0f, fbuf[i]));
			if (bps == 16)
			{
				((int16_t *) buffer)[i] = (int16_t)(x * 32767.0f);
			}
			else if (bps == 24)
			{
				int32_t s = (int32_t)(x * 8388607.0f);
				uint8_t *p = &buffer[i * 3];
				p[0] = s & 0xff;
				p[1] = (s >> 8) & 0xff;
				p[2] = (s >> 16) & 0xff;
			}
			else
			{
				((int32_t *) buffer)[i] = (int32_t)(x * 2147483647.0f);
			}
		}

		snd_pcm_sframes_t written = snd_pcm_writei(info->pcm_handle, buffer, chunk);

		if (written < 0) {
			//fprintf(stderr, "underrun or write error: %s\n", snd_strerror(written));
			snd_pcm_prepare(info->pcm_handle);
			continue;
		}

		info->frames_played += written;

		printf("Frames: %ld/%ld ", info->frames_played, info->total_frames);
		
		duration_played = info->frames_played / frames_per_sec;
		fprintf(stdout, "Duration: %02ld:%02ld/%02ld:%02ld\r",
				duration_played / 60,
				duration_played % 60,
				audio_minutes,
				audio_seconds
		);
		fflush(stdout);
	}

PAUSE_AUDIO:
	while ((key = kbhit()) != 2 &&
		info->frames_played < info->total_frames)
	{
		if (key == 1) { goto RESUME_AUDIO; }
	}

END_AUDIO:
	return;
}

int
main(int argc, char *argv[])
{
	int err;
	WAVHeader *header;
	FILE *fp;
	struct audio_info info;
	
	if (argc < 2) {
		printf("Usage: %s <wav file> [--filter <txt file>]\n", argv[0]);
		return 1;
	}

	enable_raw_mode();
	clear_screen();

	// File info
	{
		struct stat stat_buf;
		if ((err = stat(argv[1], &stat_buf)) != 0) {
			fprintf(stderr, "stat failed.\n");
			exit(EXIT_FAILURE);
		} 
		info.audio_size = stat_buf.st_size;
	}

	int fd = open(argv[1], O_RDONLY);
	char *file_buf = (char *) mmap(NULL, info.audio_size, PROT_READ, MAP_SHARED, fd, 0);
	if (file_buf == MAP_FAILED) {
		fprintf(stderr, "Failed to map %s file.\n", argv[1]);
		exit(EXIT_FAILURE);
	}

	header = (WAVHeader *) file_buf;

	// Check file size
	// Exclude extra 8 bytes from header
	{
		int file_size_valid = info.audio_size - 8 - header->chunk_size;
		if (file_size_valid != 0) {
			fprintf(stderr, "%s file size does not match with chunk size.\n", argv[1]);
			exit(EXIT_FAILURE);
		}
	}

	// Opens the file
	fp = fopen(argv[1], "rb");
	if (!fp) {
		perror("Failed to open file");
		exit(EXIT_FAILURE);
	}

	// Check if the file extension is .wav
	{
		char *ext = NULL;
		if ((ext = strrchr(argv[1], '.')) == NULL) {
			fprintf(stderr, "%s does not have a file extension.\n", argv[1]);
			exit(EXIT_FAILURE);
		}
		if (strcmp(ext, ".wav") != 0 && strcmp(ext, ".WAV") != 0) {
			fprintf(stderr, "%s is not a valid WAV file.", argv[1]);
			exit(EXIT_FAILURE);
		}
	}

	// Headers
	if (!(strncmp(header->chunk_id, "RIFF", 4) == 0) ||
		!(strncmp(header->format, "WAVE", 4) == 0) ||
		!(strncmp(header->subchunk1_id, "fmt ", 4) == 0)
		/*!(strncmp(header->subchunk2_id, "data", 4) == 0)*/)
	{
		fprintf(stderr, "%s is not a valid WAV file; header is not formatted properly.\n", argv[1]);
		exit(EXIT_FAILURE);
	}

	// Sample Rate in Normal Range
	if (header->sample_rate < 8000 && header->sample_rate > 192000) {
		fprintf(stderr, "%s file has sample rate headerside the normal range\n", argv[1]);
		exit(EXIT_FAILURE);
	}

	// Number of Channels
	if (header->num_channels != 1 && header->num_channels != 2) {
		fprintf(stderr, "%s file is neither mono (1) or stereo (2)\n", argv[1]);
		exit(EXIT_FAILURE);
	}
	
	// BPS
	if (!(header->bps == 8 || header->bps == 16 || header->bps == 24 || header->bps == 32)) {
		fprintf(stderr, "%s file has invalid BPS\n", argv[1]);
		exit(EXIT_FAILURE);
	}

	fprintf(stdout, "%s is a valid WAV file\n\r", argv[1]);

	// Assumes header is ~44 bytes, ignores subchunk2_size
	// to avoid wrong data.
	info.pcm_data = (uint8_t *) file_buf + sizeof(WAVHeader);
	size_t pcm_size = info.audio_size - 45; //header->subchunk2_size;

	// Opens default sound device
	snd_pcm_open(&info.pcm_handle, "default", SND_PCM_STREAM_PLAYBACK, 0);

	// Get the audio file's PCM format
	snd_pcm_format_t pcm_format;

	// PCM
	switch (header->bps) {
	case 16:
		pcm_format = SND_PCM_FORMAT_S16_LE;
		break;
	case 24:
		pcm_format = SND_PCM_FORMAT_S24_3LE;
		break;
	case 32:
		pcm_format = SND_PCM_FORMAT_S32_LE;
		break;
	default:
		fprintf(stderr, "Unsupported bit depth: %d\n", header->bps);
		return 1;
	}

	info.audio = header;
	info.frame_size = header->bps / 8 * header->num_channels;
	info.total_frames = pcm_size / info.frame_size;
	info.frames_played = 0;

	// Sets the parameters
	snd_pcm_set_params(info.pcm_handle,
			pcm_format,
			SND_PCM_ACCESS_RW_INTERLEAVED,
			header->num_channels,
			header->sample_rate,
			1, 500000);

	printf("Playing: %s\n\r", argv[1]);
	fflush(stdout);

	pthread_t player_thread;
	err = pthread_create(&player_thread, NULL, (void *) audio_play, &info);
	if (err != 0) {
		fprintf(stderr, "Failed to create a thread.\n");
		exit(EXIT_FAILURE);
	}

	pthread_join(player_thread, NULL);

	snd_pcm_drain(info.pcm_handle);
	snd_pcm_close(info.pcm_handle);
	munmap(file_buf, info.audio_size);
	close(fd);

	return 0;
}
