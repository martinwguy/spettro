/*
 * cache.h: Declarations for cache.c
 */

#ifndef CACHE_H

#include "calc.h"

extern result_t *remember_result(result_t *result);
extern result_t *recall_result(double t, int speclen, window_function_t window);
extern void	drop_all_results(void);

#define CACHE_H
#endif
