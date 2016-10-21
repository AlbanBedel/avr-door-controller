/*
 * Copyright 2014  Alban Bedel (alban.bedel@avionic-design.de)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */
#ifndef TIMER_H
#define TIMER_H

/** \defgroup Timer Timer
 *
 * Timer provides an API for high level timers with millisecond precision
 * as well as a counter with microsecond precision.
 *
 * @{
 */
#include <stdint.h>

/** Type for the timer callbacks */
typedef void (*timer_cb_t)(void *context);

/** struct to hold a timer state */
struct timer {
    /** Pointer to the next timer in the pending list */
    struct timer * volatile  next;
    /** Callback to call when the timer expires */
    timer_cb_t callback;
    /** Context pointer for the callback */
    void *context;
    /** When the callback should be scheduled */
    uint16_t when;
    /** Set if the timer is in the pending list */
    uint8_t volatile pending : 1;
};

/** Return true if time a is after time b */
#define time_after(a, b) ((int16_t)((b) - (a)) < 0)
/** Return true if time a is before time b */
#define time_before(a, b) time_after(b, a)

/** Return true if time a is equal to time b or after. */
#define time_after_eq(a, b) ((int16_t)((a) - (b)) >= 0)
/** Return true if time a is equal to time b or before. */
#define time_before_eq(a, b) time_after_eq(b, a)


/** Initialize the timer subsystem */
void timers_init(void);

/** Prepare the timers before entering sleep mode
 *
 * This function will mask the timer interrupts if no timer
 * is pending. It should be used with interrupts disabled
 * right before entering sleep mode.
 */
void timers_sleep(void);

/** Restore the timers after wkaing up the device */
void timers_wakeup(void);

/** Get the current time in milliseconds
 *
 * \return The current time in milliseconds
 */
uint16_t timer_get_time(void);

/** Get the current time in microseconds
 *
 * \return The current time in microseconds
 */
uint16_t timer_get_time_us(void);

/** Init a timer object with the given callback and context
 *
 * \param t The timer to setup
 * \param callback The callback to call when the timer expires
 * \param context The context pointer to pass to the callback
 */
void timer_init(struct timer *t, timer_cb_t callback, void *context);

/** Schedule a timer to run at the given time
 *
 * \param t The timer to schedule
 * \param when The time when the timer should run, in milliseconds.
 */
void timer_schedule(struct timer *t, uint16_t when);

/** Schedule a timer to run after a delay
 *
 * \param t The timer to schedule
 * \param delay The delay, in milliseconds, after which should run.
 *
 * Note that because the timer is only 16 bits, only delay up to
 * 32 seconds make sense.
 */
void timer_schedule_in(struct timer *t, uint16_t delay);

/** Deschedule a timer
 *
 * \param t The timer to deschedule
 */
void timer_deschedule(struct timer *t);

/**@}*/
#endif /* TIMER_H */
