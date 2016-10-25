/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#ifndef H_OS_CPUTIME_
#define H_OS_CPUTIME_

#ifdef __cplusplus
extern "C" {
#endif

#include "syscfg/syscfg.h"
#include "os/queue.h"
#include "hal/hal_timer.h"

/*
 * NOTE: these definitions allow one to override the cputime frequency used.
 * The reason these definitions exist is to make the code more efficient/smaller
 * when CPUTIME counts at 1 MHz.
 *
 * For those who want a different cputime frequency, you can set the config
 * definition for OS_CPUTIME_FREQ to the desired frequency in your project,
 * target or bsp.
 */
#if (MYNEWT_VAL(OS_CPUTIME_FREQ) == 0)
#error "cputime frequency cannot be 0!"
#endif

#if (MYNEWT_VAL(OS_CPUTIME_FREQ) == 1000000)
#define OS_CPUTIME_FREQ_1MHZ
#endif

/* CPUTIME data. */
struct os_cputime_data
{
    uint32_t ticks_per_usec;    /* number of ticks per usec */
};
extern struct os_cputime_data g_cputime;

/* Helpful macros to compare cputimes */
#define CPUTIME_LT(__t1, __t2) ((int32_t)   ((__t1) - (__t2)) < 0)
#define CPUTIME_GT(__t1, __t2) ((int32_t)   ((__t1) - (__t2)) > 0)
#define CPUTIME_GEQ(__t1, __t2) ((int32_t)  ((__t1) - (__t2)) >= 0)
#define CPUTIME_LEQ(__t1, __t2) ((int32_t)  ((__t1) - (__t2)) <= 0)

/**
 * os cputime init
 *
 * Initialize the cputime module. This must be called after os_init is called
 * and before any other timer API are used. This should be called only once
 * and should be called before the hardware timer is used.
 *
 * @param clock_freq The desired cputime frequency, in hertz (Hz).
 *
 * @return int 0 on success; -1 on error.
 */
int os_cputime_init(uint32_t clock_freq);

/**
 * os cputime get32
 *
 * Returns the low 32 bits of cputime.
 *
 * @return uint32_t The lower 32 bits of cputime
 */
uint32_t os_cputime_get32(void);

/**
 * os cputime nsecs to ticks
 *
 * Converts the given number of nanoseconds into cputime ticks.
 *
 * @param usecs The number of nanoseconds to convert to ticks
 *
 * @return uint32_t The number of ticks corresponding to 'nsecs'
 */
uint32_t os_cputime_nsecs_to_ticks(uint32_t nsecs);

/**
 * os cputime ticks to nsecs
 *
 * Convert the given number of ticks into nanoseconds.
 *
 * @param ticks The number of ticks to convert to nanoseconds.
 *
 * @return uint32_t The number of nanoseconds corresponding to 'ticks'
 */
uint32_t os_cputime_ticks_to_nsecs(uint32_t ticks);

#if defined(OS_CPUTIME_FREQ_1MHZ)
#define os_cputime_usecs_to_ticks(x)    (x)
#define os_cputime_ticks_to_usecs(x)    (x)
#else
/**
 * os cputime usecs to ticks
 *
 * Converts the given number of microseconds into cputime ticks.
 *
 * @param usecs The number of microseconds to convert to ticks
 *
 * @return uint32_t The number of ticks corresponding to 'usecs'
 */
uint32_t os_cputime_usecs_to_ticks(uint32_t usecs);

/**
 * os cputime ticks to usecs
 *
 * Convert the given number of ticks into microseconds.
 *
 * @param ticks The number of ticks to convert to microseconds.
 *
 * @return uint32_t The number of microseconds corresponding to 'ticks'
 */
uint32_t os_cputime_ticks_to_usecs(uint32_t ticks);
#endif

/**
 * os cputime delay ticks
 *
 * Wait until the number of ticks has elapsed. This is a blocking delay.
 *
 * @param ticks The number of ticks to wait.
 */
void os_cputime_delay_ticks(uint32_t ticks);

/**
 * os cputime delay nsecs
 *
 * Wait until 'nsecs' nanoseconds has elapsed. This is a blocking delay.
 *
 * @param nsecs The number of nanoseconds to wait.
 */
void os_cputime_delay_nsecs(uint32_t nsecs);

/**
 * os cputime delay usecs
 *
 * Wait until 'usecs' microseconds has elapsed. This is a blocking delay.
 *
 * @param usecs The number of usecs to wait.
 */
void os_cputime_delay_usecs(uint32_t usecs);

/**
 * os cputime timer init
 *
 * @param timer The timer to initialize. Cannot be NULL.
 * @param fp    The timer callback function. Cannot be NULL.
 * @param arg   Pointer to data object to pass to timer.
 */
void os_cputime_timer_init(struct hal_timer *timer, hal_timer_cb fp, void *arg);

/**
 * os cputime timer start
 *
 * Start a cputimer that will expire at 'cputime'. If cputime has already
 * passed, the timer callback will still be called (at interrupt context).
 *
 * @param timer     Pointer to timer to start. Cannot be NULL.
 * @param cputime   The cputime at which the timer should expire.
 */
void os_cputime_timer_start(struct hal_timer *timer, uint32_t cputime);

/**
 * os cputimer timer relative
 *
 * Sets a cpu timer that will expire 'usecs' microseconds from the current
 * cputime.
 *
 * @param timer Pointer to timer. Cannot be NULL.
 * @param usecs The number of usecs from now at which the timer will expire.
 */
void os_cputime_timer_relative(struct hal_timer *timer, uint32_t usecs);

/**
 * os cputime timer stop
 *
 * Stops a cputimer from running. The timer is removed from the timer queue
 * and interrupts are disabled if no timers are left on the queue. Can be
 * called even if timer is not running.
 *
 * @param timer Pointer to cputimer to stop. Cannot be NULL.
 */
void os_cputime_timer_stop(struct hal_timer *timer);

#ifdef __cplusplus
}
#endif

#endif /* H_OS_CPUTIME_ */
