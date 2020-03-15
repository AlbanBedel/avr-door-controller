#include <stdlib.h>
#include <stdint.h>
#include <time.h>

#include "rtc.h"

void rtc_tick(void)
{
	/* Tick the system timer */
	system_tick();
}

int8_t rtc_set_system_time(void)
{
	struct tm rtc_time = {};
	int8_t err;

	err = rtc_get(&rtc_time);
	if (!err) {
		rtc_mask();
		set_system_time(mk_gmtime(&rtc_time));
		rtc_unmask();
	}

	return err;
}
