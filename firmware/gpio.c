/*
 * Copyright 2014  Alban Bedel (alban.bedel@avionic-design.de)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <stdlib.h>
#include <avr/io.h>
#include "gpio.h"

/** Struct to access the GPIO registers */
struct gpio_regs {
	volatile uint8_t pin;
	volatile uint8_t ddr;
	volatile uint8_t port;
};

/** Helper macro to get the GPIO register of a port */
#define GPIO_REGS(n)	((struct gpio_regs *)&PIN ## n)

/** Get the register to use for a GPIO
 *
 * \param gpio GPIO ID
 * \return A pointer to the registers, or NULL on error.
 */
static struct gpio_regs *gpio_get_regs(uint8_t gpio)
{
	switch(GPIO_PORT(gpio)) {
	case GPIO_PORT_A:
#ifdef PORTA
		return GPIO_REGS(A);
#else
		return NULL;
#endif
	case GPIO_PORT_B:
#ifdef PORTB
		return GPIO_REGS(B);
#else
		return NULL;
#endif
	case GPIO_PORT_C:
#ifdef PORTC
		return GPIO_REGS(C);
#else
		return NULL;
#endif
	case GPIO_PORT_D:
#ifdef PORTD
		return GPIO_REGS(D);
#else
		return NULL;
#endif
	case GPIO_PORT_E:
#ifdef PORTE
		return GPIO_REGS(E);
#else
		return NULL;
#endif
	case GPIO_PORT_F:
#ifdef PORTF
		return GPIO_REGS(F);
#else
		return NULL;
#endif
	default:
		return NULL;
	}
}

int8_t gpio_is_valid(uint8_t gpio)
{
	return gpio_get_regs(gpio) != NULL;
}

int8_t gpio_direction_input(uint8_t gpio, uint8_t pull)
{
	struct gpio_regs *regs;
	uint8_t mask;

	regs = gpio_get_regs(gpio);
	if (regs == NULL)
		return -1;

	mask = 1 << GPIO_PIN(gpio);
	if (pull)
		regs->port |= mask;
	else
		regs->port &= ~mask;

	regs->ddr &= ~mask;
	return 0;
}

int8_t gpio_direction_output(uint8_t gpio, uint8_t val)
{
	struct gpio_regs *regs;
	uint8_t mask;

	regs = gpio_get_regs(gpio);
	if (regs == NULL)
		return -1;

	mask = 1 << GPIO_PIN(gpio);
	if ((!!val) ^ GPIO_POLARITY(gpio))
		regs->port |= mask;
	else
		regs->port &= ~mask;

	regs->ddr |= mask;
	return 0;
}

int8_t gpio_get_value(uint8_t gpio)
{
	struct gpio_regs *regs;

	regs = gpio_get_regs(gpio);
	if (regs == NULL)
		return -1;

	return ((regs->pin >> GPIO_PIN(gpio)) & 1) ^ GPIO_POLARITY(gpio);
}

void gpio_set_value(uint8_t gpio, uint8_t state)
{
	struct gpio_regs *regs;

	regs = gpio_get_regs(gpio);
	if (regs == NULL)
		return;

	if ((!!state) ^ GPIO_POLARITY(gpio))
		regs->port |= 1 << GPIO_PIN(gpio);
	else
		regs->port &= ~(1 << GPIO_PIN(gpio));
}

int8_t gpio_open_collector(uint8_t gpio, uint8_t val)
{
	struct gpio_regs *regs;
	uint8_t mask;

	regs = gpio_get_regs(gpio);
	if (regs == NULL)
		return -1;

	mask = 1 << GPIO_PIN(gpio);
	regs->port &= ~mask;
	if ((!!val) ^ GPIO_POLARITY(gpio))
		regs->ddr &= ~mask;
	else
		regs->ddr |= mask;

	return 0;
}

void gpio_open_collector_set_value(uint8_t gpio, uint8_t state)
{
	struct gpio_regs *regs;

	regs = gpio_get_regs(gpio);
	if (regs == NULL)
		return;

	if ((!!state) ^ GPIO_POLARITY(gpio))
		regs->ddr &= ~(1 << GPIO_PIN(gpio));
	else
		regs->ddr |= 1 << GPIO_PIN(gpio);
}
