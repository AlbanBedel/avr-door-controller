#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include "errno.h"
#include "utils.h"
#include "external-irq.h"
#include "i2c.h"
#include "rtc.h"

#define REG_SEC			0x00
#define REG_MIN			0x01
#define REG_HOUR		0x02
#define REG_WDAY		0x03
#define REG_MDAY		0x04
#define REG_MONTH		0x05
#define REG_YEAR		0x06
#define REG_ALARM1_SEC		0x07
#define REG_ALARM1_MIN		0x08
#define REG_ALARM1_HOUR		0x09
#define REG_ALARM1_DAY		0x0A
#define REG_ALARM2_MIN		0x0B
#define REG_ALARM2_HOUR		0x0C
#define REG_ALARM2_DAY		0x0D
#define REG_CONTROL		0x0E
#define REG_STATUS		0x0F
#define REG_AGING_OFFSET	0x10
#define REG_TEMP_MSB		0x11
#define REG_TEMP_LSB		0x12

#define YEAR_ZERO		(2000 - 1900)

#define BCD_READ(v, hmask) (((v) & 0x0F) + (((v) >> 4) & (hmask)) * 10)
#define BCD_WRITE(v, hmask) (((v) % 10) | ((((v) / 10) & (hmask)) << 4))

/* Should be defined at the board level */
static uint8_t ds3231_addr;
static uint8_t ds3231_irq;

static int8_t rtc_ds3231_op(
	uint8_t reg, uint8_t dir, uint8_t *values, uint8_t num_values)
{
	struct i2c_msg msgs[] = {
		{
			.addr = I2C_ADDR(ds3231_addr, I2C_DIR_WRITE),
			.len = 1,
			.buf = &reg,
		}, {
			.addr = I2C_ADDR(ds3231_addr, dir),
			.len = num_values,
			.buf = values,
			.no_start = (dir == I2C_DIR_WRITE),
		}
	};

	return i2c_transfer_sync(msgs, ARRAY_SIZE(msgs));
}

static int8_t rtc_ds3231_read(
	uint8_t reg, uint8_t *values, uint8_t num_values)
{
	return rtc_ds3231_op(reg, I2C_DIR_READ, values, num_values);
}

static int8_t rtc_ds3231_write(
	uint8_t reg, uint8_t *values, uint8_t num_values)
{
	return rtc_ds3231_op(reg, I2C_DIR_WRITE, values, num_values);
}

int8_t rtc_get(struct tm *tm)
{
	uint8_t status, values[REG_YEAR + 1 - REG_SEC];
	int8_t err;

	/* Check if the time is valid */
	err = rtc_ds3231_read(REG_STATUS, &status, sizeof(status));
	if (err)
		return err;
	/* Oscillator has been stopped, the time is not valid */
	if (status & BIT(7))
		return -EFAULT;

	/* Read the date and time registers */
	err = rtc_ds3231_read(REG_SEC, values, ARRAY_SIZE(values));
	if (err)
		return err;

	memset(tm, 0, sizeof(*tm));
	tm->tm_sec = BCD_READ(values[REG_SEC - REG_SEC], 0x7);
	tm->tm_min = BCD_READ(values[REG_MIN - REG_SEC], 0x7);
	/* Handle 12H mode */
	if (values[REG_HOUR - REG_SEC] & BIT(6)) {
		tm->tm_hour = BCD_READ(values[REG_HOUR - REG_SEC], 0x1);
		if (values[REG_HOUR - REG_SEC] & BIT(5))
			tm->tm_hour = (tm->tm_hour + 12) % 24;
	} else {
		tm->tm_hour = BCD_READ(values[REG_HOUR - REG_SEC], 0x3);
	}
	tm->tm_wday = values[REG_WDAY - REG_SEC] & 0x7;
	tm->tm_mday = BCD_READ(values[REG_MDAY - REG_SEC], 0x3);
	tm->tm_mon = BCD_READ(values[REG_MONTH - REG_SEC], 0x1);
	tm->tm_year = BCD_READ(values[REG_YEAR - REG_SEC], 0xF) + YEAR_ZERO;
	/* Handle century change */
	if (values[REG_MONTH - REG_SEC] & BIT(7))
		tm->tm_year += 100;

	return 0;
}

int8_t rtc_set(const struct tm *tm)
{
	uint8_t values[REG_YEAR + 1 - REG_SEC];
	uint8_t status, year, century;
	int8_t err;

	if (tm->tm_year < YEAR_ZERO || tm->tm_year > YEAR_ZERO + 199)
		return -ERANGE;

	year = tm->tm_year - YEAR_ZERO;
	if (year >= 100) {
		century = BIT(7);
		year -= 100;
	} else {
		century = 0;
	}

	values[REG_SEC - REG_SEC] = BCD_WRITE(tm->tm_sec, 0x7);
	values[REG_MIN - REG_SEC] = BCD_WRITE(tm->tm_min, 0x7);
	values[REG_HOUR - REG_SEC] = BCD_WRITE(tm->tm_hour, 0x3);
	values[REG_WDAY - REG_SEC] = tm->tm_wday & 0x7;
	values[REG_MDAY - REG_SEC] = BCD_WRITE(tm->tm_mday, 0x3);
	values[REG_MONTH - REG_SEC] = BCD_WRITE(tm->tm_mon, 0x1) | century;
	values[REG_YEAR - REG_SEC] = BCD_WRITE(year, 0xF);

	err = rtc_ds3231_write(REG_SEC, values, ARRAY_SIZE(values));
	if (err)
		return err;

	/* Clear the oscillator stop flag to mark the time as valid */
	err = rtc_ds3231_read(REG_STATUS, &status, 1);
	if (err)
		return err;

	if (status & BIT(7)) {
		status &= ~BIT(7);
		err = rtc_ds3231_write(REG_STATUS, &status, 1);
	}

	return err;
}

void rtc_mask(void)
{
	external_irq_mask(ds3231_irq);
}

void rtc_unmask(void)
{
	external_irq_unmask(ds3231_irq);
}

void rtc_ds3231_tick(uint8_t pin_state, void *context)
{
	rtc_tick();
}

int8_t rtc_ds3231_init(uint8_t addr, uint8_t irq)
{
	uint8_t val;
	int8_t err;

	ds3231_addr = addr;
	ds3231_irq = irq;

	/* Disable the 32kHz output */
	err = rtc_ds3231_read(REG_STATUS, &val, 1);
	if (err)
		return err;

	val &= ~BIT(3);
	err = rtc_ds3231_write(REG_STATUS, &val, 1);
	if (err)
		return err;

	/* Configure the IRQ on the square wave */
	err = external_irq_setup(irq, 0, IRQ_TRIGGER_FALLING_EDGE,
				 rtc_ds3231_tick, NULL);
	if (err)
		return err;

	/* Enable the 1Hz square wave */
	val = 0;
	err = rtc_ds3231_write(REG_CONTROL, &val, 1);
	if (err)
		return err;

	/* If possible load the time from the RTC */
	rtc_set_system_time();

	return 0;
}
