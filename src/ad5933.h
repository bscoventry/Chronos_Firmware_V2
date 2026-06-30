#ifndef AD5933_H
#define AD5933_H

#include <stdint.h>

/** 7-bit I2C address (0x0D). */
#define AD5933_I2C_ADDR_7BIT  0x0Du

#define AD5933_REG_CONTROL  0x80u
#define AD5933_REG_STATUS    0x8Bu

/**
 * Phase A: verify I2C + read control (16-bit) and status (8-bit) registers.
 * Switch remains off unless @p connect_switch is true.
 *
 * @return 0 on successful reads, negative errno otherwise.
 */
int ad5933_phase_a_probe(bool connect_switch);

#endif /* AD5933_H */
