/*
 * Copyright 2014  Alban Bedel (alban.bedel@avionic-design.de)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */
#ifndef GPIO_H
#define GPIO_H

/** \defgroup GPIO GPIO
 *
 * The GPIO API provides an abstraction of the pins access similar
 * to the Linux GPIO API.
 *
 * @{
 */

#include <stdint.h>

/** \defgroup GPIOPort GPIO Ports
 * @{
 */
#define GPIO_PORT_A		1
#define GPIO_PORT_B		2
#define GPIO_PORT_C		3
#define GPIO_PORT_D		4
#define GPIO_PORT_E		5
#define GPIO_PORT_F		6
/**@}*/

/** Generate a GPIO ID from port and pin number
 *
 * \param port GPIO port, one of \ref GPIOPort
 * \param pin Pin number
 * \return The GPIO ID
 */
#define GPIO_ID(port, pin)	(((port) & 0x1F) << 3 | ((pin) & 0x7))

/** Generate a GPIO ID from a port name and pin number
 *
 * \param port Short port name: A, B, C, etc
 * \param pin Pin number
 * \return The GPIO ID
 *
 * This is a convenience macro to make GPIO definitions nicer when
 * the port is a compile time constant.
 */
#define GPIO(port, pin)		GPIO_ID(GPIO_PORT_##port, pin)

/** Get the port from a GPIO ID
 *
 * \param gpio GPIO ID
 * \return Port, one of \ref GPIOPort
 */
#define GPIO_PORT(gpio)		(((gpio) >> 3) & 0x1F)

/** Get the pin from a GPIO ID
 *
 * \param gpio GPIO ID
 * \return Pin number
 */
#define GPIO_PIN(gpio)		((gpio) & 0x7)

/** Check if a GPIO is valid
 *
 * \param gpio GPIO ID
 * \return > 0 if the GPIO ID is valid, <= 0 otherwise
 */
int8_t gpio_is_valid(uint8_t gpio);

/** Configure a GPIO as input
 *
 * \param gpio GPIO ID
 * \param pull Enable the internal pull-up
 * \return 0 on success, a negative value otherwise
 */
int8_t gpio_direction_input(uint8_t gpio, uint8_t pull);

/** Configure a GPIO as output
 *
 * \param gpio GPIO ID
 * \param val Initial value
 * \return 0 on success, a negative value otherwise
 */
int8_t gpio_direction_output(uint8_t gpio, uint8_t val);

/** Get the current value of an input GPIO
 *
 * \param gpio GPIO ID
 * \return the pin state
 */
int8_t gpio_get_value(uint8_t gpio);

/** Set the value of an output GPIO
 *
 * \param gpio GPIO ID
 * \param state The new pin state
 */
void gpio_set_value(uint8_t gpio, uint8_t state);

/** Configure a GPIO as open collector
 *
 * Note that for this mode can only be used with an *external* pull-up,
 * using the on-chip pull is not supported! Furthermore to change the gpio
 * state \ref gpio_open_collector_set_value must be used instead of
 * \ref gpio_set_value.
 *
 * \param gpio GPIO ID
 * \param val Initial value
 * \return 0 on success, a negative value otherwise
 */
int8_t gpio_open_collector(uint8_t gpio, uint8_t val);

/** Set the value of an open collector GPIO
 *
 * \param gpio GPIO ID
 * \param state The new pin state
 */
void gpio_open_collector_set_value(uint8_t gpio, uint8_t state);

/**@}*/
#endif /* GPIO_H */
