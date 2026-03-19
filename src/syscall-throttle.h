#ifndef _SYSCALL_THROTTHLE_H_
#define _SYSCALL_THROTTHLE_H_

#include <asm/preempt.h>
#include <linux/atomic.h>
#include <linux/compiler.h>
#include <linux/delay.h>
#include <linux/gfp_types.h>
#include <linux/irqflags.h>
#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/preempt.h>
#include <linux/printk.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/timer.h>
#include <linux/types.h>
#include <linux/wait.h>

#define MODNAME "SYSCALL-THROTTLE"

#define LOG_FINE if (0)
#define LOG_FINEST if (0)

#define dispatcher_symbol_name "x64_sys_call"
#define TIMER_INTERVAL 1000
#define CRITICAL_PER_UNIT 3

DEFINE_PER_CPU(struct kprobe **, saved_kprobe_context_p);
DECLARE_WAIT_QUEUE_HEAD(critical_sleeping_wq);

struct syscall_throttle_context {
  atomic_t hack_ready_on_cpu;
  atomic_t crit_req;
  atomic_t crit_lim;
  atomic_t crit_sleep;
  atomic_t running;
  struct kprobe probe_throttle;
  struct timer_list my_timer;
};

struct syscall_throttle_context *sys_thr_cxt;

#endif