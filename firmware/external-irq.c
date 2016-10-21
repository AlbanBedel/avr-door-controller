/*
 * Copyright 2014  Alban Bedel (alban.bedel@avionic-design.de)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <stdlib.h>
#include "external-irq.h"
#include "gpio.h"

/** Struct to hold an IRQ handler */
struct external_irq_handler {
	/** Handler function */
	external_irq_handler_t handler;
	/** Context passed to the handler */
	void *context;
	/** Trigger type, needed for edge trigger on pin change IRQs */
	uint8_t trigger;
};

/** Table of IRQ handler for external interrupts */
static struct external_irq_handler
external_irq_handler_ext[EXTERNAL_IRQ_EXT_COUNT];

/** Table of IRQ handler for pin change interrupts */
static struct external_irq_handler
external_irq_handler_pc[EXTERNAL_IRQ_PC_COUNT * 8];

/** State of each port that provides pin change interrupts */
static uint8_t external_irq_pc_state[EXTERNAL_IRQ_PC_COUNT];

/** List of the ports associated to each pin change interrupts.
 *
 * Each entry is a pointer to the PINx register of the port.
 */
extern volatile uint8_t * const external_irq_pc_pin[];

/** List of the GPIOs associated to each pin change interrupts. */
extern const uint8_t external_irq_gpio_pc[];

/** List of the GPIOs associated to each external interrupts. */
extern const uint8_t external_irq_gpio_ext[];

uint8_t external_irq_get_gpio(uint8_t irq)
{
	uint8_t num = IRQ_NUMBER(irq);

	switch(IRQ_TYPE(irq)) {
	case IRQ_TYPE_EXT:
		if (num < EXTERNAL_IRQ_EXT_COUNT)
			return external_irq_gpio_ext[num];
		break;
	case IRQ_TYPE_PC:
		if (num < EXTERNAL_IRQ_PC_COUNT * 8)
			return external_irq_gpio_pc[num];
		break;
	}
	return 0;
}

int8_t external_irq_get_pin_state(uint8_t irq)
{
	uint8_t gpio = external_irq_get_gpio(irq);
	if (gpio)
		return gpio_get_value(gpio);
	else
		return -1;
}

/** Setup an external IRQ */
static int8_t external_irq_setup_ext(uint8_t irq_num, uint8_t trigger)
{
	volatile uint8_t *reg = &EICRA + (irq_num >> 2);
	uint8_t shift = (irq_num & 3) << 1;
	*reg = (*reg & ~(3 << shift)) | ((trigger & 3) << shift);
	return 0;
}

/** Setup a pin change IRQ */
static int8_t external_irq_setup_pc(uint8_t irq_num, uint8_t trigger)
{
	/* Level trigger are not supported */
	if (trigger == IRQ_TRIGGER_LOW_LEVEL)
		return -1;
	PCICR |= _BV(irq_num >> 3);
	return 0;
}

int8_t external_irq_setup(uint8_t irq, uint8_t pull, uint8_t trigger,
			external_irq_handler_t handler, void *context)
{
	struct external_irq_handler *dispatch;
	uint8_t gpio, irq_num;
	int8_t err;

	/* Get the GPIO and configure it as input */
	gpio = external_irq_get_gpio(irq);
	if (!gpio)
		return -1;

	err = gpio_direction_input(gpio, pull);
	if (err)
		return err;

	/* Mask it to make sure it doesn't trigger */
	err = external_irq_mask(irq);
	if (err)
		return err;

	/* Note: The IRQ number has been validated in
	 *       external_irq_get_gpio(). No need to re-check here. */
	irq_num = IRQ_NUMBER(irq);

	/* Get the handler and setup the trigger */
	switch(IRQ_TYPE(irq)) {
	case IRQ_TYPE_EXT:
		dispatch = &external_irq_handler_ext[irq_num];
		err = external_irq_setup_ext(irq_num, trigger);
		break;
	case IRQ_TYPE_PC:
		dispatch = &external_irq_handler_pc[irq_num];
		err = external_irq_setup_pc(irq_num, trigger);
		break;
	default:
		err = -1;
		break;
	}
	if (err)
		return err;

	dispatch->handler = handler;
	dispatch->context = context;
	dispatch->trigger = trigger;

	return 0;
}

/** Unmask an external IRQ */
static int8_t external_irq_unmask_ext(uint8_t irq_num)
{
	EIMSK |= _BV(irq_num);
	return 0;
}

/** Mask an external IRQ */
static int8_t external_irq_mask_ext(uint8_t irq_num)
{
	EIMSK &= ~_BV(irq_num);
	return 0;
}

/** Get the mask register of a pin change IRQ.
 *
 * \param irq_num IRQ number
 * \return A pointer to the mask register
 */
static volatile uint8_t *external_irq_get_pc_mask_reg(uint8_t irq_num)
{
	irq_num >>= 3;
#ifdef PCMSK0
	if (irq_num == 0)
		return &PCMSK0;
#endif
#ifdef PCMSK1
	if (irq_num == 1)
		return &PCMSK1;
#endif
#ifdef PCMSK2
	if (irq_num == 2)
		return &PCMSK2;
#endif
#ifdef PCMSK3
	if (irq_num == 3)
		return &PCMSK3;
#endif
	return NULL;
}

/** Unmask a pin change IRQ */
static int8_t external_irq_unmask_pc(uint8_t irq_num)
{
	volatile uint8_t *reg = external_irq_get_pc_mask_reg(irq_num);
	uint8_t pin = irq_num & 7;
	uint8_t port = irq_num >> 3;

	if (!reg)
		return -1;

	/* Update the stored pin state before unmasking */
	if (external_irq_get_pin_state(IRQ(PC, irq_num)))
		external_irq_pc_state[port] |= _BV(pin);
	else
		external_irq_pc_state[port] &= ~_BV(pin);

	*reg |= _BV(pin);
	return 0;
}

/** Mask a pin change IRQ */
static int8_t external_irq_mask_pc(uint8_t irq_num)
{
	volatile uint8_t *reg = external_irq_get_pc_mask_reg(irq_num);
	if (!reg)
		return -1;

	*reg &= ~_BV(irq_num & 7);
	return 0;
}

int8_t external_irq_unmask(uint8_t irq)
{
	switch(IRQ_TYPE(irq)) {
	case IRQ_TYPE_EXT:
		return external_irq_unmask_ext(IRQ_NUMBER(irq));
	case IRQ_TYPE_PC:
		return external_irq_unmask_pc(IRQ_NUMBER(irq));
	default:
		return -1;
	}
}

int8_t external_irq_mask(uint8_t irq)
{
	switch(IRQ_TYPE(irq)) {
	case IRQ_TYPE_EXT:
		return external_irq_mask_ext(IRQ_NUMBER(irq));
	case IRQ_TYPE_PC:
		return external_irq_mask_pc(IRQ_NUMBER(irq));
	default:
		return -1;
	}
}

#if EXTERNAL_IRQ_EXT_COUNT > 0
/** Generic ISR for external interrupts */
static void external_irq_ext_handler(uint8_t num)
{
	struct external_irq_handler *irq = &external_irq_handler_ext[num];
	if (irq->handler)
		irq->handler(external_irq_get_pin_state(IRQ(EXT, num)),
			irq->context);
}

/** Helper to define an ISR for external interrupts */
#define EXT_INT_HANDLER(x)			\
	ISR(INT##x##_vect) {			\
		external_irq_ext_handler(x);	\
	}

#ifdef INT0_vect
EXT_INT_HANDLER(0)
#endif

#ifdef INT1_vect
EXT_INT_HANDLER(1)
#endif

#ifdef INT2_vect
EXT_INT_HANDLER(2)
#endif

#ifdef INT3_vect
EXT_INT_HANDLER(3)
#endif

#ifdef INT4_vect
EXT_INT_HANDLER(4)
#endif

#ifdef INT5_vect
EXT_INT_HANDLER(5)
#endif

#ifdef INT6_vect
EXT_INT_HANDLER(6)
#endif

#ifdef INT7_vect
EXT_INT_HANDLER(7)
#endif

#endif /* EXTERNAL_IRQ_EXT_COUNT > 0 */

#if EXTERNAL_IRQ_PC_COUNT > 0
/** Generic ISR for pin change interrupts */
static void external_irq_pc_handler(uint8_t port,
				volatile uint8_t *mask_reg)
{
	uint8_t last_state = external_irq_pc_state[port];
	uint8_t state = *external_irq_pc_pin[port];
	uint8_t mask = *mask_reg;
	uint8_t pin;

	for (pin = 0 ; pin < 8 ; pin += 1) {
		struct external_irq_handler *irq =
			&external_irq_handler_pc[(port << 3) + pin];
		/* Skip if the IRQ is masked or there is no handler */
		if (!(mask & _BV(pin)) || !irq->handler)
			continue;
		/* Skip if this pin state didn't change */
		if ((state & _BV(pin)) == (last_state & _BV(pin)))
			continue;
		/* Call the handler if there is a trigger */
		if (irq->trigger == IRQ_TRIGGER_BOTH_EDGE ||
			(irq->trigger == IRQ_TRIGGER_FALLING_EDGE &&
				(state & _BV(pin)) == 0) ||
			(irq->trigger == IRQ_TRIGGER_RAISING_EDGE &&
				(state & _BV(pin)) != 0))
			irq->handler((state >> pin) & 1, irq->context);
	}

	external_irq_pc_state[port] = state;
}

/** Helper to define an ISR for pin change interrupts */
#define PC_INT_HANDLER(x)						\
	ISR(PCINT##x##_vect) {						\
		external_irq_pc_handler(x, &PCMSK##x);			\
	}								\

#ifdef PCINT0_vect
PC_INT_HANDLER(0)
#endif

#ifdef PCINT1_vect
PC_INT_HANDLER(1)
#endif

#ifdef PCINT2_vect
PC_INT_HANDLER(2)
#endif

#ifdef PCINT3_vect
PC_INT_HANDLER(3)
#endif

#endif /* EXTERNAL_IRQ_PC_COUNT > 0 */
