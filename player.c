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
#include <ctype.h>

#include <pthread.h>

#include <sys/types.h>
#include <dirent.h>
#include <errno.h>

#include "biquad.h"

#define CHUNK_FRAMES 4096
#define MAX_STRINGS 200
#define MAX_STRING_LEN 1024

#define move_cursor(x,y) fprintf(stdout, "\x1b[%d;%dH", (y), (x))
#define hide_cursor() fprintf(stdout, "\x1b[?25l")
#define show_cursor() fprintf(stdout, "\x1b[?25h")

// no support for IEEE float
typedef struct
{
    // RIFF Chunk
	char chunk_id[4];
	uint32_t chunk_size;
	char format[4];

    // FMT Subchunk
	char subchunk1_id[4];
	uint32_t subchunk1_size;
	uint16_t audio_format;
	uint16_t num_channels;
	uint32_t sample_rate;
	uint32_t byte_rate; // per sec
	uint16_t block_align;
	uint16_t bps;

    // Data Subchunk
	char subchunk2_id[4];
	uint32_t subchunk2_size;
} __attribute__((packed))
WAVHeader;

enum PlayerState {
	PLAYER_STOPPED,
	PLAYER_PAUSED,
	PLAYER_PLAYING,
};

struct audio_info
{
	WAVHeader *audio;
	size_t frame_size;
	size_t frames_played;
	size_t total_frames;
	size_t audio_size;
	enum PlayerState state;
	uint8_t *pcm_data;
	uint8_t loop;
	char *filename;
	snd_pcm_t *pcm_handle;
};

enum FilterType : int
{
	BQ_NONE = 0,
	BQ_PEAKING = 1,
	BQ_LOWSHELF,
	BQ_HIGHSHELF,
	BQ_LOWPASS,
	BQ_HIGHPASS,
};

// do i really need this? ;-;
#define INIT_BQ(X, a, b, c) \
	do { \
		(X).type = BQ_NONE; \
		memcpy((X).args, (float[]) {a, b, c}, sizeof((X).args)); \
	} while (0)

struct biquad_info
{
	enum FilterType type;
	float args[3];
	// BQ_PEAKING, BQ_LOWSHELF, BQ_HIGHSHELF
	// 0 - db_gain
	// 1 - frequency
	// 2 - quality

	// BQ_LOW_PASS, BQ_HIGHPASS
	// 0 - frequency
	// 1 - quality
};

struct biquad_info _filters[3];
uint8_t _num_filters = 0;

// masking -
// char *buf = mmap;
// int32_t = *(int32_t *) buf;
// ((*buf) & 0x80) >>> 8;

// TODO(daria): select audio folders
// TODO(daria): saved precomputed numbers for biquad

struct termios orig_termios;

static inline
void
disable_raw_mode(void)
{
	tcsetattr(0, TCSANOW, &orig_termios);
}

static inline
void
enable_raw_mode(void)
{
	struct termios new_termios;
	tcgetattr(0, &orig_termios);
	memcpy(&new_termios, &orig_termios, sizeof(new_termios));

	atexit(disable_raw_mode);
	cfmakeraw(&new_termios);
	tcsetattr(0, TCSANOW, &new_termios);
}

static inline
void
clear_screen(void)
{
	fprintf(stdout,"\x1b[1;1H\x1b[2J");
}

static inline
int
keyboard_hit(void)
{
	struct pollfd pfd = { .fd = STDIN_FILENO, .events = POLLIN };
	int ret_val;

	// TODO(daria): change ret value
	if ((ret_val = poll(&pfd, 1, 0)) > 0 && (pfd.revents & POLLIN)) {
		char c;
		read(STDIN_FILENO, &c, 1);
		if (isalpha(c) ||
			isdigit(c) ||
			c == ' ' ||
			c == '<' ||
			c == '>')
		{
			return c;
		}
	}

	return 0;
}

void *
display_screen(struct audio_info *info)
{
	// TODO(daria): add a mutex.
	size_t frames_per_sec = info->audio->sample_rate ;//* info->audio->num_channels;

	size_t audio_duration = info->audio_size / info->audio->byte_rate;
	size_t audio_minutes = audio_duration / 60;
	size_t audio_seconds = audio_duration % 60;

	size_t duration_played;
	hide_cursor();

	move_cursor(0,2);
	// clears from cursor to end of screen
	fprintf(stdout, "\x1b[J"); 

	static char state_str[3][10] = {
		"STOPPED",
		"PAUSED",
		"PLAYING",
	};

	uint8_t stop  = 0;

	// TODO(daria): change the eq values when modified
	{
		int i = 0;
		fprintf(stdout, "EQ:\n\r");
		fprintf(stdout, "  \x1b[4m#\x1b[0m   \x1b[4m%-10s\x1b[0m \x1b[4m%-10s\x1b[0m "
				"\x1b[4m%-10s\x1b[0m \x1b[4m%-5s\x1b[0m\n\r",
				"TYPE", "DB GAIN",
				"FREQUENCY", "QUALITY");
		for (i = 0; i < 3; i++)
		{
			fprintf(stdout, "  %d - %-10d %-10.1f %-10.0f %-5.1f\n\r",
					i,
					_filters[i].type,
					_filters[i].args[0],
					_filters[i].args[1],
					_filters[i].args[2]);
		}
		fprintf(stdout, "\n\r");
	}

	fprintf(stdout, "Audio: %s\n\r", info->filename);
	fflush(stdout);
	for (;;)
	{
		if (info->state == PLAYER_STOPPED)
		{
			stop = 1; // to show the player has stopped
		}

		duration_played = info->frames_played / frames_per_sec;
		move_cursor(0, 9);
		fprintf(stdout, "State: %s, Loop: %s  \n\r",
				state_str[info->state],
				info->loop ? "TRUE" : "FALSE"
		);
		fprintf(stdout, "Duration: %02ld:%02ld/%02ld:%02ld\n\r",
				duration_played / 60,
				duration_played % 60,
				audio_minutes,
				audio_seconds
	   );
		fflush(stdout);

		if (stop){
			show_cursor();
			break;
		}
	}

	pthread_exit(NULL);
}

void *
audio_play(struct audio_info *info)
{
	char key;
	size_t frames_per_sec = info->audio->sample_rate ;//* info->audio->num_channels;
	uint32_t five_sec = 5 * frames_per_sec;

	size_t fs = info->audio->sample_rate;
	uint8_t channels = info->audio->num_channels;

	// TODO(daria): modifiable eq while playing
	struct Biquad eq[3][2];
	int8_t selected_bq = 0;

	for (int b = selected_bq; b < 3 /*_num_filters*/; b++)
	{
		for (uint8_t c = 0; c < channels; c++)
		{
			switch (_filters[b].type)
			{
			case BQ_PEAKING:
				bq_peaking(&eq[b][c],
						_filters[b].args[0],
						_filters[b].args[1],
						(float) fs,
						_filters[b].args[2]);
				break;
			case BQ_LOWSHELF:
				bq_lowshelf(&eq[b][c],
						_filters[b].args[0],
						_filters[b].args[1],
						(float) fs,
						_filters[b].args[2]);
				break;
			case BQ_HIGHSHELF:
				bq_highshelf(&eq[b][c],
						_filters[b].args[0],
						_filters[b].args[1],
						(float) fs,
						_filters[b].args[2]);
				break;
			case BQ_LOWPASS:
				bq_lowpass(&eq[b][c],
						_filters[b].args[0],
						(float) fs,
						_filters[b].args[1]);
				break;
			case BQ_HIGHPASS:
				bq_highpass(&eq[b][c],
						_filters[b].args[0],
						(float) fs,
						_filters[b].args[1]);
				break;
			default:
				break;
			}
		}
	}

	static uint8_t buffer[CHUNK_FRAMES * 8];
	static float fbuf[CHUNK_FRAMES * 8];

	info->state = PLAYER_PLAYING;

	while (info->frames_played < info->total_frames)
	{
		key = keyboard_hit();
		if (key == 'q') { goto END_AUDIO; }
        else if (key == ' ')
		{
			snd_pcm_drop(info->pcm_handle);
            info->state = PLAYER_PAUSED;
			while ((key = keyboard_hit()) != 'q'  &&
				info->frames_played < info->total_frames)
			{
				if (key == ' ')
				{ 
					info->state = PLAYER_PLAYING;
                    break;
				}
			}
            
            if (key == 'q') { goto END_AUDIO;}
		}
		else if (key == '<')
		{
			snd_pcm_drop(info->pcm_handle);

			signed int duration_pos = info->frames_played - five_sec;
			if (duration_pos < 0) {
				info->frames_played = 0;
			} else {
				info->frames_played -= five_sec;
			}

			snd_pcm_prepare(info->pcm_handle);
		}
		else if (key == '>')
		{
			snd_pcm_drop(info->pcm_handle);

			info->frames_played += five_sec;
			if (info->frames_played > info->total_frames) {
				info->frames_played = info->total_frames;
			}

			snd_pcm_prepare(info->pcm_handle);
		}
		else if (key == 'l') { info->loop = !info->loop; }

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

				// TEMP change
				for (uint8_t n = 0; n < 3 /*_num_filters*/; n++)
				{
					if (_filters[n].type != BQ_NONE)
						x = bq_process(&eq[n][ch], x);
				}
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

		if (written < 0)
		{
			//fprintf(stderr, "underrun or write error: %s\n", snd_strerror(written));
			snd_pcm_prepare(info->pcm_handle);
			continue;
		}

		info->frames_played += written;

		if (info->loop && info->frames_played >= info->total_frames)
		{
			info->frames_played = 0;
		}
	}

END_AUDIO:
	info->state = PLAYER_STOPPED;
	pthread_exit(NULL);
}

int
print_files(
        int *num_dir,
        struct dirent *dir_entry,
        DIR *dir,
        char directories[MAX_STRINGS][MAX_STRING_LEN],
        char *loc_path,
        char files[MAX_STRINGS][MAX_STRING_LEN],
        int *file_idx)
{
    int num_audio_files = 0;

    fprintf(stdout, "Files:\n\r");

    // PERF(daria): searching files
    while (*num_dir > 0)
    {
        char current_path[1024] = { 0 };
        fflush(stdout);

        (*num_dir)--;
        strcpy(current_path, directories[*num_dir]);

        dir = opendir(current_path);

        if (!dir)
        {
            // TODO(daria): handle permission denied error
            if (errno == 24)
            {
                fprintf(stdout, "Notice: Reached max open files.\n\r");
                break;
            }
            perror("opendir");
            continue;
        }

        while ((dir_entry = readdir(dir)) != NULL)
        {
            if (dir_entry->d_name[0] == '.') continue;

            char full_path[1024];
            snprintf(full_path, sizeof(full_path), "%s/%s", current_path, dir_entry->d_name);

            struct stat st;
            if (stat(full_path, &st) == -1)
            {
                perror(full_path);
                continue;
            }

            if (S_ISDIR(st.st_mode) && *num_dir != MAX_STRINGS)
            {
                strncpy(directories[*num_dir], full_path, MAX_STRING_LEN);
                (*num_dir)++;
            }
            else if (S_ISREG(st.st_mode) == 1)
            {
                if (*file_idx < MAX_STRINGS)
                {
                    strncpy(files[(*file_idx)++], full_path, 1024);
                }
                else { break; }

                char *link = &full_path[strlen(loc_path) + 1];
                char *ext = strrchr(dir_entry->d_name, '.');

                if (ext && strcmp(ext, ".wav") == 0) {
                    fprintf(stdout, "\x1b[1m%s\x1b[0m\n\r", link);
                    num_audio_files++;
                    continue;
                }

                fprintf(stdout, "%s\n\r", link);
            }
        }
        closedir(dir);
    }

    return num_audio_files;
}

int
main(int argc, char *argv[])
{
	int err;
	WAVHeader header = { 0 };
	struct audio_info info;
	
	clear_screen();
	fflush(stdout);
	fflush(stderr);

	char file_path[255];
	(argc > 1) ? strncpy(file_path, argv[1], 255) : 0;

	INIT_BQ(_filters[0], -5.0f, 1000.0f, 1.0f);
	INIT_BQ(_filters[1], -5.0f, 1000.0f, 1.0f);
	INIT_BQ(_filters[2], -5.0f, 1000.0f, 1.0f);

	fprintf(stdout, "-- \x1b[34myacht\x1b[0m --\n");
	enable_raw_mode();

	if (argc < 2) // Interactive Mode
	{
        // ------------------------------- //
        // ----- DIRECTORY TRAVERSAL ----- //
        // ------------------------------- //

		DIR *dir = NULL;
		struct dirent *dir_entry = NULL;
		char directories[MAX_STRINGS][MAX_STRING_LEN];
		char files[MAX_STRINGS][MAX_STRING_LEN];
		int num_dir = 0;
		int file_idx = 0;

		char c[2] = { 0 };
		char input_line[255] = { 0 };
		int length = 0;
		int line_length = 0;

		char loc_path[255] = { 0 };

		getcwd(loc_path, 255);
		memset(&directories[0], '\0', sizeof(directories));

		int num_audio_files = 0;

        // Add current directory to explore
		strcpy(directories[num_dir], loc_path);
        num_dir++;

        // Print all files in the directory
        // including ones in the subdirectory
        num_audio_files = print_files(&num_dir, dir_entry, dir,
                directories, loc_path, files, &file_idx);

		fprintf(stdout,
				"\n\rNumber of WAV files: %d\n\r"
				"Current Directory: %s\n\r",
				num_audio_files, loc_path);

        // Shell-like loop
		while (length != -1)
		{
			fprintf(stdout, "\rEnter WAV file (Ctrl-Q to exit): %s", input_line);
			fflush(stdout);

			c[length] = '\0';
			length = read(STDIN_FILENO, c, 1);
			line_length = strlen(input_line);

			// CTRLQ
			if (c[0] == 17)  { return 0; }
            // ESC - Ignore
			else if (c[0] == '\x1b')
			{
				char seq[2];
				read(STDIN_FILENO, &seq[0], 1);
				read(STDIN_FILENO, &seq[1], 1);
				continue;
			}
			// BSPACE
			else if (c[0] == 127)
            { 
                input_line[line_length - 1] = '\0';
            }
			// ENTER
			else if (c[0] == 13)
			{
				fprintf(stdout, "\n\r");
				fflush(stdout);

				int result = chdir(input_line);
				if (result != -1)
				{
					input_line[0] = '\0';
					c[0] = '\0';
					fprintf(stdout, "\n\r");

                    getcwd(loc_path, 255);

                    memset(&directories[0], '\0', MAX_STRINGS * MAX_STRING_LEN);
                    memset(&files[0], '\0', MAX_STRINGS * MAX_STRING_LEN);
                    num_dir = 1;
                    file_idx = 0;

                    strcpy(directories[0], loc_path);

                    num_audio_files = print_files(&num_dir, dir_entry, dir,
                            directories, loc_path, files, &file_idx);

                    fprintf(stdout,
                            "\n\rNumber of WAV files: %d\n\r"
                            "Current Directory: %s\n\r",
                            num_audio_files, loc_path);
				}
                else
                {
                    fprintf(stdout, "\n\r");

                    char *pt;
                    uint8_t j = 0;
                    for (; j < file_idx; j++)
                    {
                        pt = strstr(files[j], input_line);
                        if (pt) { break; }
                    }
                    // this is horrible
                    if (pt) { strncpy(file_path, files[j], 255); break; }
                    fprintf(stdout, "%s not found\n\r", input_line);
                }
			}
			else if (line_length + 1 < 255) { strcat(input_line, c); }

			// Clear current line
			fprintf(stdout, "\x1b[1G\x1b[2K");
		}
	}
	else if (argc >= 3)
	{
        int filter_idx = -1;

		for (uint8_t i = 1; i < argc; i++)
		{
			if (strcmp(argv[i], "--filter") == 0)
			{
                filter_idx = i;
			}
		}
        
        if (filter_idx + 1 >= argc)
        {
            fprintf(stdout, "Usage: %s <wav file> [--filter <txt file>]\n\r",
                    argv[0]
            );
            exit(EXIT_FAILURE);
        }

        FILE *fp = fopen(argv[filter_idx + 1], "r");
        char *line_buf = NULL;
        size_t line_size = 0;
        ssize_t nread = 0;

        if (!fp)
        {
            fprintf(stderr, "Failed to open %s file\n\r", argv[filter_idx + 1]);
            exit(EXIT_FAILURE);
        }

        uint8_t filter_nargs = 0;
        while ((nread = getline(&line_buf, &line_size, fp)) != -1)
        {
            if (line_buf[nread - 1] == '\n')
            {
                line_buf[nread - 1] = '\0';
            }

            if (strcmp(line_buf, "BQ_PEAKING") == 0)
            {
                _filters[_num_filters].type = BQ_PEAKING;
                filter_nargs = 3;
            }
            else if (strcmp(line_buf, "BQ_LOWSHELF") == 0)
            {
                _filters[_num_filters].type = BQ_LOWSHELF;
                filter_nargs = 3;
            }
            else if (strcmp(line_buf, "BQ_HIGHSHELF") == 0)
            {
                _filters[_num_filters].type = BQ_HIGHSHELF;
                filter_nargs = 3;
            }
            else if (strcmp(line_buf, "BQ_LOWPASS") == 0)
            {
                _filters[_num_filters].type = BQ_LOWPASS;
                filter_nargs = 2;
            }
            else if (strcmp(line_buf, "BQ_HIGHPASS") == 0)
            {
                _filters[_num_filters].type = BQ_HIGHPASS;
                filter_nargs = 2;
            }
            else
            {
                continue;
            }

            for (uint8_t i = 0; i < filter_nargs; i++)
            {
                if ((nread = getline(&line_buf, &line_size, fp) == -1))
                {
                    fprintf(stderr,
                    "Not enough args for filter #%d\n\r",
                    _num_filters + 1);
                    exit(EXIT_FAILURE);
                }

                errno = 0;
                char *end;
                _filters[_num_filters].args[i] = strtof(line_buf, &end);
            }

            _num_filters++;
        }
        
        if (_num_filters == 0)
        {
            fprintf(stdout,
            "No filters are applied; none were valid.\n\r");
        }

        free(line_buf);
        fclose(fp);
	}
    
    // ------------------------------- //
    // ------- FILE READING ---------- //
    // ------------------------------- //

    // Check if the file exists
	{
		struct stat stat_buf;
		if ((err = stat(file_path, &stat_buf)) != 0) {
			fprintf(stderr, "File not found, stat failed.\n\r");
			exit(EXIT_FAILURE);
		} 
		info.audio_size = stat_buf.st_size;
	}

	int fd = open(file_path, O_RDONLY);
	char *file_buf = (char *) mmap(NULL, info.audio_size, PROT_READ, MAP_SHARED, fd, 0);
	if (file_buf == MAP_FAILED) {
		fprintf(stderr, "Failed to map %s file.\n", argv[1]);
		goto EXIT;
	}
	close(fd);

    uint8_t offset = 0;

    // RIFF Chunk

    memcpy((uint8_t *) &(header.chunk_id), file_buf + offset, 4);
    offset += 4;

    memcpy((uint8_t *) &(header.chunk_size), file_buf + offset, 4);
    offset += 4;

    memcpy((uint8_t *) &(header.format), file_buf + offset, 4);
    offset += 4;

    // RIFF/WAVE container
    if (strncmp(header.chunk_id, "RIFF", 4) != 0)
    {
        fprintf(stderr, "Not a valid RIFF/WAVE file.\n\r");
        goto CLEANUP;
    }
    // Validate file size with WAV header
    if (strncmp(header.format, "WAVE", 4) != 0)
    {
        fprintf(stderr, "WAVE chunk is not found.\n\r");
        goto CLEANUP;
    }

	// Check file size
	// Exclude extra 8 bytes from header
	{
		int file_size_valid = info.audio_size - 8 - header.chunk_size;
		if (file_size_valid != 0) {
			fprintf(stderr, "%s file size does not match with chunk size.\n", file_path);
			goto CLEANUP;
		}
	}

    {
        struct {
            char id[4];
            uint32_t size;
        } chunk;
        uint8_t found_fmt = 0;

        // Added a limit to the offset in case
        // of an invalid WAV header
        while (offset < 100)
        {
            memcpy((uint8_t *) &chunk, file_buf + offset, 8);
            offset += 8;
            printf("%.*s\n\r", 4, chunk.id);

            if (strncmp(chunk.id, "fmt ", 4) == 0)
            {
                memcpy(&(header.subchunk1_id), (uint8_t *) &chunk, 8);
                memcpy(&(header.audio_format), (uint8_t *) file_buf + offset, 16);
                offset += 16;
                found_fmt = 1;
            }
            else if (strncmp(chunk.id, "data", 4) == 0)
            {
                memcpy(&(header.subchunk2_id), &chunk, 8);
                // Reached the actual audio
                break;
            }
            else
            {
                // Skip other subchunks, checks for padding
                uint32_t bytes_to_skip = chunk.size;

                if (bytes_to_skip % 2 != 0)
                {
                    bytes_to_skip += 1;
                }

                offset += bytes_to_skip;
            }

            // TODO(daria): handle all types of chunks
            // https://www.recordingblogs.com/wiki/wave-file-format
        }

        if (!found_fmt)
        {
            fprintf(stderr, "fmt subchunk is not found\n\r");
            goto EXIT;
        }
        
        if (offset > 100)
        {
            fprintf(stderr, "WAV Header is incomplete");
            goto EXIT;
        }
    }

    // ------------------------------- //
    // --- HEADER FIELD VALIDATION --- //
    // ------------------------------- //

	// Sample Rate in Normal Range
	if (header.sample_rate < 8000 && header.sample_rate > 192000)
	{
		fprintf(stderr, "%s file has sample rate outside the normal range\n", file_path);
		goto CLEANUP;
	}

	// Number of Channels
	if (header.num_channels != 1 && header.num_channels != 2)
	{
		fprintf(stderr, "%s file is neither mono (1) or stereo (2)\n", file_path);
		goto CLEANUP;
	}
	
	// BPS
	if (!(header.bps == 8 || header.bps == 16 || header.bps == 24 || header.bps == 32))
	{
		fprintf(stderr, "%s file has invalid BPS\n", file_path);
		goto CLEANUP;
	}

	fprintf(stdout, "%s is a valid WAV file\n\r", file_path);

	// Assumes header is ~44 bytes, ignores subchunk2_size
	// to avoid wrong data.
	info.pcm_data = (uint8_t *) file_buf + offset;
	size_t pcm_size = header.subchunk2_size;//info.audio_size - 46; //header->subchunk2_size;

	// Opens default sound device
	snd_pcm_open(&info.pcm_handle, "default", SND_PCM_STREAM_PLAYBACK, 0);

	// Get the audio file's PCM format
	snd_pcm_format_t pcm_format;

	// PCM
	switch (header.bps)
	{
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
			fprintf(stderr, "Unsupported bit depth: %d\n", header.bps);
			goto CLEANUP;
	}

	info.audio = &header;
	info.frame_size = header.bps / 8 * header.num_channels;
	info.total_frames = pcm_size / info.frame_size;
	info.frames_played = 0;

	// Sets the parameters
	snd_pcm_set_params(info.pcm_handle,
			pcm_format,
			SND_PCM_ACCESS_RW_INTERLEAVED,
			header.num_channels,
			header.sample_rate,
			1, 500000);

	info.filename = strrchr(file_path, '/');
	if (info.filename == NULL) { info.filename = file_path; }
	else { info.filename += 1; }

	pthread_t player_thread;
	pthread_t screen_thread;
	err = pthread_create(&player_thread, NULL, (void *(*)(void *)) audio_play, &info);
	if (err != 0)
	{
		fprintf(stderr, "Failed to create a thread.\n");
		goto CLEANUP;
	}

	err = pthread_create(&screen_thread, NULL, (void *(*)(void *)) display_screen, &info);
	if (err != 0)
	{
		fprintf(stderr, "Failed to create a thread.\n");
		goto CLEANUP;
	}

	pthread_join(player_thread, NULL);
	pthread_join(screen_thread, NULL);

	snd_pcm_drain(info.pcm_handle);
	snd_pcm_close(info.pcm_handle);
    snd_config_update_free_global();

CLEANUP:
	munmap(file_buf, info.audio_size);
EXIT:
	fflush(stderr);
	return 0;
}
