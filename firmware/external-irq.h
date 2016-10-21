/*
 * Copyright 2014  Alban Bedel (alban.bedel@avionic-design.de)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */
#ifndef EXTERNAL_IRQ_H
#define EXTERNAL_IRQ_H

/** \defgroup ExternalIRQ External IRQ
 *
 * The external_irq API provides a unified interface for external IRQs,
 * abstracting most differences between the simple external interrupts
 * and the pin change interrupts. It provides a simple API to register
 * callbacks for each IRQ and manipulate the IRQ mask.
 *
 * @{
 */

#include <avr/interrupt.h>

/** Number of pin external interrupts available */
#if defined(INT7_vect)
#define EXTERNAL_IRQ_EXT_COUNT 8
#elif defined(INT6_vect)
#define EXTERNAL_IRQ_EXT_COUNT 7
#elif defined(INT5_vect)
#define EXTERNAL_IRQ_EXT_COUNT 6
#elif defined(INT4_vect)
#define EXTERNAL_IRQ_EXT_COUNT 5
#elif defined(INT3_vect)
#define EXTERNAL_IRQ_EXT_COUNT 4
#elif defined(INT2_vect)
#define EXTERNAL_IRQ_EXT_COUNT 3
#elif defined(INT1_vect)
#define EXTERNAL_IRQ_EXT_COUNT 2
#elif defined(INT0_vect)
#define EXTERNAL_IRQ_EXT_COUNT 1
#else
#define EXTERNAL_IRQ_EXT_COUNT 0
#endif

/** Number of pin change interrupts available */
#if defined(PCINT3_vect)
#define EXTERNAL_IRQ_PC_COUNT 4
#elif defined(PCINT2_vect)
#define EXTERNAL_IRQ_PC_COUNT 3
#elif defined(PCINT1_vect)
#define EXTERNAL_IRQ_PC_COUNT 2
#elif defined(PCINT0_vect)
#define EXTERNAL_IRQ_PC_COUNT 1
#else
#define EXTERNAL_IRQ_PC_COUNT 0
#endif

/** \defgroup IrqType IRQ Types
 * @{
 */
#define IRQ_TYPE_NONE		0
#define IRQ_TYPE_EXT		1
#define IRQ_TYPE_PC		2
/**@}*/

/** \defgroup IrqTrigger IRQ Trigger
 * @{
 */
#define IRQ_TRIGGER_LOW_LEVEL		0
#define IRQ_TRIGGER_BOTH_EDGE		1
#define IRQ_TRIGGER_FALLING_EDGE	2
#define IRQ_TRIGGER_RAISING_EDGE	3
/**@}*/

/** Generate an IRQ ID from type and IRQ number
 *
 * \param type IRQ type, one of \ref IrqType
 * \param num IRQ number
 * \return The IRQ ID
 */
#define IRQ_ID(type, num)	(((type) & 3) << 6 | ((num) & 0x3F))

/** Generate an IRQ ID from a type name and IRQ number
 *
 * \param type Short IRQ type: EXT or PC
 * \param num IRQ number
 * \return The IRQ ID
 *
 * This is a convenience macro to make IRQ definitions nicer when
 * the IRQ type is a compile time constant.
 */
#define IRQ(type, num)		IRQ_ID(IRQ_TYPE_##type, num)

/** Get the type from an IRQ ID
 *
 * \param irq IRQ ID
 * \return IRQ type, one of \ref IrqType
 */
#define IRQ_TYPE(irq)		(((irq) >> 6) & 3)

/** Get the IRQ number from an IRQ ID
 *
 * \param irq IRQ ID
 * \return IRQ number
 */
#define IRQ_NUMBER(irq)		((irq) & 0x3F)

/** Function signature for external IRQ handlers
 *
 * \param pin_state State of the pin associate with the IRQ
 * \param context Context pointer passed to the setup function
 */
typedef void (*external_irq_handler_t)(uint8_t pin_state, void *context);

/** Get the GPIO associated with an IRQ
 *
 * \param irq ID of the IRQ to query
 * \return A GPIO ID on success, 0 on error.
 */
uint8_t external_irq_get_gpio(uint8_t irq);

/** Configure an external IRQ
 *
 * \param irq ID of the IRQ to configure
 * \param pull Enable the internal pull up
 * \param trigger Trigger type, one of \ref IrqTrigger
 * \param handler Function to handle IRQ events
 * \param context Context pointer that is passed to the handler
 * \return 0 on success, a negative value otherwise
 *
 * After calling this function the IRQ is masked, it must first be unmasked
 * using \ref external_irq_unmask to enable the events.
 */
int8_t external_irq_setup(uint8_t irq, uint8_t pull, uint8_t trigger,
			external_irq_handler_t handler, void *context);

/** Unmask an external IRQ
 *
 * \param irq ID of the IRQ to unmask
 * \return 0 on success, a negative value otherwise
 */
int8_t external_irq_unmask(uint8_t irq);

/** Mask an external IRQ
 *
 * \param irq ID of the IRQ to mask
 * \return 0 on success, a negative value otherwise
 */
int8_t external_irq_mask(uint8_t irq);

/**@}*/
#endif /* EXTERNAL_IRQ_H */
