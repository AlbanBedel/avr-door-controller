/* MCU */
#define MCU			atmega168
#define F_CPU			16000000

/* Life LED */
#define LIFE_LED_GPIO		GPIO(B, 5, HIGH_ACTIVE)

/* Doors */
#define NUM_DOORS		2

/* RTC */
#define HAS_RTC			0
#define DS3231_ADDR		0
#define DS3231_IRQ		0
