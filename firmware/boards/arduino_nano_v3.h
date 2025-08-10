/* MCU */
#define MCU			atmega328p
#define F_CPU			16000000

/* Life LED */
#define LIFE_LED_GPIO		GPIO(B, 5, HIGH_ACTIVE)

/* Doors */
#define NUM_DOORS		2

/* RTC */
#define HAS_RTC			1
#define DS3231_ADDR		0x68
#define DS3231_IRQ		IRQ(PC, 11)

/* Enable TOTP and HOTP support */
#define WITH_OTP		1
