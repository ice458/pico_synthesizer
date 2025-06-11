/*
Synthesizer
├─ synthesizer.h
│    ├─ enum definitions
│    │    ├─ wave_type_t (SIN, SQU, SAW, TRI, NOISE)
│    │    ├─ env_state_t (ATTACK, DECAY, SUSTAIN, RELEASE, IDLE)
│    │    └─ sweep_type_t (SWEEP_NONE, SWEEP_UP, SWEEP_DOWN)
│    ├─ struct definitions
│    │    ├─ tone_t
│    │    │    ├─ osc1 (wave_type_t type)
│    │    │    ├─ rm (int8_t freq_rate, int8_t rm_gain)
│    │    │    ├─ sweep (sweep_type_t type, int8_t speed)
│    │    │    ├─ env (int8_t attack_time, decay_time, sustain_level, sustain_rate, release_time)
│    │    │    └─ output_gain (fp_t)
│    │    ├─ voice_state_t
│    │    │    ├─ assigned_channel_num (int8_t)
│    │    │    ├─ tone (tone_t)
│    │    │    ├─ note (int8_t)
│    │    │    ├─ velocity (int8_t)
│    │    │    ├─ osc1 (wave_type_t type, q8_t increment, q8_t read_pointer)
│    │    │    ├─ osc2 (q8_t increment, q8_t read_pointer)
│    │    │    ├─ pb (q8_t factor)
│    │    │    ├─ env (fp_t amplitude, env_state_t state, uint32_t counter, bool is_note_off_received)
│    │    │    ├─ dc_cut (fp_t prev_out, fp_t prev_in)
│    │    │    └─ amplitude (fp_t)
│    │    └─ ch_state_t
│    │         ├─ tone (tone_t)
│    │         ├─ pitch_bend (int8_t sensitivity, uint16_t range)
│    │         ├─ volume (fp_t)
│    │         ├─ expression (uint8_t)
│    │         ├─ mod (uint8_t depth, uint8_t freq)
│    │         ├─ is_hold_on (bool)
│    │         ├─ pan (uint8_t)
│    │         └─ nrpn_rpn (param_type_t param_type, uint8_t nrpn_msb, uint8_t nrpn_lsb, uint8_t rpn_msb, uint8_t rpn_lsb, uint8_t data_msb, uint8_t data_lsb, bool has_lsb)
│    ├─ include "tone.h"
│    ├─ global variable definitions
│    │    ├─ voice_state_t voice_state[MAX_VOICE_NUM]
│    │    ├─ ch_state_t channel_state[MAX_CHANNEL_NUM]
│    │    ├─ master_state_t master_state
│    │    └─ reverb_state_t reverb_state
│    └─ function declarations
│         ├─ set_voice_state
│         ├─ voice
│         ├─ init_channel
│         ├─ init_master
│         ├─ init_reverb
│         ├─ note_on
│         ├─ note_off
│         ├─ channel
│         ├─ reverb
│         ├─ master
│         └─ synthesizer_task
│
├─ tone.c (tone_gm definition)
│
├─ pitch_bend_table.h (pitch_bend_factors_all definition)
│
├─ wave_table.h (wave_table definitions)
│
├─ fp.h (fixed-point arithmetic definitions)
│
├─ midi.h (MIDI buffer and event handling)
│
├─ synthesizer.c
│    ├─ global variables definitions
│    │    ├─ voice_state_t voice_state[MAX_VOICE_NUM]
│    │    ├─ ch_state_t channel_state[MAX_CHANNEL_NUM]
│    │    ├─ master_state_t master_state
│    │    └─ reverb_state_t reverb_state
│    ├─ function definitions
│    │    ├─ set_voice_state(voice_state_t *vs, tone_t *tone, int8_t ch, int8_t note, int8_t velocity)
│    │    ├─ voice(voice_state_t *vs)
│    │    ├─ init_channel(int8_t ch_to_init)
│    │    ├─ init_master()
│    │    ├─ init_reverb()
│    │    ├─ note_on(midi_t *midi, voice_state_t *vs)
│    │    ├─ note_off(midi_t *midi, voice_state_t *vs)
│    │    ├─ channel(voice_state_t *vs, int8_t ch)
│    │    ├─ reverb(stereo_t input)
│    │    ├─ master(voice_state_t *vs)
│    │    └─ synthesizer_task()
*/

#include "synthesizer.h"
#include "pan_table.h"

voice_state_t voice_state[MAX_VOICE_NUM];
ch_state_t channel_state[MAX_CHANNEL_NUM];
master_state_t master_state;
reverb_state_t reverb_state;

static inline q8_t get_interpolated_pitch_bend_factor(int8_t sensitivity_idx, uint16_t bend_value_midi)
{
    if (sensitivity_idx < 0 || sensitivity_idx >= 25)
    {
        sensitivity_idx = 2; // Default to +/- 2 semitones if out of range
    }

    if (PITCH_BEND_INTERPOLATED_TABLE_SIZE == 0)
        return float_to_q8(1.0f);
    if (PITCH_BEND_INTERPOLATED_TABLE_SIZE == 1)
        return pitch_bend_factors_interpolated[sensitivity_idx][0];

    const uint32_t original_table_max_input = PITCH_BEND_ORIGINAL_TABLE_SIZE - 1;
    const uint32_t interpolated_table_max_idx = PITCH_BEND_INTERPOLATED_TABLE_SIZE - 1;

    if (original_table_max_input == 0)
    {
        return pitch_bend_factors_interpolated[sensitivity_idx][0];
    }
    if (bend_value_midi >= original_table_max_input)
    {
        return pitch_bend_factors_interpolated[sensitivity_idx][interpolated_table_max_idx];
    }

    uint32_t scaled_numerator = (uint32_t)bend_value_midi * interpolated_table_max_idx;

    int idx1 = scaled_numerator / original_table_max_input;

    uint32_t remainder = scaled_numerator % original_table_max_input;
    uint8_t fraction_u8 = (uint8_t)((remainder * 256U) / original_table_max_input);

    int idx2 = idx1 + 1;

    q8_t val1 = pitch_bend_factors_interpolated[sensitivity_idx][idx1];
    q8_t val2 = pitch_bend_factors_interpolated[sensitivity_idx][idx2];

    int32_t diff = (int32_t)val2 - (int32_t)val1;
    int32_t term = (diff * fraction_u8) >> 8;

    return val1 + (q8_t)term;
}

void set_voice_state(voice_state_t *vs, tone_t *tone, int8_t ch, int8_t note, int8_t velocity)
{
    // Initialize channel number
    vs->assigned_channel_num = ch;

    // Initialize tone
    vs->tone = *tone;

    // Set MIDI note number
    vs->note = note;
    if (vs->assigned_channel_num != 9)
    {
        // Initialize oscillator 1
        vs->osc1.type = tone->osc1.type;
        vs->osc1.increment = increment_table[note];
        vs->osc1.read_pointer = 0;

        // Initialize oscillator 2
        vs->osc2.increment = increment_table[note] * (tone->rm.freq_rate + 1) / 32;
        vs->osc2.read_pointer = 0;

        if (ch >= 0 && ch < MAX_CHANNEL_NUM)
        {
            vs->pb.factor = get_interpolated_pitch_bend_factor(
                channel_state[ch].pitch_bend.sensitivity,
                channel_state[ch].pitch_bend.range);
        }
        else
        {
            // Default to no bend if channel is invalid
            vs->pb.factor = get_interpolated_pitch_bend_factor(2, 8192); // Sensitivity 2, center value
        }

        // Initialize vibrato (not used in this version)
        vs->vibrato.increment = vibrato_table[channel_state[ch].mod.freq];
        vs->vibrato.read_pointer = 0;
        vs->vibrato.factor = float_to_q8(1.0f);
        vs->vibrato.depth = channel_state[ch].mod.depth;
        vs->vibrato.freq = channel_state[ch].mod.freq;

        // Initialize envelope
        vs->env.amplitude = 0;
        vs->env.state = ATTACK;
        vs->env.counter = 0;
        vs->env.is_note_off_received = false;
    }
    else
    {
        // Initialize PCM oscillator
        vs->osc1.increment = 1;
        vs->osc1.read_pointer = 0;
        vs->env.state = ATTACK;                                      // Less likely to be deprived of voice duaring PCM playback
        vs->pcm_initial_delay_counter = PCM_INITIAL_SILENCE_SAMPLES; // To make sure PCM playback starts after a delay
    }

    // Initialize DC cut filter
    vs->dc_cut.prev_out = 0;
    vs->dc_cut.prev_in = 0;

    // Set velocity
    vs->velocity = velocity;

    // Set initial amplitude
    vs->amplitude = 0;
}

void voice(voice_state_t *vs)
{
    fp_t wave1, wave2;
    if (vs->assigned_channel_num != 9)
    {
        // osc1
        switch (vs->tone.osc1.type)
        {
        case SIN:
            wave1 = sin_table[q8_to_int32_t(vs->osc1.read_pointer)];
            break;
        case SQU:
            wave1 = square_table[q8_to_int32_t(vs->osc1.read_pointer)];
            break;
        case SAW:
            wave1 = sawtooth_table[q8_to_int32_t(vs->osc1.read_pointer)];
            break;
        case TRI:
            wave1 = triangle_table[q8_to_int32_t(vs->osc1.read_pointer)];
            break;
        case NOISE:
            wave1 = noise_table[q8_to_int32_t(vs->osc1.read_pointer)];
            break;
        }
        q8_t tmp_increment = q8_mul(vs->osc1.increment, vs->pb.factor);
        tmp_increment = q8_mul(tmp_increment, vs->vibrato.factor);
        vs->osc1.read_pointer += tmp_increment;
        if (vs->osc1.read_pointer >= (TABLE_LENGTH_q8))
            vs->osc1.read_pointer -= (TABLE_LENGTH_q8);

        // osc2
        wave2 = sin_table[q8_to_int32_t(vs->osc2.read_pointer)];
        tmp_increment = q8_mul(vs->osc2.increment, vs->pb.factor);
        tmp_increment = q8_mul(tmp_increment, vs->vibrato.factor);
        vs->osc2.read_pointer += tmp_increment;
        if (vs->osc2.read_pointer >= (TABLE_LENGTH_q8))
            vs->osc2.read_pointer -= (TABLE_LENGTH_q8);

        // vibrato(LFO)
        if (vs->vibrato.depth != 0)
        {
            fp_t lfo_sample_fp = sin_table[q8_to_int32_t(vs->vibrato.read_pointer)];
            q8_t lfo_bipolar_q8 = (q8_t)(lfo_sample_fp >> 7);
            q8_t delta_q8 = (q8_t)(((int32_t)lfo_bipolar_q8 * (int32_t)vs->vibrato.depth * 10) >> 15);
            vs->vibrato.factor = float_to_q8(1.0f) + delta_q8;
            vs->vibrato.read_pointer += vs->vibrato.increment;
            if (vs->vibrato.read_pointer >= (TABLE_LENGTH_q8))
                vs->vibrato.read_pointer -= (TABLE_LENGTH_q8);
        }
        else
        {
            vs->vibrato.factor = float_to_q8(1.0f);
        }

        // ring modulation
        if (vs->tone.rm.freq_rate != 0)
        {
            wave2 = wave2 * vs->tone.rm.rm_gain >> 7; // Apply ring modulation gain
            wave1 = fp_mul(wave1, wave2);
        }

        // env
        if (vs->env.counter == 0)
        {
            switch (vs->env.state)
            {
            case ATTACK:
                if (vs->tone.env.attack_time != 0)
                {
                    vs->env.amplitude += 127 / vs->tone.env.attack_time;
                }
                else
                {
                    vs->env.amplitude = 127 * 128; // Immediate attack
                }
                if (vs->env.amplitude >= 127 * 128)
                {
                    vs->env.amplitude = 127 * 128;
                    vs->env.state = DECAY;
                }
                break;
            case DECAY:
                if (vs->tone.env.decay_time != 0)
                {
                    vs->env.amplitude -= 127 / vs->tone.env.decay_time;
                }
                else
                {
                    vs->env.amplitude = vs->tone.env.sustain_level * 128; // Immediate decay
                }
                if (vs->env.amplitude <= vs->tone.env.sustain_level * 128)
                {
                    vs->env.amplitude = vs->tone.env.sustain_level * 128;
                    vs->env.state = SUSTAIN;
                }
                break;
            case SUSTAIN:
                vs->env.amplitude -= vs->tone.env.sustain_rate;
                if (vs->env.amplitude <= 0)
                {
                    vs->env.amplitude = 0;
                    vs->env.state = RELEASE;
                }
                break;
            case RELEASE:
                if (vs->tone.env.release_time != 0)
                {
                    vs->env.amplitude -= 127 / vs->tone.env.release_time;
                }
                else
                {
                    vs->env.amplitude = 0; // Immediate release
                }
                if (vs->env.amplitude <= 0)
                {
                    vs->env.amplitude = 0;
                    vs->env.state = IDLE;
                }
                break;
            case IDLE:
                vs->env.amplitude = 0; // No output in idle state
                break;
            default:
                break;
            }
        }
        vs->env.counter++;
        if (vs->env.counter >= ENV_COUNTER_THRESHOLD)
        {
            vs->env.counter = 0;
        }

        fp_t adsr_gain = vs->env.amplitude * (FP_MAX >> 14); // Scale amplitude to fixed-point range
        wave1 = fp_mul(wave1, adsr_gain);
    }
    else
    {
        // PCM oscillator
        if (vs->pcm_initial_delay_counter > 0)
        {
            wave1 = 0; // output silence during initial delay
            vs->pcm_initial_delay_counter--;
        }
        else
        {
            if ((PCM_START_NOTE <= vs->note) && (vs->note <= PCM_END_NOTE))
            {
                int32_t pcm_note_offset = vs->note - PCM_START_NOTE; // Calculate PCM note offset
                const pcm_sample_t *sample = &pcm_samples[pcm_note_offset];

                if (sample->data != NULL && sample->length > 0 && vs->osc1.read_pointer < sample->length)
                {
                    wave1 = sample->data[vs->osc1.read_pointer];
                    vs->osc1.read_pointer++;
                    if (vs->osc1.read_pointer >= sample->length)
                    {
                        vs->env.state = IDLE;
                    }
                }
                else
                {
                    wave1 = 0;
                    vs->env.state = IDLE;
                    vs->osc1.read_pointer = 0;
                }
            }
            else
            {
                vs->env.state = IDLE;
                wave1 = 0;
                vs->osc1.read_pointer = 0;
            }
        }
    }

    // Velocity scaling
    wave1 = (fp_t)((int32_t)(wave1) * (int32_t)(vs->velocity) >> 7); // Scale wave1 by velocity (0 to 127)

    // Output gain
    vs->amplitude = (fp_t)((int32_t)(wave1) * (int32_t)(vs->tone.output_gain) >> 7);

    // // DC cut
    fp_t signal_for_dc_cut = vs->amplitude;
    vs->amplitude = fp_mul(HPF_ALPHA, (vs->dc_cut.prev_out + signal_for_dc_cut - vs->dc_cut.prev_in));
    vs->dc_cut.prev_in = signal_for_dc_cut;
    vs->dc_cut.prev_out = vs->amplitude;
}

void init_channel(int8_t ch_to_init)
{
    if (ch_to_init < 0 || ch_to_init >= MAX_CHANNEL_NUM)
    {
        // Initialize all channels if -1 or invalid channel is given
        for (int i = 0; i < MAX_CHANNEL_NUM; i++)
        {
            channel_state[i].tone = tone_gm[0];          // Initialize channel tone with GM default
            channel_state[i].volume = float_to_fp(0.1f); // Default volume
            channel_state[i].expression = 127;           // Default expression
            channel_state[i].pitch_bend.sensitivity = 2; // Default sensitivity
            channel_state[i].pitch_bend.range = 8192;    // Default range
            channel_state[i].mod.depth = 0;              // Default modulation depth
            channel_state[i].mod.freq = 64;              // Default modulation frequency
            channel_state[i].is_hold_on = false;         // Hold is off by default
            channel_state[i].pan = 64;                   // Default pan

            // Initialize NRPN/RPN
            channel_state[i].nrpn_rpn.param_type = PARAM_TYPE_NONE;
            channel_state[i].nrpn_rpn.nrpn_msb = 0;
            channel_state[i].nrpn_rpn.nrpn_lsb = 0;
            channel_state[i].nrpn_rpn.rpn_msb = 0;
            channel_state[i].nrpn_rpn.rpn_lsb = 0;
            channel_state[i].nrpn_rpn.data_msb = 0;
            channel_state[i].nrpn_rpn.data_lsb = 0;
            // channel_state[i].nrpn_rpn.has_lsb = false; // REMOVED
            channel_state[i].nrpn_rpn.data_msb_received = false;
            channel_state[i].nrpn_rpn.data_lsb_received = false;
        }
    }
    else
    {
        // Initialize only the specified channel
        channel_state[ch_to_init].tone = tone_gm[0];
        channel_state[ch_to_init].volume = float_to_fp(0.1f);
        channel_state[ch_to_init].expression = 127;
        channel_state[ch_to_init].pitch_bend.sensitivity = 2;
        channel_state[ch_to_init].pitch_bend.range = 8192;
        channel_state[ch_to_init].mod.depth = 0;
        channel_state[ch_to_init].mod.freq = 64;
        channel_state[ch_to_init].is_hold_on = false;
        channel_state[ch_to_init].pan = 64;

        channel_state[ch_to_init].nrpn_rpn.param_type = PARAM_TYPE_NONE;
        channel_state[ch_to_init].nrpn_rpn.nrpn_msb = 0;
        channel_state[ch_to_init].nrpn_rpn.nrpn_lsb = 0;
        channel_state[ch_to_init].nrpn_rpn.rpn_msb = 0;
        channel_state[ch_to_init].nrpn_rpn.rpn_lsb = 0;
        channel_state[ch_to_init].nrpn_rpn.data_msb = 0;
        channel_state[ch_to_init].nrpn_rpn.data_lsb = 0;
        // channel_state[ch_to_init].nrpn_rpn.has_lsb = false; // REMOVED
        channel_state[ch_to_init].nrpn_rpn.data_msb_received = false;
        channel_state[ch_to_init].nrpn_rpn.data_lsb_received = false;
    }
}

void init_reverb()
{
    // Initialize Reverb parameters (these are examples, tune them for desired sound)
    // Comb Filter 1
    reverb_state.comb_delay_times[0] = 1103;
    reverb_state.comb_feedback_gain[0] = float_to_fp(0.77f);
    // Comb Filter 2
    reverb_state.comb_delay_times[1] = 1277;
    reverb_state.comb_feedback_gain[1] = float_to_fp(0.71f);

    for (int i = 0; i < REVERB_COMB_FILTER_COUNT; i++)
    {
        reverb_state.comb_write_ptr[i] = 0;
        for (int j = 0; j < MAX_REVERB_COMB_DELAY_SAMPLES; j++)
        {
            reverb_state.comb_buffer_l[i][j] = 0;
            reverb_state.comb_buffer_r[i][j] = 0;
        }
    }

    // Allpass Filter 1
    reverb_state.allpass_delay_times[0] = 131;
    reverb_state.allpass_feedback_gain[0] = float_to_fp(0.6f);

    for (int i = 0; i < REVERB_ALLPASS_FILTER_COUNT; i++)
    {
        reverb_state.allpass_write_ptr[i] = 0;
        for (int j = 0; j < MAX_REVERB_ALLPASS_DELAY_SAMPLES; j++)
        {
            reverb_state.allpass_buffer_l[i][j] = 0;
            reverb_state.allpass_buffer_r[i][j] = 0;
        }
    }

    reverb_state.wet_level = float_to_fp(0.33f);
    reverb_state.dry_level = float_to_fp(1.0f) - reverb_state.wet_level;
}

void init_master()
{
    master_state.dc_cut.prev_out.u32 = 0;
    master_state.dc_cut.prev_in.u32 = 0;
    init_channel(-1); // Initialize all channels
    init_reverb();    // Initialize reverb
}

void note_on(midi_t *midi, voice_state_t *vs)
{
    int voice_to_use = -1;
    int idle_candidate = -1;
    int releasing_candidate = -1;
    int active_steal_candidate = -1;

    // Single pass to find the best candidate voice based on priority:
    // 1. IDLE voice (highest priority)
    // 2. RELEASING voice
    // 3. ACTIVE voice (steal - lowest priority)
    for (int i = 0; i < MAX_VOICE_NUM; i++)
    {
        if (vs[i].env.state == IDLE)
        {
            idle_candidate = i;
            break; // Found an IDLE voice, which is the highest priority. No need to search further.
        }
        // If no IDLE voice found yet in this iteration, check for other types.
        // Store the first encountered RELEASING voice.
        if (releasing_candidate == -1 && vs[i].env.state == RELEASE)
        {
            releasing_candidate = i;
        }
        // Store the first encountered ACTIVE (ATTACK, DECAY, SUSTAIN) voice for potential stealing.
        if (active_steal_candidate == -1 &&
            (vs[i].env.state == ATTACK || vs[i].env.state == DECAY || vs[i].env.state == SUSTAIN))
        {
            active_steal_candidate = i;
        }
    }

    // Determine which voice to use based on the candidates found, respecting priority.
    if (idle_candidate != -1)
    {
        voice_to_use = idle_candidate;
        gpio_put(PICO_DEFAULT_LED_PIN, 0);
    }
    else if (releasing_candidate != -1)
    {
        voice_to_use = releasing_candidate;
        gpio_put(PICO_DEFAULT_LED_PIN, 0);
    }
    else if (active_steal_candidate != -1)
    {
        voice_to_use = active_steal_candidate;
        gpio_put(PICO_DEFAULT_LED_PIN, 1); // Indicate voice stealing by turning LED on.
    }
    // If voice_to_use is still -1 at this point, it means no suitable voice was found
    // (e.g., MAX_VOICE_NUM could be 0, or all voices are in an unexpected state).

    if (voice_to_use != -1)
    {
        set_voice_state(&vs[voice_to_use], &channel_state[midi->ch].tone, midi->ch, midi->msg[1], midi->msg[2]);
    }
    else
    {
        // No voice found.
        gpio_put(PICO_DEFAULT_LED_PIN, 1);
    }
}

void note_off(midi_t *midi, voice_state_t *vs)
{
    // Search for the voice corresponding to the MIDI note
    for (int i = 0; i < MAX_VOICE_NUM; i++)
    {
        if (vs[i].env.state != IDLE && vs[i].assigned_channel_num == midi->ch && vs[i].note == midi->msg[1])
        {
            vs[i].env.is_note_off_received = true; // Set flag to indicate note off received
            if (channel_state[midi->ch].is_hold_on == false)
            {
                vs[i].env.state = RELEASE;
            }
        }
    }
}

stereo_t channel(voice_state_t *vs, int8_t ch)
{
    fp_t ch_gain = channel_state[ch].volume;                                              // Get channel volume
    ch_gain = fp_mul(ch_gain, float_to_fp((float)channel_state[ch].expression / 127.0f)); // Apply expression control more accurately
    fp_t mono_signal = 0;                                                                 // Accumulate mono signal for the channel
    stereo_t output;
    output.u32 = 0; // Initialize output
    int8_t pan_value = channel_state[ch].pan;

    // Process all voices for the specified channel and sum them into a mono signal
    for (int i = 0; i < MAX_VOICE_NUM; i++)
    {
        if (vs[i].assigned_channel_num == ch && vs[i].env.state != IDLE) // Only process active voices
        {
            voice(&vs[i]); // Process the voice
            mono_signal += fp_mul(vs[i].amplitude, ch_gain);
        }
    }

    // Pan control using precomputed table
    output.ch.left = fp_mul(mono_signal, pan_table[pan_value][0]);
    output.ch.right = fp_mul(mono_signal, pan_table[pan_value][1]);

    return output;
}

stereo_t reverb(stereo_t input)
{
    stereo_t wet_out;
    wet_out.u32 = 0;
    stereo_t final_out;

    fp_t comb_sum_l = 0;
    fp_t comb_sum_r = 0;

    // --- Parallel Comb Filters ---
    for (int i = 0; i < REVERB_COMB_FILTER_COUNT; i++)
    {
        uint16_t delay_len = reverb_state.comb_delay_times[i];
        uint16_t w_ptr = reverb_state.comb_write_ptr[i];
        uint16_t r_ptr = (w_ptr - delay_len + MAX_REVERB_COMB_DELAY_SAMPLES) % MAX_REVERB_COMB_DELAY_SAMPLES;

        fp_t delayed_l = reverb_state.comb_buffer_l[i][r_ptr];
        fp_t delayed_r = reverb_state.comb_buffer_r[i][r_ptr];

        // y[n] = x[n] + g * y[n-M]
        fp_t current_out_l = input.ch.left + fp_mul(reverb_state.comb_feedback_gain[i], delayed_l);
        fp_t current_out_r = input.ch.right + fp_mul(reverb_state.comb_feedback_gain[i], delayed_r);

        reverb_state.comb_buffer_l[i][w_ptr] = current_out_l;
        reverb_state.comb_buffer_r[i][w_ptr] = current_out_r;

        comb_sum_l += current_out_l;
        comb_sum_r += current_out_r;

        reverb_state.comb_write_ptr[i] = (w_ptr + 1) % MAX_REVERB_COMB_DELAY_SAMPLES;
    }

    // Average the output of comb filters
    wet_out.ch.left = comb_sum_l / REVERB_COMB_FILTER_COUNT;
    wet_out.ch.right = comb_sum_r / REVERB_COMB_FILTER_COUNT;

    // --- Series Allpass Filters ---
    // Output of comb stage is input to allpass stage
    fp_t allpass_stage_input_l = wet_out.ch.left;
    fp_t allpass_stage_input_r = wet_out.ch.right;

    for (int i = 0; i < REVERB_ALLPASS_FILTER_COUNT; i++)
    {
        uint16_t delay_len = reverb_state.allpass_delay_times[i];
        uint16_t w_ptr = reverb_state.allpass_write_ptr[i];
        uint16_t r_ptr = (w_ptr - delay_len + MAX_REVERB_ALLPASS_DELAY_SAMPLES) % MAX_REVERB_ALLPASS_DELAY_SAMPLES;
        fp_t g = reverb_state.allpass_feedback_gain[i];

        fp_t x_l = allpass_stage_input_l;
        fp_t x_r = allpass_stage_input_r;

        fp_t delayed_val_l = reverb_state.allpass_buffer_l[i][r_ptr]; // d[n-M]
        fp_t delayed_val_r = reverb_state.allpass_buffer_r[i][r_ptr]; // d[n-M]

        // Allpass: y(n) = d(n-M) + g*x(n)
        //          d(n) = x(n) - g*y(n)
        fp_t y_l = delayed_val_l + fp_mul(g, x_l);
        fp_t y_r = delayed_val_r + fp_mul(g, x_r);

        reverb_state.allpass_buffer_l[i][w_ptr] = x_l - fp_mul(g, y_l);
        reverb_state.allpass_buffer_r[i][w_ptr] = x_r - fp_mul(g, y_r);

        allpass_stage_input_l = y_l; // Output of this filter is input to next
        allpass_stage_input_r = y_r;

        reverb_state.allpass_write_ptr[i] = (w_ptr + 1) % MAX_REVERB_ALLPASS_DELAY_SAMPLES;
    }
    wet_out.ch.left = allpass_stage_input_l;
    wet_out.ch.right = allpass_stage_input_r;

    // --- Wet/Dry Mix ---
    final_out.ch.left = fp_mul(input.ch.left, reverb_state.dry_level) + fp_mul(wet_out.ch.left, reverb_state.wet_level);
    final_out.ch.right = fp_mul(input.ch.right, reverb_state.dry_level) + fp_mul(wet_out.ch.right, reverb_state.wet_level);

    return final_out;
}

stereo_t master(voice_state_t *vs)
{
    stereo_t mixed_signal, processed_signal;
    mixed_signal.u32 = 0; // Initialize to zero

    for (int i = 0; i < MAX_CHANNEL_NUM; i++)
    {
        stereo_t ch_signal = channel(vs, i);
        mixed_signal.ch.left += ch_signal.ch.left;
        mixed_signal.ch.right += ch_signal.ch.right;
    }

    // Apply Reverb
    processed_signal = reverb(mixed_signal);
    // processed_signal = mixed_signal;

    // Store signal before DC cut for prev_in
    stereo_t signal_before_dc_cut = processed_signal;

    // Apply DC cut filter to the master output
    // Corrected DC cut logic for stereo:
    processed_signal.ch.left = fp_mul(HPF_ALPHA, (processed_signal.ch.left + master_state.dc_cut.prev_out.ch.left - master_state.dc_cut.prev_in.ch.left));
    processed_signal.ch.right = fp_mul(HPF_ALPHA, (processed_signal.ch.right + master_state.dc_cut.prev_out.ch.right - master_state.dc_cut.prev_in.ch.right));

    // Update DC cut state
    master_state.dc_cut.prev_in.ch.left = signal_before_dc_cut.ch.left;
    master_state.dc_cut.prev_in.ch.right = signal_before_dc_cut.ch.right;
    master_state.dc_cut.prev_out.ch.left = processed_signal.ch.left;
    master_state.dc_cut.prev_out.ch.right = processed_signal.ch.right;

    return processed_signal;
}

// pitch_bend
void handle_pitch_bend(int8_t channel, uint8_t lsb, uint8_t msb)
{
    if (channel >= 0 && channel < MAX_CHANNEL_NUM)
    {
        uint16_t pitch_bend_value = (lsb | (msb << 7));
        channel_state[channel].pitch_bend.range = pitch_bend_value;

        q8_t new_pb_factor = get_interpolated_pitch_bend_factor(
            channel_state[channel].pitch_bend.sensitivity,
            pitch_bend_value);

        for (int i = 0; i < MAX_VOICE_NUM; i++)
        {
            if (voice_state[i].assigned_channel_num == channel && voice_state[i].env.state != IDLE)
            {
                voice_state[i].pb.factor = new_pb_factor;
            }
        }
    }
}

// program_change
void handle_program_change(int8_t channel, uint8_t program)
{
    if (channel >= 0 && channel < MAX_CHANNEL_NUM)
    {
        channel_state[channel].tone = tone_gm[program];
    }
}

// sustain_pedal
void handle_sustain_pedal(int8_t channel, uint8_t value)
{
    if (channel >= 0 && channel < MAX_CHANNEL_NUM)
    {
        if (value > 0) // Sustain on
        {
            channel_state[channel].is_hold_on = true;
        }
        else // Sustain off
        {
            channel_state[channel].is_hold_on = false;
            for (int i = 0; i < MAX_VOICE_NUM; i++)
            {
                if (voice_state[i].assigned_channel_num == channel &&
                    voice_state[i].env.state != IDLE &&
                    voice_state[i].env.is_note_off_received == true)
                {
                    voice_state[i].env.state = RELEASE; // Transition to RELEASE state
                }
            }
        }
    }
}

// volume_control
void handle_volume_control(int8_t channel, uint8_t value)
{
    if (channel >= 0 && channel < MAX_CHANNEL_NUM)
    {
        channel_state[channel].volume = float_to_fp((float)value / 127.0f * 0.2);
    }
}

// expression_control
void handle_expression_control(int8_t channel, uint8_t value)
{
    if (channel >= 0 && channel < MAX_CHANNEL_NUM)
    {
        channel_state[channel].expression = value;
    }
}

// pan_control
void handle_pan_control(int8_t channel, uint8_t value)
{
    if (channel >= 0 && channel < MAX_CHANNEL_NUM)
    {
        channel_state[channel].pan = value;
    }
}

// modulation_depth
void handle_modulation(int8_t channel, uint8_t value)
{
    if (channel >= 0 && channel < MAX_CHANNEL_NUM)
    {
        channel_state[channel].mod.depth = value; // Set channel modulation depth
        // Apply the new modulation depth to all active voices on this channel
        for (int i = 0; i < MAX_VOICE_NUM; i++)
        {
            if (voice_state[i].assigned_channel_num == channel && voice_state[i].env.state != IDLE)
            {
                voice_state[i].vibrato.depth = channel_state[channel].mod.depth;
            }
        }
    }
}

// vibrato_rate
void handle_Vibrato_Rate(int8_t channel, uint8_t value)
{
    if (channel >= 0 && channel < MAX_CHANNEL_NUM)
    {
        channel_state[channel].mod.freq = value; // Set channel modulation depth
    }
}

// reverb_send
void handle_reverb_send(uint8_t value)
{
    fp_t new_wet_level = float_to_fp((float)value / 127.0f * 0.6f); // Max 60% wet, adjust as needed
    if (new_wet_level > float_to_fp(1.0f))
        new_wet_level = float_to_fp(1.0f);
    if (new_wet_level < 0)
        new_wet_level = 0;

    reverb_state.wet_level = new_wet_level;
    reverb_state.dry_level = float_to_fp(1.0f) - new_wet_level;
    if (reverb_state.dry_level < 0)
        reverb_state.dry_level = 0;
}

// stop_event
void handle_stop_event()
{
    for (int i = 0; i < MAX_VOICE_NUM; i++)
    {
        if (voice_state[i].env.state != IDLE)
        {
            voice_state[i].env.state = RELEASE;
        }
    }
    gpio_put(PICO_DEFAULT_LED_PIN, 0);
}

// reset_event
void handle_reset_event()
{
    midi_buffer_init();
    init_master();
    for (int i = 0; i < MAX_VOICE_NUM; i++)
    {
        voice_state[i].env.state = IDLE;
    }
    gpio_put(PICO_DEFAULT_LED_PIN, 0);
}

// All Notes Off/Reset Controllers
void handle_all_notes_off(int8_t channel, uint8_t controller)
{
    // For All Notes Off type messages, affect all voices or specific channel voices
    for (int i = 0; i < MAX_VOICE_NUM; i++)
    {
        // Check if the CC message is channel-specific.
        // GM standard All Notes Off (0x7B) is channel-specific.
        // 0x78 (All Sound Off) is also channel-specific.
        if (voice_state[i].assigned_channel_num == channel)
        {
            voice_state[i].env.state = RELEASE; // More graceful than IDLE
        }
    }

    if (controller == 0x79)
    { // Reset All Controllers
        // Reset controllers for the specific channel
        if (channel >= 0 && channel < MAX_CHANNEL_NUM)
        {
            init_channel(channel); // Initialize only the specified channel
            for (int i = 0; i < MAX_VOICE_NUM; i++)
            {
                if (voice_state[i].assigned_channel_num == channel && voice_state[i].env.state != IDLE)
                {
                    voice_state[i].pb.factor = get_interpolated_pitch_bend_factor(
                        channel_state[channel].pitch_bend.sensitivity,
                        channel_state[channel].pitch_bend.range);
                    voice_state[i].vibrato.depth = 0; // Reset depth for active voices
                    voice_state[i].tone = tone_gm[0]; // Reset tone to default
                }
            }
        }
    }
}

// NRPN/RPN
void handle_rpn_nrpn(int8_t channel, bool is_nrpn, uint16_t parameter, uint16_t value)
{
    if (channel < 0 || channel >= MAX_CHANNEL_NUM)
        return;
    uint8_t p_MSB = (parameter >> 7) & 0x7F; // Get MSB
    uint8_t p_LSB = parameter & 0x7F;        // Get LSB
    uint8_t v_MSB = (value >> 7) & 0x7F;     // Get MSB of value
    uint8_t v_LSB = value & 0x7F;            // Get LSB of value

    // NRPN
    if (is_nrpn)
    {
        switch (p_MSB)
        {
        case 2:
            if (p_LSB == 0) // Set osc1 type
            {
                if (v_MSB < 5)
                {
                    channel_state[channel].tone.osc1.type = (wave_type_t)v_MSB;
                }
            }
            else if (p_LSB == 2) // Set output gain
            {
                if (v_MSB <= 127)
                {
                    channel_state[channel].tone.output_gain = v_MSB;
                }
            }
            break;
        case 3:
            if (p_LSB == 0) // Set rm frequency
            {
                if (v_MSB <= 127)
                {
                    channel_state[channel].tone.rm.freq_rate = v_MSB;
                }
            }
            else if (p_LSB == 1) // Set rm gain
            {
                if (v_MSB <= 127)
                {
                    channel_state[channel].tone.rm.rm_gain = v_MSB;
                }
            }
            break;
        case 6:
            if (p_LSB == 0) // Set sustin rate
            {
                if (v_MSB <= 127)
                {
                    channel_state[channel].tone.env.sustain_rate = v_MSB;
                }
            }
            else if (p_LSB == 1) // Set sustain level
            {
                if (v_MSB <= 127)
                {
                    channel_state[channel].tone.env.sustain_level = v_MSB;
                }
            }

        default:
            break;
        }
    }
    // RPN
    else
    {
        switch (parameter)
        {
        // RPN 0: pitch_bend_sensitivity
        case 0:
            if (value <= 24)
            { // max ±24 semitones
                channel_state[channel].pitch_bend.sensitivity = value;
                // Update all active voices on this channel
                for (int i = 0; i < MAX_VOICE_NUM; i++)
                {
                    if (voice_state[i].assigned_channel_num == channel && voice_state[i].env.state != IDLE)
                    {
                        voice_state[i].pb.factor = get_interpolated_pitch_bend_factor(
                            value,
                            channel_state[channel].pitch_bend.range);
                    }
                }
            }
            break;

        // RPN 1: master fine tuning
        case 1:
            break;

        // RPN 2: master coarse tuning
        case 2:
            break;

        default:
            break;
        }
    }
}

void handle_control_change(int8_t channel, uint8_t controller, uint8_t value)
{
    if (channel < 0 || channel >= MAX_CHANNEL_NUM)
        return;

    // NRPN/RPN
    switch (controller)
    {
    case 0x63: // NRPN MSB (99)
        channel_state[channel].nrpn_rpn.nrpn_msb = value;
        channel_state[channel].nrpn_rpn.param_type = PARAM_TYPE_NRPN;
        return;

    case 0x62: // NRPN LSB (98)
        channel_state[channel].nrpn_rpn.nrpn_lsb = value;
        channel_state[channel].nrpn_rpn.param_type = PARAM_TYPE_NRPN;
        return;

    case 0x65: // RPN MSB (101)
        channel_state[channel].nrpn_rpn.rpn_msb = value;
        channel_state[channel].nrpn_rpn.param_type = PARAM_TYPE_RPN;
        return;

    case 0x64: // RPN LSB (100)
        channel_state[channel].nrpn_rpn.rpn_lsb = value;
        channel_state[channel].nrpn_rpn.param_type = PARAM_TYPE_RPN;
        return;

    case 0x06: // data entry MSB (6)
        if (channel_state[channel].nrpn_rpn.param_type != PARAM_TYPE_NONE)
        {
            // save MSB value
            channel_state[channel].nrpn_rpn.data_msb = value;
            channel_state[channel].nrpn_rpn.data_msb_received = true;

            // If LSB has also been received, process the data
            if (channel_state[channel].nrpn_rpn.data_lsb_received)
            {
                bool is_nrpn = (channel_state[channel].nrpn_rpn.param_type == PARAM_TYPE_NRPN);
                uint16_t param_num;
                if (is_nrpn)
                {
                    param_num = (channel_state[channel].nrpn_rpn.nrpn_msb << 7) |
                                channel_state[channel].nrpn_rpn.nrpn_lsb;
                }
                else
                {
                    param_num = (channel_state[channel].nrpn_rpn.rpn_msb << 7) |
                                channel_state[channel].nrpn_rpn.rpn_lsb;
                }
                uint16_t data_value = (channel_state[channel].nrpn_rpn.data_msb << 7) | channel_state[channel].nrpn_rpn.data_lsb;
                handle_rpn_nrpn(channel, is_nrpn, param_num, data_value);
                // Reset flags
                channel_state[channel].nrpn_rpn.data_msb_received = false;
                channel_state[channel].nrpn_rpn.data_lsb_received = false;
            }
        }
        return;

    case 0x26: // data entry LSB (38)
        if (channel_state[channel].nrpn_rpn.param_type != PARAM_TYPE_NONE)
        {
            channel_state[channel].nrpn_rpn.data_lsb = value;
            channel_state[channel].nrpn_rpn.data_lsb_received = true;

            // If MSB has also been received, process the data
            if (channel_state[channel].nrpn_rpn.data_msb_received)
            {
                bool is_nrpn = (channel_state[channel].nrpn_rpn.param_type == PARAM_TYPE_NRPN);
                uint16_t param_num;
                if (is_nrpn)
                {
                    param_num = (channel_state[channel].nrpn_rpn.nrpn_msb << 7) |
                                channel_state[channel].nrpn_rpn.nrpn_lsb;
                }
                else
                {
                    param_num = (channel_state[channel].nrpn_rpn.rpn_msb << 7) |
                                channel_state[channel].nrpn_rpn.rpn_lsb;
                }
                uint16_t data_value = (channel_state[channel].nrpn_rpn.data_msb << 7) | channel_state[channel].nrpn_rpn.data_lsb;
                handle_rpn_nrpn(channel, is_nrpn, param_num, data_value);
                // Reset flags
                channel_state[channel].nrpn_rpn.data_msb_received = false;
                channel_state[channel].nrpn_rpn.data_lsb_received = false;
            }
            // If only LSB is received and MSB is not (e.g. 7bit controller)
            else if (channel_state[channel].nrpn_rpn.param_type != PARAM_TYPE_NONE && !channel_state[channel].nrpn_rpn.data_msb_received)
            {
                bool is_nrpn = (channel_state[channel].nrpn_rpn.param_type == PARAM_TYPE_NRPN);
                uint16_t param_num;
                if (is_nrpn)
                {
                    param_num = (channel_state[channel].nrpn_rpn.nrpn_msb << 7) |
                                channel_state[channel].nrpn_rpn.nrpn_lsb;
                }
                else
                {
                    param_num = (channel_state[channel].nrpn_rpn.rpn_msb << 7) |
                                channel_state[channel].nrpn_rpn.rpn_lsb;
                }
                // Process with MSB as 0, effectively using LSB as a 7-bit value
                uint16_t data_value = channel_state[channel].nrpn_rpn.data_lsb;
                handle_rpn_nrpn(channel, is_nrpn, param_num, data_value);
                // Reset LSB flag as it's processed
                channel_state[channel].nrpn_rpn.data_lsb_received = false;
            }
        }
        return;

    case 0x60: // data increment (96)
        if (channel_state[channel].nrpn_rpn.param_type != PARAM_TYPE_NONE)
        {
            // get current NRPN/RPN parameter type and number
            bool is_nrpn = (channel_state[channel].nrpn_rpn.param_type == PARAM_TYPE_NRPN);
            uint16_t param_num;

            if (is_nrpn)
            {
                param_num = (channel_state[channel].nrpn_rpn.nrpn_msb << 7) |
                            channel_state[channel].nrpn_rpn.nrpn_lsb;
            }
            else
            {
                param_num = (channel_state[channel].nrpn_rpn.rpn_msb << 7) |
                            channel_state[channel].nrpn_rpn.rpn_lsb;
            }

            // add 1 to the current value (implementation-dependent)
            uint16_t current_value = 0; // get current value logic
            uint16_t new_value = current_value + 1;

            // replace the current value with the new value
            handle_rpn_nrpn(channel, is_nrpn, param_num, new_value);
        }
        return;

    case 0x61: // data decrement (97)
        if (channel_state[channel].nrpn_rpn.param_type != PARAM_TYPE_NONE)
        {
            // get current NRPN/RPN parameter type and number
            bool is_nrpn = (channel_state[channel].nrpn_rpn.param_type == PARAM_TYPE_NRPN);
            uint16_t param_num;

            if (is_nrpn)
            {
                param_num = (channel_state[channel].nrpn_rpn.nrpn_msb << 7) |
                            channel_state[channel].nrpn_rpn.nrpn_lsb;
            }
            else
            {
                param_num = (channel_state[channel].nrpn_rpn.rpn_msb << 7) |
                            channel_state[channel].nrpn_rpn.rpn_lsb;
            }

            // subtract 1 from the current value (implementation-dependent)
            uint16_t current_value = 0; // get current value logic
            uint16_t new_value = current_value - 1;

            // replace the current value with the new value
            handle_rpn_nrpn(channel, is_nrpn, param_num, new_value);
        }
        return;

    case 0x7B: // All Notes Off (123)
    case 0x78: // All Sound Off (120)
    case 0x79: // Reset All Controllers (121)
        if (controller >= 0x78)
        {
            handle_all_notes_off(channel, controller);
            return;
        }
        break;
    }

    // Handle standard control changes
    if (controller == 0x07)
    { // Volume Control (7)
        handle_volume_control(channel, value);
    }
    else if (controller == 0x0B)
    { // Expression Control (11)
        handle_expression_control(channel, value);
    }
    else if (controller == 0x0A)
    { // Pan Control (10)
        handle_pan_control(channel, value);
    }
    else if (controller == 0x01)
    { // Modulation(1)
        handle_modulation(channel, value);
    }
    else if (controller == 0x40)
    { // Hold (Sustain Pedal) (64)
        handle_sustain_pedal(channel, value);
    }
    else if (controller == 0x42)
    { // Hold 2 (Sostenuto Pedal) (66)
      // Handle Sostenuto Pedal if needed
      // Currently, this is not implemented in the synthesizer
    }
    else if (controller == 0x4c) // Vibrato Rate
    {
        handle_Vibrato_Rate(channel, value);
    }
    else if (controller == 0x5B)
    { // Reverb Send Level (Effect 1 Depth) (91)
        handle_reverb_send(value);
    }
    else if (controller == 0x48)
    { // Set Rerease time
        if (channel >= 0 && channel < MAX_CHANNEL_NUM)
        {
            channel_state[channel].tone.env.release_time = value; // Set release rate
        }
    }
    else if (controller == 0x49)
    { // Set Attack time
        if (channel >= 0 && channel < MAX_CHANNEL_NUM)
        {
            channel_state[channel].tone.env.attack_time = value; // Set release rate
        }
    }
    else if (controller == 0x4B)
    { // Set Decay time
        if (channel >= 0 && channel < MAX_CHANNEL_NUM)
        {
            channel_state[channel].tone.env.decay_time = value; // Set release rate
        }
    }
    else
    {
        // Handle other control changes if needed
        // Currently, this is not implemented in the synthesizer
    }
}

void synthesizer_task()
{
    if (!midi_buffer_empty())
    {
        midi_t midi = midi_buffer_pop();

        switch (midi.event)
        {
        case NOTE_ON:
            note_on(&midi, voice_state);
            break;

        case NOTE_OFF:
            note_off(&midi, voice_state);
            break;

        case PITCH_BEND:
            handle_pitch_bend(midi.ch, midi.msg[1], midi.msg[2]);
            break;

        case PROGRAM_CHANGE:
            handle_program_change(midi.ch, midi.msg[1]);
            break;

        case CONTROL_CHANGE:
            handle_control_change(midi.ch, midi.msg[1], midi.msg[2]);
            break;

        case STOP:
            handle_stop_event();
            break;

        case RESET:
            handle_reset_event();
            break;

        default:
            // Unknown MIDI event
            break;
        }
    }
}