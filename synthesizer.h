#ifndef SYNTHESIZER_H
#define SYNTHESIZER_H

#include "pico/stdlib.h"
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include "hardware/interp.h"
#include "fp.h"
#include "wave_table.h"
#include "midi.h"
#include "pitch_bend_table_interpolated.h"
#include "vibrato_table.h"
#include "pcm_table.h"

#define FS (40e3f)
#define TABLE_LENGTH_q8 (TABLE_LENGTH << 8)
#define MAX_VOICE_NUM 20
#define MAX_CHANNEL_NUM 16

#define MAX_SASTAIN_LENGTH 7 // Maximum sustain length in seconds
#define ENV_COUNTER_THRESHOLD ((uint32_t)(MAX_SASTAIN_LENGTH * FS / 127.0f / 128.0f))

#define HPF_CUTOFF_FREQ 2.0f
#define HPF_RC (1.0f / (2.0f * M_PI * HPF_CUTOFF_FREQ))
#define HPF_ALPHA (float_to_fp(HPF_RC / (HPF_RC + (1.0f / FS))))

#define PCM_INITIAL_SILENCE_SAMPLES 10

// Reverb Parameters
#define REVERB_COMB_FILTER_COUNT 2
#define REVERB_ALLPASS_FILTER_COUNT 1
#define MAX_REVERB_COMB_DELAY_SAMPLES 6000
#define MAX_REVERB_ALLPASS_DELAY_SAMPLES 800

typedef union
{
    uint32_t u32; // Access as a single 32-bit unsigned integer
    struct
    {
        fp_t left;  // Left channel (16-bit)
        fp_t right; // Right channel (16-bit)
    } ch;
} stereo_t;

typedef struct
{
    // Comb filters (parallel)
    fp_t comb_buffer_l[REVERB_COMB_FILTER_COUNT][MAX_REVERB_COMB_DELAY_SAMPLES];
    fp_t comb_buffer_r[REVERB_COMB_FILTER_COUNT][MAX_REVERB_COMB_DELAY_SAMPLES];
    uint16_t comb_delay_times[REVERB_COMB_FILTER_COUNT]; // Actual delay length in samples
    fp_t comb_feedback_gain[REVERB_COMB_FILTER_COUNT];
    uint16_t comb_write_ptr[REVERB_COMB_FILTER_COUNT];

    // Allpass filters (series)
    fp_t allpass_buffer_l[REVERB_ALLPASS_FILTER_COUNT][MAX_REVERB_ALLPASS_DELAY_SAMPLES];
    fp_t allpass_buffer_r[REVERB_ALLPASS_FILTER_COUNT][MAX_REVERB_ALLPASS_DELAY_SAMPLES];
    uint16_t allpass_delay_times[REVERB_ALLPASS_FILTER_COUNT]; // Actual delay length in samples
    fp_t allpass_feedback_gain[REVERB_ALLPASS_FILTER_COUNT];
    uint16_t allpass_write_ptr[REVERB_ALLPASS_FILTER_COUNT];

    fp_t wet_level;
    fp_t dry_level;
} reverb_state_t;

typedef enum
{
    SIN = 0,
    SQU = 1,
    SAW = 2,
    TRI = 3,
    NOISE = 4,
} wave_type_t;

typedef enum
{
    ATTACK,
    DECAY,
    SUSTAIN,
    RELEASE,
    IDLE,
} env_state_t;

typedef struct
{
    struct
    {
        wave_type_t type; // Wave type
    } osc1;               // Oscillator parameters

    struct
    {
        int8_t freq_rate; // freq2=freq1*(freq_rate + 1)/32
        int8_t rm_gain;   // Ring modulation gain
    } rm;                 // Ring modulation parameters

    struct
    {
        int8_t attack_time;   // Attack time
        int8_t decay_time;    // Decay time
        int8_t sustain_level; // Sustain level
        int8_t sustain_rate;  // Sustain rate
        int8_t release_time;  // Release time
    } env;                    // Envelope parameters
    int8_t output_gain;       // Output gain
} tone_t;

#include "tone.h"

typedef struct
{
    int8_t assigned_channel_num;
    tone_t tone;
    int8_t note;     // MIDI note number (0 to 127)
    int8_t velocity; // Velocity (0 to 127)
    struct
    {
        wave_type_t type;  // Wave type
        q8_t increment;    // Phase increment for the oscillator
        q8_t read_pointer; // Read pointer for the oscillator waveform
    } osc1;                // First oscillator state

    struct
    {
        q8_t increment;    // Phase increment for the second oscillator
        q8_t read_pointer; // Read pointer for the second oscillator waveform
    } osc2;                // Second oscillator state

    struct
    {
        q8_t factor; // Pitch bend factor for the oscillator
    } pb;

    struct
    {
        q8_t increment;
        q8_t read_pointer; // Read pointer for the vibrato waveform
        q8_t factor;       // Vibrato factor
        int8_t depth;      // Depth of the vibrato
        int8_t freq;       // Speed of the vibrato
    } vibrato;             // Vibrato parameters

    struct
    {
        fp_t amplitude;            // Amplitude of the oscillator waveform
        env_state_t state;         // Current state of the envelope
        uint32_t counter;          // Counter for envelope timing (based on fs)
        bool is_note_off_received; // Flag to indicate if a note off event has been received
    } env;                         // Envelope state

    struct
    {
        fp_t prev_out; // Previous output value for DC cut
        fp_t prev_in;  // Previous input value for DC cut
    } dc_cut;          // DC cut parameters

    fp_t amplitude;                     // Current amplitude of the voice
    uint32_t pcm_initial_delay_counter; // PCM initial delay counter
} voice_state_t;

typedef enum
{
    PARAM_TYPE_NONE,
    PARAM_TYPE_RPN,
    PARAM_TYPE_NRPN
} param_type_t;

// NRPN/RPN state structure
typedef struct
{
    param_type_t param_type; // Current parameter type
    uint8_t nrpn_msb;        // NRPN MSB (CC 99)
    uint8_t nrpn_lsb;        // NRPN LSB (CC 98)
    uint8_t rpn_msb;         // RPN MSB (CC 101)
    uint8_t rpn_lsb;         // RPN LSB (CC 100)
    uint8_t data_msb;        // Data entry MSB (CC 6)
    uint8_t data_lsb;        // Data entry LSB (CC 38)
    bool data_msb_received;  // Flag to indicate if Data MSB has been received
    bool data_lsb_received;  // Flag to indicate if Data LSB has been received
} nrpn_rpn_state_t;

// Channel state structure
typedef struct
{
    tone_t tone;        // tone parameters for the channel
    fp_t volume;        // volume (0-1.0f)
    uint8_t expression; // expression (0-127)
    struct
    {
        int8_t sensitivity; // sensitivity of pitch bend (0-24)
        uint16_t range;     // pitch bend range (0-16383)
    } pitch_bend;
    struct
    {
        uint8_t depth; // depth of modulation (0-127)
        uint8_t freq;  // frequency of modulation (0-127)
    } mod;
    bool is_hold_on;           // Hold mode (true if hold is on)
    uint8_t pan;               // Pan (0-127, 64 is center)
    nrpn_rpn_state_t nrpn_rpn; // NRPN/RPN state
} ch_state_t;

typedef struct
{
    struct
    {
        stereo_t prev_out; // Previous output value for DC cut
        stereo_t prev_in;  // Previous input value for DC cut
    } dc_cut;              // DC cut parameters
} master_state_t;

extern voice_state_t voice_state[MAX_VOICE_NUM];
extern ch_state_t channel_state[MAX_CHANNEL_NUM];
extern master_state_t master_state;
extern reverb_state_t reverb_state;

void set_voice_state(voice_state_t *vs, tone_t *tone, int8_t ch, int8_t note, int8_t velocity);
void voice(voice_state_t *vs);
void init_channel(int8_t ch_to_init);
void init_master();
void init_reverb();
stereo_t reverb(stereo_t input);
void note_on(midi_t *midi, voice_state_t *vs);
void note_off(midi_t *midi, voice_state_t *vs);
stereo_t channel(voice_state_t *vs, int8_t ch);
stereo_t master(voice_state_t *vs);
void synthesizer_task();
void handle_rpn_nrpn(int8_t channel, bool is_nrpn, uint16_t parameter, uint16_t value);

#endif