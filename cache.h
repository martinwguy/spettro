/*
 * cache.h: Declarations for cache.c
 */

#ifndef CACHE_H

#include "calc.h"

extern void	remember_result(result_t *result);
extern result_t *recall_result(double t, int speclen);

#define CACHE_H
#endif
