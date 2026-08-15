/* The hardware natives every ESP32 moth host registers: GPIO, analog, PWM,
 * tone, random, I2C (single-register and bulk), UART, prefs (NVS), servo.
 *
 * One implementation, two hosts — the headless VM (vm/esp) and the display
 * firmware (ui/esp-s3) must offer the same names with the same semantics,
 * or the same program behaves differently depending on which firmware is
 * flashed. Before this file the display host offered almost none of them,
 * which meant a program with a screen could not read a sensor.
 */
#ifndef MOTH_HW_NATIVES_H
#define MOTH_HW_NATIVES_H

#include "driver/i2c_master.h"
#include "moth_vm.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Registers every hardware native on [vm]. Timing and print stay with the
 * host — the display host logs differently than the headless one. */
void moth_hw_register(moth_vm *vm);

/* Adopts an existing I2C master bus instead of creating one at i2cBegin.
 * The display board's sensors share the bus its panel driver already owns
 * (touch, IMU, RTC all hang off the same two pins), and two masters on one
 * pair of pins fight. After adoption, i2cBegin becomes a no-op and every
 * i2c* native talks through the adopted bus. */
void moth_hw_adopt_i2c_bus(i2c_master_bus_handle_t bus);

#ifdef __cplusplus
}
#endif
#endif /* MOTH_HW_NATIVES_H */
