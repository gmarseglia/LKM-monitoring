#include "asm/preempt.h"
#include "linux/minmax.h"
#include "linux/printk.h"
#include "linux/timer.h"
#include "linux/types.h"
#include <linux/atomic.h>
#include <linux/delay.h>
#include <linux/irqflags.h>
#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/preempt.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/wait.h>

#define MODNAME "SYSCALL-THROTTLE"

#define LOG_FINE if (0)

#define target_func "x64_sys_call"
#define TIMER_INTERVAL 1000
#define CRITICAL_PER_UNIT 3

static struct kprobe the_probe;
static atomic_t crit_req = ATOMIC_INIT(0);
static atomic_t crit_lim = ATOMIC_INIT(CRITICAL_PER_UNIT);
static atomic_t crit_sleep = ATOMIC_INIT(0);
static atomic_t running = ATOMIC_INIT(1);

static DECLARE_WAIT_QUEUE_HEAD(my_wait_queue);
static struct timer_list my_timer;

DEFINE_PER_CPU(struct kprobe **, saved_kprobe_context_p);

static int __kprobes pre_handler_throttle(struct kprobe *p,
                                          struct pt_regs *regs) {

  if (atomic_read(&running) == 0)
    return 0;

  // #TODO: why orig_ax works?
  struct pt_regs *the_regs = (struct pt_regs *)regs->di;
  unsigned long sys_call_number = the_regs->orig_ax;

  if (preempt_count() == 0) {
    pr_alert("%s: found preemptable, aborting.", MODNAME);
    return 0;
  }

  if (irqs_disabled()) {
    pr_alert("%s: irqs are disabled, aborting.", MODNAME);
    return 0;
  }

  if (sys_call_number == 1 && (current->pid == 2673 || current->pid == 2683)) {
    int curr_req = atomic_fetch_inc(&crit_req);
    int curr_lim = atomic_read(&crit_lim);

    LOG_FINE pr_info("%s: probe #%05d hit, for pid %d, with ax=%lu", MODNAME,
                     curr_req, current->pid, sys_call_number);

    if (curr_req >= curr_lim) {
      LOG_FINE pr_info(
          "%s: probe #%05d must be delayed, curr_req=%d, curr_lim=%d", MODNAME,
          curr_req, curr_req, curr_lim);

      struct kprobe **kprobe_context_p = this_cpu_read(saved_kprobe_context_p);
      this_cpu_write(*kprobe_context_p, NULL);

      atomic_inc(&crit_sleep);
      // #TODO: preempt_enable_no_resched() could be better
      preempt_enable();

      wait_event_interruptible(my_wait_queue,
                               curr_req < atomic_read(&crit_lim) ||
                                   !atomic_read(&running));

      preempt_disable();
      atomic_dec(&crit_sleep);

      this_cpu_write(*kprobe_context_p, p);

      LOG_FINE pr_info("%s: probe #%05d has completed delay", MODNAME,
                       curr_req);
    }

    pr_info("%s: probe #%05d completed, for pid %d", MODNAME, curr_req,
            current->pid);
  }

  /* A pre_handler must return 0 unless it handles the fault itself */
  return 0;
}

static int load_throttle(void) {
  int ret;

  the_probe.symbol_name = target_func;
  the_probe.pre_handler = pre_handler_throttle;

  ret = register_kprobe(&the_probe);
  if (ret < 0) {
    pr_err("%s: register_kprobe failed, returned %d\n", MODNAME, ret);
    return ret;
  }

  return 0;
}

static void dummy_run(void *arg) {
  pr_info("%s: dummy running on CPU %d", MODNAME, smp_processor_id());
  return;
}

static int __kprobes pre_handler_search(struct kprobe *p,
                                        struct pt_regs *regs) {
  struct kprobe **temp = (struct kprobe **)&saved_kprobe_context_p;

  pr_info("%s: pre_handler_search running on CPU %d", MODNAME,
          smp_processor_id());

  while ((unsigned long)temp > 0) {
    temp--;
    if ((struct kprobe *)this_cpu_read(*temp) == p) {
      printk("%s: found on CPU %d at %p", MODNAME, smp_processor_id(), temp);
      this_cpu_write(saved_kprobe_context_p, temp);
      break;
    }
  }
  if (temp == 0) {
    pr_err("%s: NOT found on CPU %d", MODNAME, smp_processor_id());
    return 0;
  }

  struct kprobe **kprobe_context_p = this_cpu_read(saved_kprobe_context_p);
  struct kprobe *recovered_p = this_cpu_read(*kprobe_context_p);

  this_cpu_write(*kprobe_context_p, NULL);

  if (recovered_p == NULL) {
    pr_info("%s: recovered_p == NULL", MODNAME);
  } else {
    pr_info("%s: recovered_p->symbol_name=%s", MODNAME,
            recovered_p->symbol_name);
  }

  this_cpu_write(*kprobe_context_p, recovered_p);

  return 0;
}

static int load_hack(void) {
  int ret_search;
  struct kprobe search_probe;

  search_probe.symbol_name = "dummy_run";
  search_probe.pre_handler = pre_handler_search;

  ret_search = register_kprobe(&search_probe);
  if (ret_search < 0) {
    pr_err("%s: register_kprobe failed, returned %d\n", MODNAME, ret_search);
    return ret_search;
  }

  on_each_cpu(dummy_run, NULL, 1);

  pr_info("%s: load_hack completed.", MODNAME);

  unregister_kprobe(&search_probe);

  return 0;
}

static inline void wake_sleeping(void) {
  int curr_lim = atomic_read(&crit_lim);
  int curr_req = atomic_read(&crit_req);
  int new_lim = min(curr_lim, curr_req) + CRITICAL_PER_UNIT;
  atomic_set(&crit_lim, new_lim);

  if (new_lim != curr_lim)
    LOG_FINE pr_info("%s: curr_lim=%d, curr_req=%d, new_lim=%d", MODNAME,
                     curr_lim, curr_req, new_lim);

  wake_up_interruptible(&my_wait_queue);
}

static void timer_callback(struct timer_list *t) {
  wake_sleeping();

  // Re-arm the timer to fire again in 1 second
  mod_timer(&my_timer, jiffies + msecs_to_jiffies(TIMER_INTERVAL));
}

static int load_timer(void) {
  timer_setup(&my_timer, timer_callback, 0);

  mod_timer(&my_timer, jiffies + msecs_to_jiffies(TIMER_INTERVAL));

  return 0;
}

static int initfn(void) {
  int ret;

  pr_info("%s: module correctly loaded\n", MODNAME);

  ret = load_hack();
  if (ret > 0)
    return ret;

  ret = load_timer();
  if (ret > 0)
    return ret;

  ret = load_throttle();
  if (ret > 0)
    return ret;

  return 0;
}

static void exitfn(void) {
  atomic_set(&running, 0);

  unregister_kprobe(&the_probe);
  pr_info("%s: kprobe at %p unregistered\n", MODNAME, the_probe.addr);

  del_timer_sync(&my_timer);
  pr_info("%s: timer unregistered\n", MODNAME);

  wake_sleeping();
  while (atomic_read(&crit_sleep) != 0)
    msleep(20);
  pr_info("%s: all sleeping thread have completed\n", MODNAME);

  pr_info("%s: module correctly unloaded\n", MODNAME);
}

module_init(initfn);
module_exit(exitfn);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Giuseppe Marseglia <g.marseglia.it@gmail.com>");
MODULE_DESCRIPTION("See README.md");
