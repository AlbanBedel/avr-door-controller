#ifndef EEPROM_TYPES_H
#define EEPROM_TYPES_H

#include <stdint.h>
#include "utils.h"

#define ACCESS_TYPE_NONE		0
#define ACCESS_TYPE_PIN			1
#define ACCESS_TYPE_CARD		2
#define ACCESS_TYPE_CARD_AND_PIN	(ACCESS_TYPE_PIN | ACCESS_TYPE_CARD)

struct access_record {
	/* Pin code or card number */
	uint32_t key;
	/* Pin or key */
	uint8_t type   : 2;
	uint8_t invalid: 1;
	/* Mark if the record has been used since the last check */
	uint8_t used   : 1;
	/* Doors that can be opened with this token */
	uint8_t doors  : 4;
} PACKED;

struct door_config {
	/* Time the door should stay open in ms */
	uint16_t open_time;
	/* Reserved */
	uint8_t reserved[2];
} PACKED;

#define CONTROLLER_KEY_SIZE		20

struct controller_config {
	/* Device specific PRK, the result of the first stage of HKDF */
	uint8_t root_key[CONTROLLER_KEY_SIZE];
} PACKED;

#define ACCESS_RECORD_TYPE_CARD_NONE	0
#define ACCESS_RECORD_TYPE_CARD_ID	1
#define ACCESS_RECORD_TYPE_CARD(type)	((type) & 1)
#define ACCESS_RECORD_TYPE_HAS_CARD(type) \
	(ACCESS_RECORD_TYPE_CARD(type) != ACCESS_RECORD_TYPE_CARD_NONE)

#define ACCESS_RECORD_TYPE_PIN_NONE	(0 << 1)
#define ACCESS_RECORD_TYPE_PIN_FIXED	(1 << 1)
#define ACCESS_RECORD_TYPE_PIN_HOTP	(2 << 1)
#define ACCESS_RECORD_TYPE_PIN_TOTP	(3 << 1)
#define ACCESS_RECORD_TYPE_PIN(type)	((type) & 6)
#define ACCESS_RECORD_TYPE_HAS_PIN(type) \
	(ACCESS_RECORD_TYPE_PIN(type) != ACCESS_RECORD_TYPE_PIN_NONE)

#define ACCESS_RECORD_TYPE_ENTRIES(type) \
	((ACCESS_RECORD_TYPE_HAS_CARD(type) && ACCESS_RECORD_TYPE_HAS_PIN(type)) ? 2 : 1)

#define ACCESS_RECORD_TYPE_(c, p) ((c) | (p))
#define ACCESS_RECORD_TYPE(card, pin) \
	ACCESS_RECORD_TYPE_(CAT2(ACCESS_RECORD_TYPE_CARD_, card), \
			    CAT2(ACCESS_RECORD_TYPE_PIN_, pin))

#define ACCESS_RECORD_CARD_TYPE(r) ACCESS_RECORD_TYPE_CARD((r)->hdr.type)
#define ACCESS_RECORD_HAS_CARD(r) ACCESS_RECORD_TYPE_HAS_CARD((r)->hdr.type)

#define ACCESS_RECORD_PIN_TYPE(r) ACCESS_RECORD_TYPE_PIN((r)->hdr.type)
#define ACCESS_RECORD_HAS_PIN(r) ACCESS_RECORD_TYPE_HAS_PIN((r)->hdr.type)

#define ACCESS_RECORD_IS_EMPTY(r) ((r)->hdr.type == ACCESS_RECORD_TYPE(NONE, NONE))
#define ACCESS_RECORD_IS_CONTINUATION(r) (!ACCESS_RECORD_IS_EMPTY(r) && (r)->hdr.doors == 0)
#define ACCESS_RECORD_ENTRIES(r) ACCESS_RECORD_TYPE_ENTRIES((r)->hdr.type)

struct access_record_hdr {
	uint8_t type	: 3;
	/* Mark if this record has ever been used */
	uint8_t used	: 1;
	/* Bit mask of the allowed doors */
	uint8_t doors	: 4;
} PACKED;

struct access_record_otp {
	/* This index identify the key, the real key is derived from the
	 * device key using one round HKDF (see RFC5869 for details)
	 * with this index as info data.
	 */
	uint16_t key_id           : 10;

	/* The number of digits to use for the PIN, minus 6 */
	uint16_t digits           : 2;

	/* Algorithm specific configs */
	uint16_t otp_config       : 4;

	/* The current counter value or time interval */
	uint16_t value;
} PACKED;

struct access_record_hotp {
	uint16_t key_id           : 10;
	uint16_t digits           : 2;

	/* Upper limit for re-syncing the counter */
	uint16_t resync_limit     : 4;

	/* The current counter value */
	uint16_t c;
} PACKED;

struct access_record_totp {
	uint16_t key_id           : 10;
	uint16_t digits           : 2;

	/* Allow followings PIN, this is useful to cope with clock drift
	 * when using relatively short intervals. */
	uint16_t allow_followings : 1;

	/* Allow previous PIN, this is also useful to cope with clock drift
	 * but it can also be used to put a lower bound on the PIN validity.
	 * For example with an interval of 8 hours and 3 valid PINs a freshly
	 * issued PIN will always be valid for at least 16h. */
	uint16_t allow_previous   : 3;

	/* The time interval in minutes */
	uint16_t interval;
} PACKED;

union access_record_pin {
	uint32_t fixed;
	struct access_record_otp otp;
	struct access_record_hotp hotp;
	struct access_record_totp totp;
};

/* In the EEPROM a record consist of 1 or 2 struct access_record_entry.
 *
 * When 2 record are needed the second record is known as continuation record.
 * The doors bitmask is cleared in such records to allow identifying them.
 * The type is also modified to only reflect the data in the record itself.
 */
struct access_record_entry {
	struct access_record_hdr hdr;
	union {
		uint32_t card;
		union access_record_pin pin;
	};
} PACKED;

/* In memory we always have card and pin data */
struct access_record_v2 {
	struct access_record_hdr hdr;
	uint32_t card;
	union access_record_pin pin;
} PACKED;

#endif /* EEPROM_TYPES_H */
