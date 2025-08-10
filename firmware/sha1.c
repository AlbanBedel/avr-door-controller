#include <stdlib.h>
#include <string.h>

#include "sha1.h"

#define SHA1_TRAILER_SIZE 8

#define HMAC_INNER_PADDING 0x36
#define HMAC_OUTER_PADDING 0x5c

#define SHA1_K0 0x5a827999
#define SHA1_K1 0x6ed9eba1
#define SHA1_K2 0x8f1bbcdc
#define SHA1_K3 0xca62c1d6

void sha1_init(struct sha1_context *ctx)
{
	ctx->intermediate[0] = 0x67452301;
	ctx->intermediate[1] = 0xefcdab89;
	ctx->intermediate[2] = 0x98badcfe;
	ctx->intermediate[3] = 0x10325476;
	ctx->intermediate[4] = 0xc3d2e1f0;
	ctx->block_count = 0;
	ctx->block_pos = 0;
}

static uint32_t sha1_circular_shift(uint32_t word, uint8_t shift)
{
	return (word << shift) | (word >> (32 - shift));
}

/* Process a block loaded in the work array */
static void sha1_process_block(struct sha1_context *ctx)
{
    uint32_t a, b, c, d, e;
    uint32_t temp;
    uint8_t i;

    a = ctx->intermediate[0];
    b = ctx->intermediate[1];
    c = ctx->intermediate[2];
    d = ctx->intermediate[3];
    e = ctx->intermediate[4];

    for(i = 0; i < 80; i++) {
	    if (i >= 16) {
		    temp = (ctx->work[(i - 3) & 0xf] ^
			    ctx->work[(i - 8) & 0xf] ^
			    ctx->work[(i - 14) & 0xf] ^
			    ctx->work[i & 0xf]);
		    ctx->work[i & 0xf] = sha1_circular_shift(temp, 1);
	    }

	    if (i < 20) {
		    temp = ((b & c) | ((~b) & d)) + SHA1_K0;
	    } else if (i < 40) {
		    temp = (b ^ c ^ d) + SHA1_K1;
	    } else if (i < 60) {
		    temp = ((b & c) | (b & d) | (c & d)) + SHA1_K2;
	    } else {
		    temp = (b ^ c ^ d) + SHA1_K3;
	    }

	    temp += sha1_circular_shift(a, 5) + e + ctx->work[i & 0xf];

	    e = d;
	    d = c;
	    c = sha1_circular_shift(b, 30);
	    b = a;
	    a = temp;
    }

    ctx->intermediate[0] += a;
    ctx->intermediate[1] += b;
    ctx->intermediate[2] += c;
    ctx->intermediate[3] += d;
    ctx->intermediate[4] += e;
}

void sha1_input_byte(struct sha1_context *ctx, uint8_t val)
{
	uint32_t w = ((uint32_t)val) << (24 - ((ctx->block_pos & 3) << 3));

	if ((ctx->block_pos & 3) == 0)
		ctx->work[ctx->block_pos >> 2] = w;
	else
		ctx->work[ctx->block_pos >> 2] |= w;

	ctx->block_pos++;

	if (ctx->block_pos == SHA1_BLOCK_SIZE) {
		sha1_process_block(ctx);
		ctx->block_count++;
		ctx->block_pos = 0;
	}
}

void sha1_input(struct sha1_context *ctx, const uint8_t *data, uint16_t len)
{
	uint16_t i;

	for (i = 0; i < len; i++)
		sha1_input_byte(ctx, data[i]);
}

void sha1_finish(struct sha1_context *ctx)
{
	uint32_t msg_bits;

	/* Compute the total size in bits */
	msg_bits = (((uint32_t)ctx->block_count * SHA1_BLOCK_SIZE) +
		    ((uint32_t)ctx->block_pos)) * 8;

	/* Add the end mark */
	sha1_input_byte(ctx, 0x80);

	/* If the block is too small for the trailer pad it with 0 */
	while (ctx->block_pos > SHA1_BLOCK_SIZE - SHA1_TRAILER_SIZE)
		sha1_input_byte(ctx, 0x00);

	/* Pad with 0 up to the trailer and set the first 4 bytes of
	 * the trailer to 0 */
	while (ctx->block_pos < SHA1_BLOCK_SIZE - SHA1_TRAILER_SIZE + 4)
		sha1_input_byte(ctx, 0x00);

	/* Write the trailer low 4 bytes */
	sha1_input_byte(ctx, msg_bits >> 24);
	sha1_input_byte(ctx, msg_bits >> 16);
	sha1_input_byte(ctx, msg_bits >> 8);
	sha1_input_byte(ctx, msg_bits);
}

int8_t sha1_digest(struct sha1_context *ctx, uint8_t *digest, uint8_t len)
{
	uint8_t i;

	if (len > SHA1_HASH_SIZE)
		len = SHA1_HASH_SIZE;

	for (i = 0; i < len; i++)
		digest[i] = ctx->intermediate[i >> 2] >> (8 * (3 - (i & 3)));

	return len;
}

void sha1_hmac_init(struct sha1_context *ctx, const uint8_t *key, uint16_t len)
{
	uint16_t i;

	sha1_init(ctx);
	for (i = 0; i < len; i++)
		sha1_input_byte(ctx, key[i] ^ HMAC_INNER_PADDING);
	for ( ; i < SHA1_BLOCK_SIZE; i++)
		sha1_input_byte(ctx, HMAC_INNER_PADDING);

}

void sha1_hmac_finish(struct sha1_context *ctx, const uint8_t *key, uint16_t len)
{
	uint8_t inner_digest[SHA1_HASH_SIZE];
	uint16_t i;

	/* Compute the inner digest */
	sha1_finish(ctx);
	sha1_digest(ctx, inner_digest, sizeof(inner_digest));

	/* Compute the outer digest */
	sha1_init(ctx);
	for (i = 0; i < len; i++)
		sha1_input_byte(ctx, key[i] ^ HMAC_OUTER_PADDING);
	for ( ; i < SHA1_BLOCK_SIZE; i++)
		sha1_input_byte(ctx, HMAC_OUTER_PADDING);
	sha1_input(ctx, inner_digest, sizeof(inner_digest));
	sha1_finish(ctx);
}
