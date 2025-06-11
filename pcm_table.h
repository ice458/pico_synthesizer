#ifndef PCM_TABLE_H
#define PCM_TABLE_H

#include "fp.h"
#include <stdint.h>

#define PCM_ZERO_THRESHOLD 5
#define PCM_START_NOTE 35
#define PCM_END_NOTE 81
#define PCM_NOTE_COUNT 47

typedef struct
{
    const fp_t *data; // Pointer to the sample data array
    uint32_t length;  // Length of the sample data array
} pcm_sample_t;

extern const pcm_sample_t pcm_samples[PCM_NOTE_COUNT];

#endif // PCM_TABLE_H
