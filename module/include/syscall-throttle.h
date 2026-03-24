#ifndef _SYSCALL_THROTTLE_H_
#define _SYSCALL_THROTTLE_H_

#include <asm/preempt.h>
#include <linux/atomic.h>
#include <linux/compiler.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/gfp_types.h>
#include <linux/irqflags.h>
#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/preempt.h>
#include <linux/printk.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/sprintf.h>
#include <linux/timer.h>
#include <linux/types.h>
#include <linux/wait.h>

#define MODNAME "SYSCALL-THROTTLE"

#define LOG if (1)
#define LOG_FINE if (0)
#define LOG_FINEST if (0)

#define DISPATCHER_SYMBOL_NAME "x64_sys_call"
#define TIMER_INTERVAL 1000
#define CRITICAL_PER_UNIT 3

DECLARE_PER_CPU(struct kprobe **, saved_kprobe_context_p);

struct syscall_throttle_context {
	atomic_t hack_ready_on_cpu;
	atomic_t crit_req;
	atomic_t crit_avail;
	atomic_t crit_sleep;
	atomic_t running;
	struct kprobe probe_throttle;
	struct timer_list periodic_timer;
	wait_queue_head_t critical_sleeping_wq;
	int Major;
	struct mutex operation_synchronizer;
	struct class *my_class;
	struct device *my_device;
	struct rhashtable pids_registry;
};

extern struct syscall_throttle_context *sys_thr_cxt;

int load_throttle(void);
int load_hack_search(void);
int load_timer(void);
int load_driver(void);
void unload_driver(void);
int load_monitor(void);
void unload_monitor(void);

void update_limit_and_wake(void);
int register_critical(char *, struct rhashtable *);
void unregister_critical(char *, struct rhashtable *);
bool is_registered(char *, struct rhashtable *);
int is_critical(int);

#endif