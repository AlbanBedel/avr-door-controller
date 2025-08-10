#ifndef HOTP_H
#define HOTP_H

#include <stdint.h>

uint32_t hotp_sha1(const uint8_t *key, uint16_t key_len,
		   uint32_t c, uint8_t digits);

#endif /* HOTP_H */
