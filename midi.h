#ifndef MIDI_H
#define MIDI_H

#include "pico/stdlib.h"
#include "bsp/board.h"
#include "tusb.h"
#include <stdint.h>

typedef enum
{
    NOTE_OFF = 0x8,
    NOTE_ON = 0x9,
    POLY_PRESSURE = 0xa,
    CONTROL_CHANGE = 0xb,
    PROGRAM_CHANGE = 0xc,
    CHANNEL_PRESSURE = 0xd,
    PITCH_BEND = 0xe,
} midi_event_t;

typedef enum
{
    SYS_EX_START = 0xf0,
    MTC = 0xf1,
    SONG_POSITION = 0xf2,
    SONG_SELECT = 0xf3,
    TUNE_REQUEST = 0xf6,
    SYS_EX_END = 0xf7,
    TIMING_CLOCK = 0xf8,
    START = 0xfa,
    CONTINUE = 0xfb,
    STOP = 0xfc,
    ACTIVE_SENSING = 0xfe,
    RESET = 0xff,
} midi_system_msg_t;

typedef struct
{
    uint8_t ch;
    uint8_t event;
    uint8_t msg[3];
} midi_t;

void midi_buffer_init();
void midi_buffer_push(midi_t midi);
midi_t midi_buffer_pop();
bool midi_buffer_empty();

bool midi_event(uint8_t *msg);
void midi_task();

#endif