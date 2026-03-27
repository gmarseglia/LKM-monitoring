#ifndef _SYSCALL_THROTTLE_H_
#define _SYSCALL_THROTTLE_H_

#include <asm-generic/errno-base.h>
#include <asm/preempt.h>
#include <linux/atomic.h>
#include <linux/bitmap.h>
#include <linux/compiler.h>
#include <linux/cred.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/gfp_types.h>
#include <linux/irqflags.h>
#include <linux/jhash.h>
#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/preempt.h>
#include <linux/printk.h>
#include <linux/rcutree.h>
#include <linux/rhashtable-types.h>
#include <linux/rhashtable.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/spinlock_types.h>
#include <linux/sprintf.h>
#include <linux/stddef.h>
#include <linux/timer.h>
#include <linux/types.h>
#include <linux/uidgid.h>
#include <linux/wait.h>

#define MODNAME "SYSCALL-THROTTLE"

#define LOG if (1)
#define LOG_FINE if (0)
#define LOG_FINEST if (0)

#define DISPATCHER_SYMBOL_NAME "x64_sys_call"
#define TIMER_INTERVAL 1000
#define CRITICAL_PER_UNIT 3
#define MAX_NR 323
#define __ST_MAX_STR_LEN 64

DECLARE_PER_CPU(struct kprobe **, saved_kprobe_context_p);

struct syscall_throttle_context {
	/* For interruptible kprobes */
	atomic_t hack_ready_on_cpu;

	/* For monitoring */
	unsigned long *sys_numbers_registry;
	struct rhashtable pids_registry;
	struct rhashtable euid_registry;
	struct rhashtable prog_names_registry;

	/* For throttling */
	struct kprobe probe_throttle;
	atomic_t throttle_running;
	atomic_t crit_req;
	atomic_t crit_avail;
	atomic_t crit_sleep;

	/* For sleep/wakeup */
	struct timer_list periodic_timer;
	wait_queue_head_t critical_sleeping_wq;

	/* For driver */
	int Major;
	struct mutex operation_synchronizer;
	struct class *my_class;
	struct device *my_device;
};

#define SYS_THR_METRICS_SCALING_FACTOR 100000

struct syscall_throttle_sleep_metrics {
	spinlock_t lock;
	unsigned long max_sleep;
	unsigned long avg_sleep;
	unsigned long units_passed;
};

extern struct syscall_throttle_context *st_cxt;
extern struct syscall_throttle_sleep_metrics *st_slp_met;

int load_throttle(void);
int load_hack_search(void);
int load_metrics(void);
void unload_metrics(void);
int load_timer(void);
void unload_timer(void);
int load_driver(void);
void unload_driver(void);
int load_monitor(void);
void unload_monitor(void);

void update_limit_and_wake(void);

void update_sleep_metrics(void);

int register_critical_num(unsigned int);
int unregister_critical_num(unsigned int);
int register_critical_str(char *, struct rhashtable *);
int unregister_critical_str(char *, struct rhashtable *);

int is_critical(int nr);

#endif