#include "midi.h"

#define MIDI_BUFFER_SIZE 512
midi_t midi_buffer[MIDI_BUFFER_SIZE];
uint32_t midi_buffer_head = 0;
uint32_t midi_buffer_tail = 0;

void midi_buffer_init()
{
    midi_buffer_head = 0;
    midi_buffer_tail = 0;
    for (int i = 0; i < MIDI_BUFFER_SIZE; i++)
    {
        midi_buffer[i].ch = 0;
        midi_buffer[i].event = 0;
        midi_buffer[i].msg[0] = 0;
        midi_buffer[i].msg[1] = 0;
        midi_buffer[i].msg[2] = 0;
    }
}

void midi_buffer_push(midi_t midi)
{
    midi_buffer[midi_buffer_head] = midi;
    midi_buffer_head = (midi_buffer_head + 1) % MIDI_BUFFER_SIZE;
}

midi_t midi_buffer_pop()
{
    midi_t midi = midi_buffer[midi_buffer_tail];
    midi_buffer_tail = (midi_buffer_tail + 1) % MIDI_BUFFER_SIZE;
    return midi;
}

bool midi_buffer_empty()
{
    return midi_buffer_head == midi_buffer_tail;
}

bool midi_event(uint8_t *msg)
{
    midi_t midi;

    midi.ch = msg[0] & 0xf;
    midi.event = msg[0] >= 0xf0 ? msg[0] : (msg[0] >> 4) & 0xf;
    midi.msg[0] = msg[0];
    midi.msg[1] = msg[1];
    midi.msg[2] = msg[2];
    midi_buffer_push(midi);
}

void midi_task()
{
    uint8_t msg[3];
    int n_data;

    while (n_data = tud_midi_n_available(0, 0))
    {
        msg[0] = 0;
        msg[1] = 0;
        msg[2] = 0;
        if (n_data = tud_midi_n_stream_read(0, 0, msg, 3))
        {
            midi_event(msg);
        }
    }
}