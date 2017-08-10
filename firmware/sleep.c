#include <avr/sleep.h>
#include "sleep.h"

void _sleep_prepare(void) __attribute__ ((weak));
void _sleep_prepare(void)
{}

void _sleep_finish(void) __attribute__ ((weak));
void _sleep_finish(void)
{}

void _sleep(void)
{
	_sleep_prepare();
	sleep_enable();
	sei();
	sleep_cpu();
	sleep_disable();
	_sleep_finish();
}
