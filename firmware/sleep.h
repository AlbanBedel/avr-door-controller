#ifndef SLEEP_H
#define SLEEP_H

#include <avr/interrupt.h>

/* Internal helper for sleep_if */
void _sleep(void);

/* Override this function if you need to do some stuff
 * before going to sleep. The event-queue use this to
 * disable its life indicator GPIO */
void _sleep_prepare(void);

/* Override this function if you need to do some stuff
 * after waking from sleep.
 */
void _sleep_finish(void);

#define sleep_if(cond)			\
	do {				\
		cli();			\
		if (cond)		\
			_sleep();	\
		sei();			\
	} while (0)

#define sleep_while(cond)		\
	do {				\
		sleep_if(cond);		\
	} while (cond)

#define sleep_until(cond)		\
	sleep_while(!(cond))


#endif /* SLEEP_H */
