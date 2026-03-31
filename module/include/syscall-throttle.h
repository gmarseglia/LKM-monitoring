#ifndef _SYSCALL_THROTTLE_H_
#define _SYSCALL_THROTTLE_H_

#include <asm-generic/errno-base.h>
#include <asm/preempt.h>
#include <linux/atomic.h>
#include <linux/bitmap.h>
#include <linux/compiler.h>
#include <linux/compiler_attributes.h>
#include <linux/cred.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/gfp_types.h>
#include <linux/irqflags.h>
#include <linux/jhash.h>
#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/ktime.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/percpu-defs.h>
#include <linux/preempt.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/rcutree.h>
#include <linux/rhashtable-types.h>
#include <linux/rhashtable.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/seqlock.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/spinlock_types.h>
#include <linux/sprintf.h>
#include <linux/stddef.h>
#include <linux/timer.h>
#include <linux/types.h>
#include <linux/uidgid.h>
#include <linux/wait.h>

#define __ST_MODNAME "SYSCALL-THROTTLE"

#define __ST_LOG if (1)
#define __ST_LOG_FINE if (0)
#define __ST_LOG_FINEST if (0)

#define __ST_DISPATCHER_SYMBOL_NAME "x64_sys_call"
#define __ST_TIMER_INTERVAL 1000
#define __ST_BASE_CRIT_LIMIT 1

#define __ST_MAX_NR 323
#define __ST_MAX_STR_LEN 64
#define __ST_METRICS_SCALING_FACTOR 100000

#define __ST_FLAG_ON 0
#define __ST_FLAGS &st_cxt->flags
#define __ST_IS_ON test_bit(__ST_FLAG_ON, __ST_FLAGS)

DECLARE_PER_CPU(struct syscall_throttle_delay_metrics, st_dly_met);

struct string_entry {
	char string_key[__ST_MAX_STR_LEN];
	struct rhash_head linkage;
	struct rcu_head rcu;
};

struct syscall_throttle_context {
	/* General purpose */
	unsigned long flags;

	/* For interruptible kprobes */
	atomic_t hack_ready_on_cpu;
	unsigned long saved_kprobe_ctx_offset;

	/* For monitoring */
	struct mutex registry_mutex;
	unsigned long *nr_registry;
	struct rhashtable euid_registry;
	struct rhashtable prog_names_registry;

	/* For throttling */
	struct kprobe probe_throttle;
	atomic_t crit_limit;
	atomic_t crit_req;
	atomic_t crit_avail;
	atomic_t crit_sleep;

	/* For sleep/wakeup */
	struct timer_list periodic_timer;
	wait_queue_head_t critical_sleeping_wq;

	/* For ioctl driver */
	int Major;
	struct class *my_class;
	struct device *my_device;

	/* For procfs driver */
	struct proc_dir_entry *proc_dir;
};

struct syscall_throttle_sleep_metrics {
	spinlock_t lock;
	unsigned long max_sleep;
	unsigned long avg_sleep;
	unsigned long units_passed;
};

struct syscall_throttle_query_result {
	bool is_critical;
	int nr;
	char *type;
	char euid[__ST_MAX_STR_LEN];
	char name[__ST_MAX_STR_LEN];
};

struct syscall_throttle_delay_metrics {
	seqcount_t count;
	s64 max_delay_ms;
	struct syscall_throttle_query_result qr;
};

extern struct syscall_throttle_context *st_cxt;
extern struct syscall_throttle_sleep_metrics *st_slp_met;

int load_throttle(void);
int load_hack_search(void);
int load_metrics(void);
void unload_metrics(void);
int load_metrics_driver(void);
void unload_metrics_driver(void);
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

bool is_critical(struct syscall_throttle_query_result *);

#endif