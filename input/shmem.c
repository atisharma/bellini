#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "input/shmem.h"
#include "input/common.h"
#include "debug.h"

typedef unsigned int u32_t;
typedef short s16_t;
//int rc;

// See cava issue #375
// Hard-coded in squeezelite's output_vis.c, but
// this should be the same as mmap_area->buf_size
// if you were to dynamically allocate.
#define VIS_BUF_SIZE 16384

// format of shmem area, see squeezelite's output_vis.h
typedef struct {
    pthread_rwlock_t rwlock;
    u32_t buf_size;
    u32_t buf_index;
    bool running;
    u32_t rate;
    time_t updated;
    s16_t buffer[VIS_BUF_SIZE];
} vis_t;

// input: SHMEM
void *input_shmem(void *data) {
    struct audio_data *audio = (struct audio_data *)data;
    vis_t *mmap_area;
    int fd; /* file descriptor to mmaped area */
    int mmap_count = sizeof(vis_t);
    int buf_frames;
    // Reread multiple times each buffer replacement (overlapping windows).
    // Too slow and the fft buffer will miss sometimes because there is no
    // sync between this thread and the other.
    // This loop can go very fast indeed with minimal performance impact
    // and the benefit is fresh data for the high frequencies and no flicker.
    struct timespec req = {.tv_sec = 0, .tv_nsec = 1e9 / 3000};
    // 0.1s long sleep when not playing to lower CPU usage
    struct timespec req_silence = {.tv_sec = 0, .tv_nsec = 1e8};

    s16_t silence_buffer[VIS_BUF_SIZE];
    memset(silence_buffer, 0, sizeof(s16_t) * VIS_BUF_SIZE);

    debug("input_shmem: source: %s\n", audio->source);

    fd = shm_open(audio->source, O_RDONLY, 0666);

    if (fd < 0) {
        printf("Could not open source '%s': %s\n", audio->source, strerror(errno));
        exit(EXIT_FAILURE);
    } else {
        mmap_area = mmap(NULL, sizeof(vis_t), PROT_READ, MAP_SHARED, fd, 0);
        if ((intptr_t)mmap_area == -1) {
            printf("mmap failed - check if squeezelite is running with visualization enabled\n");
            exit(EXIT_FAILURE);
        }
    }

    while (!audio->terminate) {
        // audio rate may change between songs (e.g. 44.1kHz to 96kHz)
        audio->rate = mmap_area->rate;
        audio->running = mmap_area->running;
        buf_frames = mmap_area->buf_size / 2;       // there are two channels
        audio->index = (audio->FFTbufferSize - mmap_area->buf_index / 2) % audio->FFTbufferSize;
        if (mmap_area->running) {
            write_to_fftw_input_buffers(mmap_area->buffer, buf_frames, audio);
            nanosleep(&req, NULL);
        } else {
            write_to_fftw_input_buffers(silence_buffer, buf_frames, audio);
            nanosleep(&req_silence, NULL);
        }
    }

    // cleanup
    if (fd > 0) {
        if (close(fd) != 0) {
            printf("Could not close file descriptor %d: %s", fd, strerror(errno));
        }
    } else {
        printf("Bad file descriptor %d", fd);
    }

    if (munmap(mmap_area, mmap_count) != 0) {
        printf("Could not munmap() area %p+%d. %s", mmap_area, mmap_count, strerror(errno));
    }
    return 0;
}
