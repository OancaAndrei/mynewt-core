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

#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <errno.h>
#include "syscfg/syscfg.h"
#include "bsp/cmsis_nvic.h"
#include "hal/hal_timer.h"
#include "nrf52.h"
#include "nrf52_bitfields.h"
#include "mcu/nrf52_hal.h"

/* IRQ prototype */
typedef void (*hal_timer_irq_handler_t)(void);

/* User CC 2 for reading counter, CC 3 for timer isr */
#define NRF_TIMER_CC_READ       (2)
#define NRF_TIMER_CC_INT        (3)

/* XXX: what about RTC timers? How are they instantiated? How do we
   relate timer numbers to them? */
#define NRF52_HAL_TIMER_MAX     (5)

/* Maximum timer frequency */
#define NRF52_MAX_TIMER_FREQ    (16000000)

struct nrf52_hal_timer {
    uint8_t tmr_enabled;
    uint8_t tmr_irq_num;
    uint8_t tmr_pad[2];
    uint32_t timer_isrs;
    uint32_t tmr_freq;
    NRF_TIMER_Type *tmr_reg;
    TAILQ_HEAD(hal_timer_qhead, hal_timer) hal_timer_q;
};

#if MYNEWT_VAL(TIMER_0)
struct nrf52_hal_timer nrf52_hal_timer0;
#endif
#if MYNEWT_VAL(TIMER_1)
struct nrf52_hal_timer nrf52_hal_timer1;
#endif
#if MYNEWT_VAL(TIMER_2)
struct nrf52_hal_timer nrf52_hal_timer2;
#endif
#if MYNEWT_VAL(TIMER_3)
struct nrf52_hal_timer nrf52_hal_timer3;
#endif
#if MYNEWT_VAL(TIMER_4)
struct nrf52_hal_timer nrf52_hal_timer4;
#endif

static const struct nrf52_hal_timer *nrf52_hal_timers[NRF52_HAL_TIMER_MAX] = {
#if MYNEWT_VAL(TIMER_0)
    &nrf52_hal_timer0,
#else
    NULL,
#endif
#if MYNEWT_VAL(TIMER_1)
    &nrf52_hal_timer1,
#else
    NULL,
#endif
#if MYNEWT_VAL(TIMER_2)
    &nrf52_hal_timer2,
#else
    NULL,
#endif
#if MYNEWT_VAL(TIMER_3)
    &nrf52_hal_timer3,
#else
    NULL,
#endif
#if MYNEWT_VAL(TIMER_4)
    &nrf52_hal_timer4
#else
    NULL
#endif
};

/* Resolve timer number into timer structure */
#define NRF52_HAL_TIMER_RESOLVE(__n, __v)       \
    if ((__n) >= NRF52_HAL_TIMER_MAX) {         \
        rc = EINVAL;                            \
        goto err;                               \
    }                                           \
    (__v) = (struct nrf52_hal_timer *) nrf52_hal_timers[(__n)];            \
    if ((__v) == NULL) {                        \
        rc = EINVAL;                            \
        goto err;                               \
    }

/* Interrupt mask for interrupt enable/clear */
#define NRF_TIMER_INT_MASK(x)    ((1 << (uint32_t)(x)) << 16)

static uint32_t
nrf_read_timer_cntr(NRF_TIMER_Type *hwtimer)
{
    uint32_t tcntr;

    /* Force a capture of the timer into 'cntr' capture channel; read it */
    hwtimer->TASKS_CAPTURE[NRF_TIMER_CC_READ] = 1;
    tcntr = hwtimer->CC[NRF_TIMER_CC_READ];

    return tcntr;
}

/**
 * nrf timer set ocmp
 *
 * Set the OCMP used by the timer to the desired expiration tick
 *
 * NOTE: Must be called with interrupts disabled.
 *
 * @param timer Pointer to timer.
 */
static void
nrf_timer_set_ocmp(struct nrf52_hal_timer *bsptimer, uint32_t expiry)
{
    NRF_TIMER_Type *hwtimer;

    hwtimer = bsptimer->tmr_reg;

    /* Disable ocmp interrupt and set new value */
    hwtimer->INTENCLR = NRF_TIMER_INT_MASK(NRF_TIMER_CC_INT);

    /* Set output compare register to timer expiration */
    hwtimer->CC[NRF_TIMER_CC_INT] = expiry;

    /* Clear interrupt flag */
    hwtimer->EVENTS_COMPARE[NRF_TIMER_CC_INT] = 0;

    /* Enable the output compare interrupt */
    hwtimer->INTENSET = NRF_TIMER_INT_MASK(NRF_TIMER_CC_INT);

    /* Force interrupt to occur as we may have missed it */
    if ((int32_t)(nrf_read_timer_cntr(hwtimer) - expiry) >= 0) {
        NVIC_SetPendingIRQ(bsptimer->tmr_irq_num);
    }
}

/* Disable output compare used for timer */
static void
nrf_timer_disable_ocmp(NRF_TIMER_Type *hwtimer)
{
    hwtimer->INTENCLR = NRF_TIMER_INT_MASK(NRF_TIMER_CC_INT);
}

#if (MYNEWT_VAL(TIMER_0) || MYNEWT_VAL(TIMER_1) || MYNEWT_VAL(TIMER_2) || \
     MYNEWT_VAL(TIMER_3) || MYNEWT_VAL(TIMER_4))
/**
 * hal timer chk queue
 *
 *
 * @param bsptimer
 */
static void
hal_timer_chk_queue(struct nrf52_hal_timer *bsptimer)
{
    uint32_t tcntr;
    uint32_t ctx;
    struct hal_timer *timer;

    /* disable interrupts */
    __HAL_DISABLE_INTERRUPTS(ctx);
    while ((timer = TAILQ_FIRST(&bsptimer->hal_timer_q)) != NULL) {
        tcntr = nrf_read_timer_cntr(bsptimer->tmr_reg);
        if ((int32_t)(tcntr - timer->expiry) >= 0) {
            TAILQ_REMOVE(&bsptimer->hal_timer_q, timer, link);
            timer->link.tqe_prev = NULL;
            timer->cb_func(timer->cb_arg);
        } else {
            break;
        }
    }

    /* Any timers left on queue? If so, we need to set OCMP */
    timer = TAILQ_FIRST(&bsptimer->hal_timer_q);
    if (timer) {
        nrf_timer_set_ocmp(bsptimer, timer->expiry);
    } else {
        nrf_timer_disable_ocmp(bsptimer->tmr_reg);
    }
    __HAL_ENABLE_INTERRUPTS(ctx);
}
#endif

/**
 * hal timer irq handler
 *
 * Generic HAL timer irq handler.
 *
 * @param tmr
 */
/**
 * hal timer irq handler
 *
 * This is the global timer interrupt routine.
 *
 */
#if (MYNEWT_VAL(TIMER_0) || MYNEWT_VAL(TIMER_1) || MYNEWT_VAL(TIMER_2) || \
     MYNEWT_VAL(TIMER_3) || MYNEWT_VAL(TIMER_4))

static void
hal_timer_irq_handler(struct nrf52_hal_timer *bsptimer)
{
    uint32_t compare;
    NRF_TIMER_Type *hwtimer;

    /* Check interrupt source. If set, clear them */
    hwtimer = bsptimer->tmr_reg;
    compare = hwtimer->EVENTS_COMPARE[NRF_TIMER_CC_INT];
    if (compare) {
        hwtimer->EVENTS_COMPARE[NRF_TIMER_CC_INT] = 0;
    }

    /* XXX: make these stats? */
    /* Count # of timer isrs */
    ++bsptimer->timer_isrs;

    /*
     * NOTE: we dont check the 'compare' variable here due to how the timer
     * is implemented on this chip. There is no way to force an output
     * compare, so if we are late setting the output compare (i.e. the timer
     * counter is already passed the output compare value), we use the NVIC
     * to set a pending interrupt. This means that there will be no compare
     * flag set, so all we do is check to see if the compare interrupt is
     * enabled.
     */
    if (hwtimer->INTENCLR & NRF_TIMER_INT_MASK(NRF_TIMER_CC_INT)) {
        hal_timer_chk_queue(bsptimer);
        /* XXX: Recommended by nordic to make sure interrupts are cleared */
        compare = hwtimer->EVENTS_COMPARE[NRF_TIMER_CC_INT];
    }
}
#endif

#if MYNEWT_VAL(TIMER_0)
void
nrf52_timer0_irq_handler(void)
{
    hal_timer_irq_handler(&nrf52_hal_timer0);
}
#endif

#if MYNEWT_VAL(TIMER_1)
void
nrf52_timer1_irq_handler(void)
{
    hal_timer_irq_handler(&nrf52_hal_timer1);
}
#endif

#if MYNEWT_VAL(TIMER_2)
void
nrf52_timer2_irq_handler(void)
{
    hal_timer_irq_handler(&nrf52_hal_timer2);
}
#endif

#if MYNEWT_VAL(TIMER_3)
void
nrf52_timer3_irq_handler(void)
{
    hal_timer_irq_handler(&nrf52_hal_timer3);
}
#endif

#if MYNEWT_VAL(TIMER_4)
void
nrf52_timer4_irq_handler(void)
{
    hal_timer_irq_handler(&nrf52_hal_timer4);
}
#endif

/**
 * hal timer init
 *
 * Initialize (and start) a timer to run at the desired frequency.
 *
 * @param timer_num
 * @param freq_hz
 *
 * @return int
 */
int
hal_timer_init(int timer_num, uint32_t freq_hz)
{
    int rc;
    uint8_t prescaler;
    uint8_t irq_num;
    uint32_t ctx;
    uint32_t div;
    uint32_t min_delta;
    uint32_t max_delta;
    uint32_t prio;
    struct nrf52_hal_timer *bsptimer;
    NRF_TIMER_Type *hwtimer;
    hal_timer_irq_handler_t irq_isr;

    NRF52_HAL_TIMER_RESOLVE(timer_num, bsptimer);

    /* Set timer to desired frequency */
    div = NRF52_MAX_TIMER_FREQ / freq_hz;

    /* Largest prescaler is 2^9 and must make sure frequency not too high */
    if (bsptimer->tmr_enabled || (div == 0) || (div > 512)) {
        rc = EINVAL;
        goto err;
    }

    if (div == 1) {
        prescaler = 0;
    } else {
        /* Find closest prescaler */
        for (prescaler = 1; prescaler < 10; ++prescaler) {
            if (div <= (1 << prescaler)) {
                min_delta = div - (1 << (prescaler - 1));
                max_delta = (1 << prescaler) - div;
                if (min_delta < max_delta) {
                    prescaler -= 1;
                }
                break;
            }
        }
    }

    /* Now set the actual frequency */
    bsptimer->tmr_freq = NRF52_MAX_TIMER_FREQ / (1 << prescaler);

    switch (timer_num) {
#if MYNEWT_VAL(TIMER_0)
    case 0:
        irq_num = TIMER0_IRQn;
        hwtimer = NRF_TIMER0;
        irq_isr = nrf52_timer0_irq_handler;
        prio = MYNEWT_VAL(TIMER_0_INTERRUPT_PRIORITY);
        break;
#endif
#if MYNEWT_VAL(TIMER_1)
    case 1:
        irq_num = TIMER1_IRQn;
        hwtimer = NRF_TIMER1;
        irq_isr = nrf52_timer1_irq_handler;
        prio = MYNEWT_VAL(TIMER_1_INTERRUPT_PRIORITY);
        break;
#endif
#if MYNEWT_VAL(TIMER_2)
    case 2:
        irq_num = TIMER2_IRQn;
        hwtimer = NRF_TIMER2;
        irq_isr = nrf52_timer2_irq_handler;
        prio = MYNEWT_VAL(TIMER_2_INTERRUPT_PRIORITY);
        break;
#endif
#if MYNEWT_VAL(TIMER_3)
    case 3:
        irq_num = TIMER3_IRQn;
        hwtimer = NRF_TIMER3;
        irq_isr = nrf52_timer3_irq_handler;
        prio = MYNEWT_VAL(TIMER_3_INTERRUPT_PRIORITY);
        break;
#endif
#if MYNEWT_VAL(TIMER_4)
    case 4:
        irq_num = TIMER4_IRQn;
        hwtimer = NRF_TIMER4;
        irq_isr = nrf52_timer4_irq_handler;
        prio = MYNEWT_VAL(TIMER_4_INTERRUPT_PRIORITY);
        break;
#endif
    default:
        hwtimer = NULL;
        prio = 0;
        break;
    }

    if (hwtimer == NULL) {
        rc = EINVAL;
        goto err;
    }

    bsptimer->tmr_reg = hwtimer;
    bsptimer->tmr_irq_num = irq_num;
    bsptimer->tmr_enabled = 1;

    /* disable interrupts */
    __HAL_DISABLE_INTERRUPTS(ctx);

    /* Make sure HFXO is started */
    if ((NRF_CLOCK->HFCLKSTAT & CLOCK_HFCLKSTAT_STATE_Msk) == 0) {
        NRF_CLOCK->EVENTS_HFCLKSTARTED = 0;
        NRF_CLOCK->TASKS_HFCLKSTART = 1;
        while (1) {
            if ((NRF_CLOCK->EVENTS_HFCLKSTARTED) != 0) {
                break;
            }
        }
    }

    /* Stop the timer first */
    hwtimer->TASKS_STOP = 1;

    /* Put the timer in timer mode using 32 bits. */
    hwtimer->MODE = TIMER_MODE_MODE_Timer;
    hwtimer->BITMODE = TIMER_BITMODE_BITMODE_32Bit;

    /* Set the pre-scalar */
    hwtimer->PRESCALER = prescaler;

    /* Start the timer */
    hwtimer->TASKS_START = 1;

    /* Set isr in vector table and enable interrupt */
    NVIC_SetPriority(irq_num, prio);
    NVIC_SetVector(irq_num, (uint32_t)irq_isr);
    NVIC_EnableIRQ(irq_num);

    __HAL_ENABLE_INTERRUPTS(ctx);

    return 0;

err:
    return rc;
}

/**
 * hal timer deinit
 *
 * De-initialize a HW timer.
 *
 * @param timer_num
 *
 * @return int
 */
int
hal_timer_deinit(int timer_num)
{
    int rc;
    uint32_t ctx;
    struct nrf52_hal_timer *bsptimer;
    NRF_TIMER_Type *hwtimer;

    NRF52_HAL_TIMER_RESOLVE(timer_num, bsptimer);

    __HAL_DISABLE_INTERRUPTS(ctx);
    hwtimer = bsptimer->tmr_reg;
    hwtimer->INTENCLR = NRF_TIMER_INT_MASK(NRF_TIMER_CC_INT);
    hwtimer->TASKS_STOP = 1;
    __HAL_ENABLE_INTERRUPTS(ctx);

    bsptimer->tmr_enabled = 0;

err:
    return rc;
}

/**
 * hal timer get resolution
 *
 * Get the resolution of the timer. This is the timer period, in nanoseconds
 *
 * @param timer_num
 *
 * @return uint32_t The
 */
uint32_t
hal_timer_get_resolution(int timer_num)
{
    int rc;
    uint32_t resolution;
    struct nrf52_hal_timer *bsptimer;

    NRF52_HAL_TIMER_RESOLVE(timer_num, bsptimer);

    resolution = 1000000000 / bsptimer->tmr_freq;
    return resolution;

err:
    rc = 0;
    return rc;
}

/**
 * hal timer read
 *
 * Returns the timer counter. NOTE: if the timer is a 16-bit timer, only
 * the lower 16 bits are valid. If the timer is a 64-bit timer, only the
 * low 32-bits are returned.
 *
 * @return uint32_t The timer counter register.
 */
uint32_t
hal_timer_read(int timer_num)
{
    int rc;
    uint32_t tcntr;
    struct nrf52_hal_timer *bsptimer;

    NRF52_HAL_TIMER_RESOLVE(timer_num, bsptimer);

    /* Force a capture of the timer into 'cntr' capture channel; read it */
    tcntr = nrf_read_timer_cntr(bsptimer->tmr_reg);

    return tcntr;

    /* Assert here since there is no invalid return code */
err:
    assert(0);
    rc = 0;
    return rc;
}

/**
 * hal timer delay
 *
 * Blocking delay for n ticks
 *
 * @param timer_num
 * @param ticks
 *
 * @return int 0 on success; error code otherwise.
 */
int
hal_timer_delay(int timer_num, uint32_t ticks)
{
    int rc;
    uint32_t until;
    NRF_TIMER_Type *hwtimer;
    struct nrf52_hal_timer *bsptimer;

    NRF52_HAL_TIMER_RESOLVE(timer_num, bsptimer);

    hwtimer = bsptimer->tmr_reg;
    until = nrf_read_timer_cntr(hwtimer) + ticks;
    while ((int32_t)(nrf_read_timer_cntr(hwtimer) - until) <= 0) {
        /* Loop here till finished */
    }
    rc = 0;

err:
    return rc;
}

/**
 *
 * Initialize the HAL timer structure with the callback and the callback
 * argument. Also initializes the HW specific timer pointer.
 *
 * @param cb_func
 *
 * @return int
 */
int
hal_timer_set_cb(int timer_num, struct hal_timer *timer, hal_timer_cb cb_func,
                 void *arg)
{
    int rc;
    struct nrf52_hal_timer *bsptimer;

    NRF52_HAL_TIMER_RESOLVE(timer_num, bsptimer);

    timer->cb_func = cb_func;
    timer->cb_arg = arg;
    timer->link.tqe_prev = NULL;
    timer->bsp_timer = bsptimer;

    rc = 0;

err:
    return rc;
}

int
hal_timer_start(struct hal_timer *timer, uint32_t ticks)
{
    int rc;
    uint32_t tick;
    struct nrf52_hal_timer *bsptimer;

    /* Set the tick value at which the timer should expire */
    bsptimer = (struct nrf52_hal_timer *)timer->bsp_timer;
    tick = nrf_read_timer_cntr(bsptimer->tmr_reg) + ticks;
    rc = hal_timer_start_at(timer, tick);
    return rc;
}

int
hal_timer_start_at(struct hal_timer *timer, uint32_t tick)
{
    uint32_t ctx;
    struct hal_timer *entry;
    struct nrf52_hal_timer *bsptimer;

    if ((timer == NULL) || (timer->link.tqe_prev != NULL) ||
        (timer->cb_func == NULL)) {
        return EINVAL;
    }
    bsptimer = (struct nrf52_hal_timer *)timer->bsp_timer;
    timer->expiry = tick;

    __HAL_DISABLE_INTERRUPTS(ctx);

    if (TAILQ_EMPTY(&bsptimer->hal_timer_q)) {
        TAILQ_INSERT_HEAD(&bsptimer->hal_timer_q, timer, link);
    } else {
        TAILQ_FOREACH(entry, &bsptimer->hal_timer_q, link) {
            if ((int32_t)(timer->expiry - entry->expiry) < 0) {
                TAILQ_INSERT_BEFORE(entry, timer, link);
                break;
            }
        }
        if (!entry) {
            TAILQ_INSERT_TAIL(&bsptimer->hal_timer_q, timer, link);
        }
    }

    /* If this is the head, we need to set new OCMP */
    if (timer == TAILQ_FIRST(&bsptimer->hal_timer_q)) {
        nrf_timer_set_ocmp(bsptimer, timer->expiry);
    }

    __HAL_ENABLE_INTERRUPTS(ctx);

    return 0;
}

/**
 * hal timer stop
 *
 * Stop a timer.
 *
 * @param timer
 *
 * @return int
 */
int
hal_timer_stop(struct hal_timer *timer)
{
    uint32_t ctx;
    int reset_ocmp;
    struct hal_timer *entry;
    struct nrf52_hal_timer *bsptimer;

    if (timer == NULL) {
        return EINVAL;
    }

   bsptimer = (struct nrf52_hal_timer *)timer->bsp_timer;

    __HAL_DISABLE_INTERRUPTS(ctx);

    if (timer->link.tqe_prev != NULL) {
        reset_ocmp = 0;
        if (timer == TAILQ_FIRST(&bsptimer->hal_timer_q)) {
            /* If first on queue, we will need to reset OCMP */
            entry = TAILQ_NEXT(timer, link);
            reset_ocmp = 1;
        }
        TAILQ_REMOVE(&bsptimer->hal_timer_q, timer, link);
        timer->link.tqe_prev = NULL;
        if (reset_ocmp) {
            if (entry) {
                nrf_timer_set_ocmp((struct nrf52_hal_timer *)entry->bsp_timer,
                                   entry->expiry);
            } else {
                nrf_timer_disable_ocmp(bsptimer->tmr_reg);
            }
        }
    }

    __HAL_ENABLE_INTERRUPTS(ctx);

    return 0;
}
