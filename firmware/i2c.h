#ifndef I2C_H
#define I2C_H

#include <stdint.h>
#include <errno.h>

#define I2C_DIR_WRITE 0
#define I2C_DIR_READ  1

#define I2C_ADDR(addr, direction) ((addr << 1) | (!!(direction)))
#define I2C_DIRECTION(addr) ((addr) & 1)

#define I2C_WRITE(addr) I2C_ADDR(addr, I2C_DIR_WRITE)
#define I2C_READ(addr) I2C_ADDR(addr, I2C_DIR_READ)

struct i2c_msg {
	uint8_t addr;
	uint8_t len;
	void *buf;

	/* Don't send a start condition, this is mostly useful
	 * when the data is split in several buffers. */
	uint8_t no_start : 1;
	/* Send a stop after this packet */
	uint8_t stop : 1;
};

typedef void (*i2c_complete_t)(int8_t status, void *ctx);

#if HAS_I2C

int8_t i2c_init(uint16_t sclk_rate_khz);

int8_t i2c_transfer(struct i2c_msg *msg, uint8_t num_msg,
		    i2c_complete_t complete, void *ctx);

int8_t i2c_transfer_sync(struct i2c_msg *msg, uint8_t num_msg);

int8_t i2c_read(uint8_t addr, void *buf, uint8_t len);

int8_t i2c_write(uint8_t addr, void *buf, uint8_t len);

#else /* HAS_I2C */

static inline int8_t i2c_init(uint16_t sclk_rate_khz)
{
	return -ENODEV;
}

static inline int8_t i2c_transfer(
	struct i2c_msg *msg, uint8_t num_msg,
	i2c_complete_t complete, void *ctx)
{
	return -ENODEV;
}

static inline int8_t i2c_transfer_sync(struct i2c_msg *msg, uint8_t num_msg)
{
	return -ENODEV;
}

static inline int8_t i2c_read(uint8_t addr, void *buf, uint8_t len)
{
	return -ENODEV;
}

static inline int8_t i2c_write(uint8_t addr, void *buf, uint8_t len)
{
	return -ENODEV;
}

#endif /* HAS_I2C */

#endif /* I2C_H */
