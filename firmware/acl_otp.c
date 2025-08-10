#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "hotp.h"
#include "sha1.h"
#include "acl.h"
#include "eeprom.h"

static uint8_t root_key[CONTROLLER_KEY_SIZE];

int8_t acl_get_otp_key(const struct access_record_v2 *rec,
		       uint8_t *key, uint8_t key_size)
{
	const struct access_record_otp *otp = &rec->pin.otp;
	uint8_t info[2 + sizeof(rec->card) + 1];
	struct sha1_context ctx = {};
	uint8_t info_size = 0;

	if (ACCESS_RECORD_PIN_TYPE(rec) != ACCESS_RECORD_TYPE_PIN_HOTP &&
	    ACCESS_RECORD_PIN_TYPE(rec) != ACCESS_RECORD_TYPE_PIN_TOTP)
		return -EINVAL;

	// Compute the HKDF expansion of the root key with
	// the key ID and card as "info".
	info[info_size++] = otp->key_id >> 8;
	info[info_size++] = otp->key_id;
	if (ACCESS_RECORD_HAS_CARD(rec)) {
		info[info_size++] = rec->card >> 24;
		info[info_size++] = rec->card >> 16;
		info[info_size++] = rec->card >> 8;
		info[info_size++] = rec->card;
	}
	info[info_size++] = 1;

	sha1_hmac_init(&ctx, root_key, sizeof(root_key));
	sha1_input(&ctx, info, info_size);
	sha1_hmac_finish(&ctx, root_key, sizeof(root_key));
	return sha1_digest(&ctx, key, key_size);
}

static uint32_t int_to_pin(uint32_t v, uint8_t digits)
{
	uint32_t pin = 0xFFFFFFFF << (4 * digits);
	uint8_t i;

	for (i = 0; i < digits; i++) {
		pin |= (v % 10) << (4 * i);
		v /= 10;
	}

	return pin;
}

static uint32_t acl_get_otp_pin(const uint8_t *key, uint16_t key_len,
				uint32_t c, uint8_t digits)
{
	return int_to_pin(hotp_sha1(key, key_len, c, digits), digits);
}

int8_t acl_check_otp_pin(struct access_record_v2 *rec, uint32_t pin)
{
	uint8_t prev, follow, digits;
	uint8_t key[OTP_KEY_SIZE];
	uint32_t c, otp_pin;
	int8_t err;
	int8_t i;

	err = acl_get_otp_key(rec, key, sizeof(key));
	if (err < 0)
		return 0;

	switch (ACCESS_RECORD_PIN_TYPE(rec)) {
	case ACCESS_RECORD_TYPE_PIN_HOTP: {
		struct access_record_hotp *hotp = &rec->pin.hotp;

		digits = hotp->digits + 6;
		follow = hotp->resync_limit;
		prev = 0;
		c = hotp->c;
		break;
	}

	case ACCESS_RECORD_TYPE_PIN_TOTP: {
		struct access_record_totp *totp = &rec->pin.totp;

		digits = totp->digits + 6;
		follow = totp->allow_followings;
		prev = totp->allow_previous;
		c = time(NULL) + UNIX_OFFSET;
		if (totp->interval)
			c /= totp->interval * 60;
		else
			c /= 30;
		break;
	}

	default: /* should not happen */
		return 0;
	}

	otp_pin = acl_get_otp_pin(key, sizeof(key), c, digits);
	if (pin == otp_pin)
		goto pin_found;

	for (i = 1; i < follow + 1; i++) {
		otp_pin = acl_get_otp_pin(key, sizeof(key), c + i, digits);
		if (pin == otp_pin) {
			c = c + i;
			goto pin_found;
		}
	}

	for (i = 1; i < prev + 1; i++) {
		otp_pin = acl_get_otp_pin(key, sizeof(key), c - i, digits);
		if (pin == otp_pin) {
			c = c - i;
			goto pin_found;
		}
	}

	/* Not found */
	return 0;

pin_found:
	/* Update the HOTP counter if needed */
	if (ACCESS_RECORD_PIN_TYPE(rec) == ACCESS_RECORD_TYPE_PIN_HOTP)
		rec->pin.hotp.c = c + 1;
	return 1;
}

int8_t acl_load_otp_root_key(void)
{
	struct controller_config cfg;
	int8_t err;

	err = eeprom_get_controller_config(&cfg);
	if (err < 0)
		return err;

	memcpy(root_key, cfg.root_key, sizeof(root_key));
	return 0;
}
