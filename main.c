#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/uart.h"
#include "hardware/vreg.h"
#include <math.h>

#include "audio_dac.pio.h"
#include "synthesizer.h"
#include "midi.h"

#define PLL_SYS_KHZ 380 * 1000

// UART(for debugging)
#define UART_ID uart0
#define BAUD_RATE 115200
#define UART_TX_PIN 12
#define UART_RX_PIN 13

bool timer_callback(struct repeating_timer *t)
{
    gpio_put(15, 1); // measure processing time
    stereo_t data = master(voice_state);
    pio_sm_put_blocking(pio0, 0, data.u32);
    gpio_put(15, 0); // end of processing time measurement
    return true;
}

int main()
{
    set_sys_clock_48mhz();
    sleep_ms(2);

    stdio_init_all();
    board_init();
    tusb_init();

    vreg_set_voltage(VREG_VOLTAGE_1_30);
    sleep_ms(2);
    set_sys_clock_khz(PLL_SYS_KHZ, true);
    sleep_ms(2);

    uart_init(UART_ID, BAUD_RATE);
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);

    // GPIO 15 for measuring processing time
    gpio_init(15);
    gpio_set_dir(15, GPIO_OUT);
    gpio_put(15, 0);

    // LED initialization
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    gpio_put(PICO_DEFAULT_LED_PIN, 0);

    // PIO initialization
    PIO pio = pio0;
    uint offset = pio_add_program(pio, &LSBJ16bit_program);
    uint sm = 0;
    uint clkdiv = 50;
    LSBJ16bit_program_init(pio, sm, offset, clkdiv, 0);

    // Repeating timer for audio processing
    // Timer interval is set to 25 microseconds (40 kHz sample rate)
    struct repeating_timer timer;
    add_repeating_timer_us(-25, &timer_callback, NULL, &timer);

    // Initialize synthesizer and MIDI buffer
    midi_buffer_init();
    init_master();

    printf("hello\n");
    while (true)
    {
        tud_task();
        midi_task();
        synthesizer_task();
    }
}
