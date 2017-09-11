/*
 * Copyright 2017  Alban Bedel (albeu@free.fr)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */
#ifndef BUTTON_H
#define BUTTON_H 1

#include "timer.h"

typedef void (*button_cb_t)(uint8_t state, void *context);

struct button {
	button_cb_t callback;
	void *context;

	struct timer debounce;
	uint8_t debounce_delay;

	uint8_t inited : 1;
	uint8_t low_active : 1;
	uint8_t next_state : 1;
	uint8_t state : 1;
};

int8_t button_init(struct button *btn, uint8_t gpio,
		   uint8_t pull, uint8_t debounce_delay,
		   button_cb_t callback, void *context);

#endif /* BUTTON_H */
