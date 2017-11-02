/*
 * spettro.h - definitions and declarations for everybody
 */

typedef enum {
	FALSE = 0,
	TRUE = 1,
} bool;

/* Slop factor for comparisons involving calculated floating point values. */
#define DELTA (1.0e-6)
