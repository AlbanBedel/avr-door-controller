#include <stdlib.h>
#include <stdint.h>
#include <avr/interrupt.h>
#include <util/twi.h>
#include "errno.h"
#include "utils.h"
#include "timer.h"
#include "i2c.h"
#include "completion.h"

#define I2C_TIMEOUT 100

struct i2c_bus {
	/* Current transfer list */
	struct i2c_msg *xfer;
	/* Number of messages in the list */
	uint8_t xfer_count;
	/* Current position in the list */
	uint8_t xfer_pos;
	/* Position in the current message buffer */
	uint8_t xfer_buf_pos;

	/* Timer to timeout operations */
	struct timer timeout;

	/* Completion function for the transfer */
	i2c_complete_t complete;
	void *complete_ctx;
};

static struct i2c_bus i2c_bus;

static void i2c_on_timeout(void *context)
{
	struct i2c_bus *bus = context;

	TWCR = BIT(TWINT) | BIT(TWEN) | BIT(TWIE) | BIT(TWSTO);
	if (bus->complete)
		bus->complete(-EINTR, bus->complete_ctx);
}

int8_t i2c_init(uint16_t sclk_rate_khz)
{
	struct i2c_bus *bus = &i2c_bus;
	uint16_t cpu_rate_khz = F_CPU / 1000;
	uint16_t divider = cpu_rate_khz / sclk_rate_khz;
	uint16_t twbr;
	uint8_t twps;

	/* Configure the clock divider used to generate SCLK.
	 * The datasheet give us the following formular:
	 *
	 * sclk_rate = f_cpu / (16 + 2 * TWBR * prescaler)
	 * where prescaler = 1 << (2 * TWPS)
	 * with TWPS in the range [0-3]
	 *
	 * This can be re-written as:
	 *
	 * sclk_rate = f_cpu / (16 + (TWBR << (2 * TWPS + 1)))
	 *
	 * So we just need to find the smallest possible shift value
	 * that allow us to have all the high bits we need in TWBR.
	 */

	/* Clamp the divider to the valid range and remove
	 * the + 16 offset. */
	if (divider < 16) {
		twbr = 0;
	} else {
		twbr = divider - 16;
		/* TWBR can be up to 15 bits */
		if (twbr > 0x7FFF)
			twbr = 0x7FFF;
	}

	/* Find the smallest shift needed */
	for (twps = 0; twps < 3; twps++) {
		uint8_t shift = 2 * twps + 1;

		/* Stop as soon as all high bits are zero */
		if (!((twbr >> shift) & 0xFF00))
			break;
	}

	TWBR = twbr >> (2 * twps + 1);
	TWSR = twps;

	timer_init(&bus->timeout, i2c_on_timeout, bus);

	return 0;
}

int8_t i2c_transfer(struct i2c_msg *msg, uint8_t num_msg,
		    i2c_complete_t complete, void *ctx)
{
	struct i2c_bus *bus = &i2c_bus;

	if (msg == NULL || num_msg < 1)
		return -EINVAL;

	if (bus->xfer)
		return -EBUSY;

	bus->xfer = msg;
	bus->xfer_count = num_msg;
	bus->xfer_pos = 0;
	bus->xfer_buf_pos = 0;
	bus->complete = complete;
	bus->complete_ctx = ctx;

	/* Launch the start condition */
	TWCR = BIT(TWINT) | BIT(TWSTA) | BIT(TWEN) | BIT(TWIE);
	timer_schedule_in(&bus->timeout, I2C_TIMEOUT);

	return 0;
}

static void i2c_transfer_complete(int8_t status, void *ctx)
{
	struct completion *comp = ctx;

	comp->done = status;
}

int8_t i2c_transfer_sync(struct i2c_msg *msg, uint8_t num_msg)
{
	struct completion comp = {};
	int8_t err;

	err = i2c_transfer(msg, num_msg, i2c_transfer_complete, &comp);
	if (err)
		return err;

	completion_wait(&comp);

	err = comp.done;
	return err < 0 ? err : 0;
}

int8_t i2c_transfer_msg(uint8_t addr, void *buf, uint8_t len)
{
	struct i2c_msg msg = {
		.addr = addr,
		.len = len,
		.buf = buf,
		.stop = 1,
	};

	return i2c_transfer_sync(&msg, 1);
}

int8_t i2c_read(uint8_t addr, void *buf, uint8_t len)
{
	return i2c_transfer_msg(I2C_ADDR(addr, I2C_DIR_READ), buf, len);
}

int8_t i2c_write(uint8_t addr, void *buf, uint8_t len)
{
	return i2c_transfer_msg(I2C_ADDR(addr, I2C_DIR_WRITE), buf, len);
}

ISR(TWI_vect)
{
	struct i2c_bus *bus = &i2c_bus;
	struct i2c_msg *msg = &bus->xfer[bus->xfer_pos];
	uint8_t twcr = BIT(TWINT) | BIT(TWEN) | BIT(TWIE);
	struct i2c_msg *next_msg = NULL;
	static uint8_t status;
	int8_t err = 0;

	status = TW_STATUS;
	if (bus->xfer_pos + 1 < bus->xfer_count)
		next_msg = &bus->xfer[bus->xfer_pos + 1];

	switch (status) {
	case TW_START:
	case TW_REP_START:
		/*  -> send the address */
		TWDR = msg->addr;
		break;

	case TW_MR_SLA_ACK:
		/* Send a ACK if more than one byte is need */
		if (msg->len > 1 || (next_msg && next_msg->no_start))
			twcr |= BIT(TWEA);
		/* Otherwise send a NACK */
		break;

	case TW_MR_DATA_ACK:
	case TW_MR_DATA_NACK:
		/* -> receive the data */
		((uint8_t *)msg->buf)[bus->xfer_buf_pos++] = TWDR;
		/* Send an ACK if there is more data */
		if (bus->xfer_buf_pos + 1 < msg->len ||
		    (next_msg && next_msg->no_start))
			twcr |= BIT(TWEA);
		/* Otherwise send a NACK */

	case TW_MT_SLA_ACK:
	case TW_MT_DATA_ACK:
		/* no more data -> go to the next message */
		if (bus->xfer_buf_pos >= msg->len) {
			/* Send a stop after this message */
			if (msg->stop)
				twcr |= BIT(TWSTO);
			/* Take the next message */
			bus->xfer_buf_pos = 0;
			bus->xfer_pos++;
			msg = next_msg;

			/* last message -> finish */
			if (msg == NULL) {
				err = bus->xfer_pos;
				break;
			}

			/* send a re-START */
			if (!msg->no_start) {
				twcr |= BIT(TWSTA);
				break;
			}
			/* or continue with the new message */
		}

		/* send the data */
		if (status < TW_MR_DATA_ACK)
			TWDR = ((uint8_t *)msg->buf)[bus->xfer_buf_pos++];
		break;

	case TW_MR_ARB_LOST:
		/* -> send a re-START */
		TWCR = BIT(TWSTA);
		break;

	case TW_MT_SLA_NACK:
	case TW_MR_SLA_NACK:
		/* -> return a no device error */
		err = -ENODEV;
		break;

	case TW_MT_DATA_NACK:
	case TW_BUS_ERROR:
		/* -> return an IO error */
		err = -EIO;
		break;
	}

	/* send a stop if we are finishing */
	if (err) {
		timer_deschedule(&bus->timeout);
		twcr |= BIT(TWSTO);
	} else {
		timer_schedule_in(&bus->timeout, I2C_TIMEOUT);
	}

	/* Move the state machine to the next step */
	TWCR = twcr;

	/* Transfer finished */
	if (err) {
		bus->xfer = NULL;
		if (bus->complete)
			bus->complete(err, bus->complete_ctx);
	}
}
