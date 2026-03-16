#ifndef POWER_CONTROL_H
#define POWER_CONTROL_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize the GPIOs for the CD74HC4067 multiplexer
void power_control_init(void);

// Set the power stage (0 to 11)
void power_control_set_stage(uint8_t stage);

#ifdef __cplusplus
}
#endif

#endif // POWER_CONTROL_H
