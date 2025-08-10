#ifndef SHA1_H
#define SHA1_H

#include <stdint.h>

#define SHA1_HASH_SIZE 20
#define SHA1_BLOCK_SIZE 64

struct sha1_context {
	uint32_t intermediate[SHA1_HASH_SIZE / 4];
	uint32_t work[SHA1_BLOCK_SIZE / 4];
	/* Enough for messages up to 16K */
	uint8_t block_count;
	uint8_t block_pos;
};

void sha1_init(struct sha1_context *ctx);

void sha1_input_byte(struct sha1_context *ctx, uint8_t val);

void sha1_input(struct sha1_context *ctx, const uint8_t *data, uint16_t len);

void sha1_finish(struct sha1_context *ctx);

int8_t sha1_digest(struct sha1_context *ctx, uint8_t *digest, uint8_t len);

void sha1_hmac_init(struct sha1_context *ctx, const uint8_t *key, uint16_t len);

void sha1_hmac_finish(struct sha1_context *ctx, const uint8_t *key, uint16_t len);

#endif /* SHA1_H */
