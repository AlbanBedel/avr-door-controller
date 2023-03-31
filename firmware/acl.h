#ifndef ACL_H
#define ACL_H

#include <stdint.h>

/*
 * This API should decouple the database from the access control request.
 *
 * Access control request are for a given door using a card and/or pin as
 * credentials. So access is based on the door and we then check if the given
 * credentials can be used for this door. In the case of OTP the valid PIN
 * has to be computed which is an expensive process.
 *
 * On the other end the database must store this list in a compact way.
 */

#define ACL_TYPE_NONE			0
#define ACL_TYPE_PIN			1
#define ACL_TYPE_CARD			2
#define ACL_TYPE_CARD_AND_PIN		(ACL_TYPE_PIN | ACL_TYPE_CARD)

int8_t acl_check_access(
	uint8_t type, uint32_t card, uint32_t pin, uint8_t door_id);

#endif /* ACL_H */
