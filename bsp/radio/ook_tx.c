// src/radio/ook_tx.c
#include "radio/ook_tx.h"

uint32_t ook_tx_clamp_us(uint32_t us) {
    return us > OOK_TX_MAX_US ? OOK_TX_MAX_US : us;
}

#ifndef HOST_TEST
#include "platform/board.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"

void ook_tx_send(const uint32_t *durs, uint32_t n, bool start_level) {
    // Take GDO0 from the capture PIO and drive it as a plain output.
    gpio_set_function(PIN_CC1101_GDO0, GPIO_FUNC_SIO);
    gpio_set_dir(PIN_CC1101_GDO0, GPIO_OUT);

    bool level = start_level;
    absolute_time_t t = get_absolute_time();
    for (uint32_t i = 0; i < n; i++) {
        gpio_put(PIN_CC1101_GDO0, level);
        t = delayed_by_us(t, ook_tx_clamp_us(durs[i]));   // accumulate -> no drift
        busy_wait_until(t);
        level = !level;
    }
    gpio_put(PIN_CC1101_GDO0, 0);   // carrier off
}
#endif
