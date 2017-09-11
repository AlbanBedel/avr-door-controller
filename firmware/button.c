/*
 * Copyright 2017  Alban Bedel (albeu@free.fr)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <string.h>
#include <errno.h>
#include "button.h"
#include "external-irq.h"
#include "timer.h"
#include "gpio.h"

static void button_timeout(void *context)
{
	struct button *btn = context;

	if (!btn->inited) {
		btn->inited = 1;
		btn->state = !btn->next_state;
	}

	if (btn->state != btn->next_state) {
		btn->state = btn->next_state;
		btn->callback(btn->state, btn->context);
	}
}

static void button_isr(uint8_t pin_state, void *context)
{
	struct button *btn = context;

	timer_schedule_in(&btn->debounce, btn->debounce_delay);
	btn->next_state = pin_state ^ btn->low_active;
}

int8_t button_init(struct button *btn, uint8_t gpio,
		   uint8_t pull, uint8_t debounce_delay,
		   button_cb_t callback, void *context)
{
	uint8_t irq;
	int8_t err;

	irq = external_irq_from_gpio(gpio);

	if (!btn || !irq || !callback)
		return -EINVAL;


	memset(btn, 0, sizeof(*btn));

	btn->callback = callback;
	btn->context = context;

	timer_init(&btn->debounce, button_timeout, btn);
	btn->debounce_delay = debounce_delay;

	err = external_irq_setup(
		irq, pull, IRQ_TRIGGER_BOTH_EDGE, button_isr, btn);
	if (err)
		return err;

	/* Setup the initial state */
	btn->low_active = GPIO_POLARITY(gpio);
	btn->state = gpio_get_value(gpio);
	btn->next_state = btn->state;

	/* Send an initial event */
	button_isr(btn->state ^ btn->low_active, btn);

	return external_irq_unmask(irq);
}
