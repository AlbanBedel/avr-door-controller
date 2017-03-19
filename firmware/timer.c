/*
 * Copyright 2014  Alban Bedel (alban.bedel@avionic-design.de)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <stdlib.h>
#include <avr/interrupt.h>
#include "timer.h"

#if   F_CPU == 1000000
#define TIMER_SHIFT 0
#elif F_CPU == 2000000
#define TIMER_SHIFT 1
#elif F_CPU == 4000000
#define TIMER_SHIFT 2
#elif F_CPU == 8000000
#define TIMER_SHIFT 0
#elif F_CPU == 16000000
#define TIMER_SHIFT 1
#else
#error "Unsupported CPU frequency!"
#endif

#define TIMER_TICK (1000 << TIMER_SHIFT)

/** Queue of the pending timer, ordered by timeout */
static struct timer * volatile pending;
/** Current time in milliseconds */
static uint16_t volatile now;

#if TIMER_SHIFT > 0
/** Extension of the timer to deliver 16 bits nano seconds */
static uint8_t cnt_extension;
/** Interrupts mask */
#define TIMER_IRQ_MASK (_BV(OCIE1A) | _BV(OCIE1B) | _BV(TOIE1))
#else
#define TIMER_IRQ_MASK (_BV(OCIE1A) | _BV(OCIE1B))
#endif


/** Mask the timer interrupt */
static void timer_mask_irq(void)
{
	TIMSK1 &= ~(TIMER_IRQ_MASK);
}

/** Unmask the timer interrupt */
static void timer_unmask_irq(void)
{
	TIMSK1 |= TIMER_IRQ_MASK;
}

void timers_init(void)
{
	/* Setup the timer to run every micro second, this allow code that
	 * need it access to a high precision timer. However we only want
	 * to run the timers every 1 millisecond. We use the 3 comparators
	 * to match every 1000 ticks.
	 */
	OCR1A = TIMER_TICK;
	OCR1B = OCR1A + TIMER_TICK;
	TCCR1A = 0;
#if F_CPU < 8000000 /* Under 8MHz don't use a prescaler */
	TCCR1B = _BV(CS10);
#else /* Above 8MHz use a 1/8 prescaler */
	TCCR1B = _BV(CS11);
#endif
	/* Enable the timer interrupt */
	timer_unmask_irq();
}

void timers_sleep(void)
{
	if (!pending)
		timer_mask_irq();
}

void timers_wakeup(void)
{
	if (!pending)
		timer_unmask_irq();
}

/** Insert a timer in the pending queue */
static void timer_queue_pending(struct timer *timer)
{
	struct timer *t;

	/* Mark the timer as pending */
	timer->pending = 1;

	/* Add it at the head of the queue */
	if (pending == NULL || time_before(timer->when, pending->when)) {
		timer->next = pending;
		pending = timer;
		return;
	}

	/* Find the insert point for the new timer */
	for (t = pending;
	     t->next && time_before_eq(t->next->when, timer->when);
	     t = t->next)
		/* NOOP */;

	/* Insert the new timer in the list */
	timer->next = t->next;
	t->next = timer;
}

/** Remove a timer from the pending queue */
static void timer_dequeue_pending(struct timer *old)
{
	/* Check if the timer is pending */
	if (!old->pending)
		return;

	/* Detach from the pending list */
	if (pending == old) {
		pending = old->next;
	} else {
		struct timer *t;
		for (t = pending; t; t = t->next)
			if (t->next == old) {
				t->next = old->next;
				break;
			}
	}
	old->next = NULL;
	/* Clear the pending flag */
	old->pending = 0;
}

void timer_init(struct timer *t, timer_cb_t callback, void *context)
{
	if (t == NULL)
		return;

	t->next = NULL;
	t->when = 0;
	t->callback = callback;
	t->context = context;
}

void timer_schedule(struct timer *t, uint16_t when)
{
	if (t == NULL)
		return;

	timer_mask_irq();
	t->when = when;
	timer_dequeue_pending(t);
	timer_queue_pending(t);
	timer_unmask_irq();
}

void timer_schedule_in(struct timer *t, uint16_t delay)
{
	if (t == NULL)
		return;

	timer_mask_irq();
	t->when = now + delay;
	timer_dequeue_pending(t);
	timer_queue_pending(t);
	timer_unmask_irq();
}

void timer_deschedule(struct timer *t)
{
	if (t == NULL)
		return;

	timer_mask_irq();
	timer_dequeue_pending(t);
	timer_unmask_irq();
}

uint16_t timer_get_time(void)
{
	uint16_t n;

	timer_mask_irq();
	n = now;
	timer_unmask_irq();

	return n;
}

uint16_t timer_get_time_us(void)
{
	uint16_t n;

	/* Mask the IRQ to make sure the TEMP register is not trashed
	 * during the read */
	timer_mask_irq();
	n = TCNT1;
#if TIMER_SHIFT > 0
	n >>= TIMER_SHIFT;
	n |= ((uint16_t)cnt_extension) << (16 - TIMER_SHIFT);
#endif
	timer_unmask_irq();

	return n;
}

static void timers_tick(void)
{
	struct timer *t;

	now += 1;

	while ((t = pending) && time_before_eq(t->when, now)) {
		/* Detach the timer from the pending list */
		pending = t->next;
		t->next = NULL;
		t->pending = 0;

		/* Run the callback */
		t->callback(t->context);
	}
}

ISR(TIMER1_COMPA_vect)
{
	OCR1B = OCR1A + TIMER_TICK;
	timers_tick();
}

ISR(TIMER1_COMPB_vect)
{
	OCR1A = OCR1B + TIMER_TICK;
	timers_tick();
}

#if TIMER_SHIFT > 0
ISR(TIMER1_OVF_vect)
{
	cnt_extension++;
}
#endif
