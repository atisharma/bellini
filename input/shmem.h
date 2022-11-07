// header file for shmem, part of bellini.

#pragma once

// See cava issue #375
// Hard-coded in squeezelite's output_vis.c, but
// this should be the same as mmap_area->buf_size
// if you were to dynamically allocate.
#define VIS_BUF_SIZE 16384


void *input_shmem(void *data);
