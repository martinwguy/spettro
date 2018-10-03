/* timer.h - Declarations for timer.c */

#ifndef TIMER_H

extern void start_timer(void);
extern void stop_timer(void);
extern void change_timer_interval(double interval);

extern bool scroll_event_pending;

#define TIMER_H
#endif
