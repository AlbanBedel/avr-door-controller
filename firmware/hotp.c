#include <stdlib.h>

#include "hotp.h"
#include "sha1.h"

static uint32_t hotp_truncate(
	const uint8_t *digest, uint8_t digest_size, uint8_t digits)
{
	uint8_t offset = digest[digest_size - 1] & 0xf;
	uint32_t hash = 0;
	uint32_t mod = 1;
	uint8_t i;

	for (i = 0; i < 4; i++) {
		hash <<= 8;
		hash |= digest[offset + i];
	}

	for (i = 0; i < digits; i++)
		mod *= 10;
	return (hash & 0x7FFFFFFF) % mod;
}

uint32_t hotp_sha1(const uint8_t *key, uint16_t key_len,
		   uint32_t c, uint8_t digits)
{
	struct sha1_context ctx = {};
	uint8_t digest[SHA1_HASH_SIZE];

	sha1_hmac_init(&ctx, key, key_len);
	{ /* Avoid needing both tm and digest at the same time on the stack */
		uint8_t tm[8] = { 0, 0, 0, 0, c >> 24, c >> 16, c >> 8, c };
		sha1_input(&ctx, tm, sizeof(tm));
	}
	sha1_hmac_finish(&ctx, key, key_len);
	sha1_digest(&ctx, digest, sizeof(digest));
	return hotp_truncate(digest, sizeof(digest), digits);
}
