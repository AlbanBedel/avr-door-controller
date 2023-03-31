#include <stdlib.h>
#include <errno.h>
#include "acl.h"
#include "eeprom.h"
#include "utils.h"

struct access_record_match {
	uint8_t type;
	uint8_t doors;
};

static int8_t acl_check_card(struct access_record_v2 *rec, uint32_t card)
{
	switch (ACCESS_RECORD_CARD_TYPE(rec)) {
	case ACCESS_RECORD_TYPE_CARD_NONE:
		return card == 0;

	case ACCESS_RECORD_TYPE_CARD_ID:
		return card == rec->card;

	default:
		return 0;
	}
}

static int8_t acl_check_pin(struct access_record_v2 *rec, uint32_t pin)
{
	switch (ACCESS_RECORD_PIN_TYPE(rec)) {
	case ACCESS_RECORD_TYPE_PIN_NONE:
		return pin == 0;

	case ACCESS_RECORD_TYPE_PIN_FIXED:
		return pin == rec->pin.fixed;

	case ACCESS_RECORD_TYPE_PIN_HOTP:
	case ACCESS_RECORD_TYPE_PIN_TOTP:
		return 0; /* TODO */

	default: /* should not happen */
		return 0;
	}

}

static int8_t access_record_filter(
	const struct access_record_hdr *hdr, const void *val)
{
	const struct access_record_match *match = val;

	/* Match on the door */
	if ((hdr->doors & match->doors) == 0)
		return 0;

	/* Then on the type of data in the record */
	switch(match->type) {
	case ACL_TYPE_PIN:
		return !ACCESS_RECORD_TYPE_HAS_CARD(hdr->type) &&
			ACCESS_RECORD_TYPE_HAS_PIN(hdr->type);

	case ACL_TYPE_CARD:
		return !ACCESS_RECORD_TYPE_HAS_PIN(hdr->type) &&
			ACCESS_RECORD_TYPE_HAS_CARD(hdr->type);

	case ACL_TYPE_CARD_AND_PIN:
		return ACCESS_RECORD_TYPE_HAS_PIN(hdr->type) &&
			ACCESS_RECORD_TYPE_HAS_CARD(hdr->type);

	default:
		return -EINVAL;
	}
}

static uint8_t acl_check_access_record(
	struct access_record_v2 *rec, uint32_t card, uint32_t pin)
{
	/* Check the card first as it is fast */
	if (!acl_check_card(rec, card))
		return 0;

	/* Then the pin as it is much slower */
	return acl_check_pin(rec, pin);
}

static int8_t acl_used(uint16_t idx, struct access_record_v2 *rec)
{
	/* For HOTP pin we need to update the counter value */
	if (ACCESS_RECORD_PIN_TYPE(rec) == ACCESS_RECORD_TYPE_PIN_HOTP) {
		rec->hdr.used = 1;
		return eeprom_write_access_record(idx, rec);
	}

	if (!rec->hdr.used) {
		rec->hdr.used = 1;
		return eeprom_update_access_record_hdr(idx, &rec->hdr);
	}

	return 0;
}

int8_t acl_check_access(uint8_t type, uint32_t card, uint32_t pin,
			uint8_t door_id)
{
	struct access_record_match match = {
		.type = type,
		.doors = BIT(door_id),
	};
	struct access_record_v2 rec;
	uint16_t idx;

	eeprom_for_each_access_record_where(
		idx, &rec, access_record_filter, &match) {
		if (acl_check_access_record(&rec, card, pin)) {
			acl_used(idx, &rec);
			return 0;
		}
	}

	return -EPERM;
}
