/*
 * scheduler.h: Function call interface to scheduler.c
 */

#include "calc.h"

extern void start_scheduler(int nthreads);
extern void stop_scheduler(void);
extern void schedule(calc_t *calc);
extern void drop_all_work(void);
extern calc_t *get_work(void);
extern void reschedule_for_bigger_step(void);
extern void calc_notify(result_t *result);
