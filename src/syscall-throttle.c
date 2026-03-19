#include <asm/preempt.h>
#include <linux/compiler.h>
#include <linux/minmax.h>
#include <linux/printk.h>
#include <linux/timer.h>
#include <linux/types.h>
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
#define LOG_FINEST if (0)

#define dispatcher_symbol_name "x64_sys_call"
#define TIMER_INTERVAL 1000
#define CRITICAL_PER_UNIT 3

static struct kprobe probe_throttle;

static atomic_t hack_ready_on_cpu = ATOMIC_INIT(0);
static atomic_t crit_req = ATOMIC_INIT(0);
static atomic_t crit_lim = ATOMIC_INIT(CRITICAL_PER_UNIT);
static atomic_t crit_sleep = ATOMIC_INIT(0);
static atomic_t running = ATOMIC_INIT(1);

static DECLARE_WAIT_QUEUE_HEAD(critical_sleeping_wq);
static struct timer_list my_timer;

DEFINE_PER_CPU(struct kprobe **, saved_kprobe_context_p);

/*
  Checks if the syscall request is critical
*/
static inline int is_critical(int sys_call_number) {
  return (sys_call_number == 1 &&
          (current->pid == 2673 || current->pid == 2683));
}

/*
  Pre-handler for the dispatcher:
    1. Checks if the syscall request is critical
    2. If it's critical, checks the limit
    3. If it's critical and over limit, applies delay
*/
static int __kprobes pre_handler_throttle(struct kprobe *p,
                                          struct pt_regs *regs) {

  /* Check if the module is still running */
  if (atomic_read(&running) == 0)
    return 0;

  struct pt_regs *the_regs = (struct pt_regs *)regs->di;
  unsigned long sys_call_number =
      the_regs->orig_ax; // #TODO: why orig_ax works?

  /* Sanity check for preemption and interrupts */
  if (preempt_count() == 0) {
    pr_alert("%s: found preemptable, aborting.", MODNAME);
    return 0;
  }
  if (irqs_disabled()) {
    pr_alert("%s: irqs are disabled, aborting.", MODNAME);
    return 0;
  }

  /* If syscall request is critical */
  if (unlikely(is_critical(sys_call_number))) {
    int curr_req = atomic_fetch_inc(&crit_req);
    int curr_lim = atomic_read(&crit_lim);

    LOG_FINE pr_info("%s: probe #%05d hit, for pid %d, with ax=%lu", MODNAME,
                     curr_req, current->pid, sys_call_number);

    /* If syscall request is critical and over limit */
    if (curr_req >= curr_lim) {
      LOG_FINE pr_info(
          "%s: probe #%05d must be delayed, curr_req=%d, curr_lim=%d", MODNAME,
          curr_req, curr_req, curr_lim);

      /* Write NULL in the kprobe context in the per-CPU memory */
      struct kprobe **kprobe_context_p = this_cpu_read(saved_kprobe_context_p);
      this_cpu_write(*kprobe_context_p, NULL);

      /* Enable preemption */
      atomic_inc(&crit_sleep);
      // #TODO: preempt_enable_no_resched() could be better
      preempt_enable();

      /* Go to sleep until under limit or when removing the module */
      wait_event_interruptible(critical_sleeping_wq,
                               curr_req < atomic_read(&crit_lim) ||
                                   !atomic_read(&running));

      /* Disable premption */
      preempt_disable();
      atomic_dec(&crit_sleep);

      /* Restore kprobe context */
      this_cpu_write(*kprobe_context_p, p);

      LOG_FINE pr_info("%s: probe #%05d has completed delay", MODNAME,
                       curr_req);
    }

    pr_info("%s: probe #%05d completed, for pid %d", MODNAME, curr_req,
            current->pid);
  }

  return 0;
}

/*
  Install the throttle pre-handler on the dispacther
*/
static int load_throttle(void) {
  int ret;

  probe_throttle.symbol_name = dispatcher_symbol_name;
  probe_throttle.pre_handler = pre_handler_throttle;

  ret = register_kprobe(&probe_throttle);
  if (ret < 0) {
    pr_err("%s: register_kprobe failed, returned %d\n", MODNAME, ret);
    return ret;
  }

  return 0;
}

/*
  Dummy function to be run on each CPU, that allow the pre_handler_search to be
  run inside a kprobe
*/
static void dummy_run(void *arg) {
  LOG_FINEST pr_info("%s: dummy running on CPU %d", MODNAME,
                     smp_processor_id());
  return;
}

/*
  Pre-handler for searching the position of the kprobe context in the per-CPU
  memory and saving it in the 'saved_kprobe_context_p' per-CPU variable
*/
static int __kprobes pre_handler_search(struct kprobe *p,
                                        struct pt_regs *regs) {

  pr_info("%s: pre_handler_search running on CPU %d", MODNAME,
          smp_processor_id());

  /* Brute force search for the position of the kprobe context */
  struct kprobe **temp = (struct kprobe **)&saved_kprobe_context_p;
  while ((unsigned long)temp > 0) {
    temp--;
    // current kprobe context is p
    if ((struct kprobe *)this_cpu_read(*temp) == p) {
      printk("%s: found on CPU %d at %p", MODNAME, smp_processor_id(), temp);
      this_cpu_write(saved_kprobe_context_p, temp);
      atomic_inc(&hack_ready_on_cpu);
      break;
    }
  }
  if (temp == 0) {
    pr_err("%s: NOT found on CPU %d", MODNAME, smp_processor_id());
    return 0;
  }

  return 0;
}

/*
  Performs the preparatory steps for the preemptable kprobe hack:
    1. Register a kprobe to be run on each CPU
    2. Run pre_handler_search on each CPU
    3. On each CPUs, the pre-handler saves the position of the kprobe context
    4. Unregister the kprobe
*/
static int load_hack_search(void) {
  int ret;

  /* Initialize and register the search probe */
  struct kprobe search_probe;
  search_probe.symbol_name = "dummy_run";
  search_probe.pre_handler = pre_handler_search;

  ret = register_kprobe(&search_probe);
  if (ret < 0) {
    pr_err("%s: register_kprobe failed, returned %d\n", MODNAME, ret);
    return ret;
  }

  /* Execute "dummy_run" on each CPU to trigger the search probe pre-handler */
  on_each_cpu(dummy_run, NULL, 1);
  if (atomic_read(&hack_ready_on_cpu) < num_online_cpus()) {
    pr_err("%s: load_hack_search did not complete on every CPU.", MODNAME);
  }

  /* Unregister the search probe */
  unregister_kprobe(&search_probe);

  pr_info("%s: load_hack_search completed.", MODNAME);

  return 0;
}

/*
  Perform the periodic update to handle limit:
    1. Updates the current limit
    2. Wakes up the the critical sleeping event wait queue
*/
static inline void update_limit_and_wake(void) {

  /* Updates the limits according the number of requests */
  int curr_lim = atomic_read(&crit_lim);
  int curr_req = atomic_read(&crit_req);
  int new_lim = min(curr_lim, curr_req) + CRITICAL_PER_UNIT;
  atomic_set(&crit_lim, new_lim);

  if (new_lim != curr_lim)
    LOG_FINE pr_info("%s: curr_lim=%d, curr_req=%d, new_lim=%d", MODNAME,
                     curr_lim, curr_req, new_lim);

  /* Wakes up the event wait queue */
  wake_up_interruptible(&critical_sleeping_wq);
}

/*
  The timer callback executed periodically:
    1. Calls the update_limit_and_wake routine
    2. Re-arms the timer
*/
static void timer_callback(struct timer_list *t) {
  update_limit_and_wake();

  /* Re-arm the timer to fire again in TIMER_INTERVAL milliseconds */
  mod_timer(&my_timer, jiffies + msecs_to_jiffies(TIMER_INTERVAL));
}

/*
  Load the periodic timer
*/
static int load_timer(void) {
  timer_setup(&my_timer, timer_callback, 0);
  mod_timer(&my_timer, jiffies + msecs_to_jiffies(TIMER_INTERVAL));
  return 0;
}

/*
  Module init function
*/
static int initfn(void) {
  int ret;

  pr_info("%s: module correctly loaded\n", MODNAME);

  ret = load_hack_search();
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

/*
  Module exit function
*/
static void exitfn(void) {
  /*
    Set atomic variable to general indication of running 0 indicates that the
    module is starting tear down operations
  */
  atomic_set(&running, 0);

  /* Delete the timer */
  del_timer_sync(&my_timer);
  pr_info("%s: timer unregistered\n", MODNAME);

  /*
    Wake up possible sleeping thread.
    running==0 makes all threads wake up
  */
  update_limit_and_wake();

  /* Wait for all thread to exit the  */
  while (atomic_read(&crit_sleep) != 0)
    msleep(20);
  pr_info("%s: all sleeping thread have completed\n", MODNAME);

  /* Unregister the kprobe */
  unregister_kprobe(&probe_throttle);
  pr_info("%s: kprobe at %p unregistered\n", MODNAME, probe_throttle.addr);

  pr_info("%s: module correctly unloaded\n", MODNAME);
}

module_init(initfn);
module_exit(exitfn);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Giuseppe Marseglia <g.marseglia.it@gmail.com>");
MODULE_DESCRIPTION("See README.md");
