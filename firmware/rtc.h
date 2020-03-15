#ifndef RTC_H
#define RTC_H

#include <stdint.h>

struct tm;

/** Init a DS3231 RTC over I2C */
int8_t rtc_ds3231_init(uint8_t addr, uint8_t irq);

/** Read the time from the RTC */
int8_t rtc_get(struct tm *tm);

/** Set the time in the RTC */
int8_t rtc_set(const struct tm *tm);

/** Set the system time from the RTC */
int8_t rtc_set_system_time(void);

/** Let the clock tick, must be called every second */
void rtc_tick(void);

/** Mask the clock tick */
void rtc_mask(void);

/** Unmask the clock tick */
void rtc_unmask(void);

#endif /* RTC_H */
