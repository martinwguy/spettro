/*
 * scheduler.h: Function call interface to scheduler.c
 */

extern void start_scheduler(int nthreads);
extern void schedule(calc_t *calc);
extern calc_t *get_work(void);
extern void reschedule_for_bigger_step(void);
