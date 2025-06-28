#ifndef ADPCM_TABLE_H
#define ADPCM_TABLE_H

#include "fp.h" // For fp_t
#include <stdint.h>

#define ADPCM_ZERO_THRESHOLD 5
#define ADPCM_START_NOTE 35
#define ADPCM_END_NOTE 81
#define ADPCM_NOTE_COUNT 47

// ADPCM decoder state
typedef struct {
    int16_t predictor;
    int8_t step_index;
} adpcm_state_t;

typedef struct {
    const uint8_t* data;    // Pointer to the ADPCM compressed data
    uint32_t length;        // Length of the ADPCM data in bytes
    uint32_t sample_count;  // Number of PCM samples when decompressed
} adpcm_sample_t;

// ADPCM step size table
extern const int16_t adpcm_step_table[89];

// ADPCM index adjustment table
extern const int8_t adpcm_index_table[16];

extern const adpcm_sample_t adpcm_samples[ADPCM_NOTE_COUNT];

// ADPCM decoder function
fp_t adpcm_decode_sample(uint8_t adpcm_sample, adpcm_state_t* state);

#endif // ADPCM_TABLE_H
