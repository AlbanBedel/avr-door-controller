#ifndef UTILS_H
#define UTILS_H

#define EXPAND(x)		x

#define CAT2_(a, b)		a ## b
#define CAT3_(a, b, c) 		a ## b ## c
#define CAT4_(a, b, c, d)	a ## b ## c ## d

#define CAT2(a, b)		EXPAND(CAT2_(a, b))
#define CAT3(a, b, c)		EXPAND(CAT3_(a, b, c))
#define CAT4(a, b, c, d)	EXPAND(CAT4_(a, b, c, d))

#define BIT(x) (1 << (x))

#define ARRAY_SIZE(a)		(sizeof(a) / sizeof(*(a)))

#endif /* UTILS_H */
