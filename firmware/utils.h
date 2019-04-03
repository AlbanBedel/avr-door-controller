#ifndef UTILS_H
#define UTILS_H

#include <stddef.h>

#define EXPAND(x)		x

#define CAT2_(a, b)		a ## b
#define CAT3_(a, b, c) 		a ## b ## c
#define CAT4_(a, b, c, d)	a ## b ## c ## d

#define CAT2(a, b)		EXPAND(CAT2_(a, b))
#define CAT3(a, b, c)		EXPAND(CAT3_(a, b, c))
#define CAT4(a, b, c, d)	EXPAND(CAT4_(a, b, c, d))

#ifndef BIT
#define BIT(x) (1 << (x))
#endif

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a)		(sizeof(a) / sizeof(*(a)))
#endif

#define PACKED			__attribute__((packed))

#ifndef container_of
#define container_of(ptr, type, member) ({			\
	const typeof( ((type *)0)->member ) *__mptr = (ptr);	\
	(type *)( (char *)__mptr - offsetof(type,member) );})
#endif

#endif /* UTILS_H */
