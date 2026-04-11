# LKM-monitoring
A Linux Kernel Module (LKM) implementing a system call throttling mechanism.

The specification of the LKM can be found at this [link](https://francescoquaglia.github.io/TEACHING/AOS/CURRENT/PROJECTS/project-specification-2025-2026.html).

## 1 Basic usage 

### 1.1 Compilation and installation

The project is divided into kernel-space (`module/`) and user-space (`user/`) components.

To compile and install the kernel module:
```sh
cd module/
make all
sudo insmod syscall-throttle.ko
```

To compile the user components:
```sh
cd user/
make all
```
This produces three executables: `user.out`, `ioctl.out`, and `stress.out`.

### 1.2 Configuration via `ioctl`

The device driver exposes a character device at `/dev/sys_thr`.

The compiled `ioctl.out` utility can be used to configure the throttling monitor dynamically at runtime.
```sh
./ioctl.out <command_id> [parameter]
```

| Command  | command_id | parameter |
| - | - | - |
| **Start throttle** | `0` | NA |
| **Stop throttle** | `1` | NA |
| **Register syscall** | `2` | `(int) nr` |
| **Unregister syscall** | `3` | `(int) nr` |
| **Register eUID** | `4` | `(string) eUID` |
| **Unregister eUID** | `5` | `(string) eUID` |
| **Register program name** | `6` | `(string) prog_name` |
| **Unregister program name** | `7` | `(string) prog_name` |
| **Set limit** | `8` | `(int) limit` |


For example to set the limit of the `read` for the program `user.out`:
```
ioctl.out 2 0
ioctl.out 6 user.out
ioctl.out 0
```

### 1.3 Observability via `procfs`

The module exports real-time metrics and configurations to the `/proc/SYSCALL-THROTTLE` directory.
These values can be inspected using `cat`:

| File | Content |
| - | - |
| `config` | If the monitor is running and the current limit. |
| `nr` | Registered syscall numbers. |
| `euid` | Registered eUIDs. |
| `program_names` | Registered program names. |
| `sleep_metrics` | The max and average number of threads that had to be put to sleep. |
| `delay_metrics` | The peak delay experienced by an actual system call execution and the program/user responsible. |

### 1.4 Testing utilities

Two test programs are provided:

`user.out`: Continuously performs `read()` calls from `/dev/null` in a loop. Allows to interrupt with `SIGINT` up to 3 times.

`stress.out`: Executes a loop of 1M reads from `/dev/null` and calculates the percentiles of execution times. This program enables the observation of the injected delay.

## 2. Implementation details

### 2.1 Syscall interception

System call invocations are intercepted using a `kprobe` installed on the symbol `x64_sys_call`, which is the final step of the dispatcher (it contains the `switch` structure with the various system calls functions).

```c
int load_throttle(void)
{
	...
	st_cxt->probe_throttle.symbol_name = "x64_sys_call";
	st_cxt->probe_throttle.pre_handler = pre_handler_throttle;

	ret = register_kprobe(&st_cxt->probe_throttle);
	...
}
```

Using a pre-handler allows to:
- **cleanly install/remove the throttling mechanism**.
- check the arguments passed to the dispatcher, such as the `nr` which identifies the desired system call.
- identify the critical system call invocations.
- apply the delay needed to respect the limit of critical invocations per second.

### 2.2 Critical syscall identification

An intercepted system call invocation is considered critical if:
```
nr is registered && (euid is registered || prog_name is registered)
```

A simplified snippet of code showing the usage is shown below:
```c
static int __kprobes pre_handler_throttle(struct kprobe *p, struct pt_regs *regs)
{
	...
	if (unlikely(is_critical(&st_qr))) {
		...
        /* Block if necessary */
	}
    return 0;
}

inline bool is_critical(struct syscall_throttle_query_result *st_qr)
{
	st_qr->is_critical = false;

	/* Check syscall */
	if (!test_bit(nr &__ST_MAX_NR_MASK, st_cxt->nr_registry)) {
		return false;
	}

	/* Get program name */
    ...
    strscpy(st_qr->name,current->comm, __ST_MAX_STR_LEN);
    if (is_registered_str(st_qr->name, &st_cxt->prog_names_registry)) {
        st_qr->is_critical = true;
    }
	

	/* Check eUID */
	kuid_t current_kuid = current_euid();
	uid_t euid_val = from_kuid(&init_user_ns, current_kuid);
	snprintf(st_qr->euid, __ST_MAX_STR_LEN, "%u", euid_val);
    if (is_registered_str(st_qr->euid, &st_cxt->euid_registry)) {
        st_qr->is_critical = true;
    }

	return st_qr->is_critical;
}
```

The implementation of the registries can be found in `module/src/monitor.c`, and in particular:
- the `nr_registry` is implemented via **bitmap**, which allows atomic concurrent-safe operations like `set_bit`, `clear_bit` and `test_bit`.
- the `prog_names_registry` and `euid_registry` are implemented via **rhashtable** which allows to use RCU concurrent-safe operations like `rhashtable_lookup_fast`, `rhashtable_insert_fast`, `rhashtable_remove_fast` and the `rhashtable_walk_*`.


### 2.3 Critical syscall limiting

When a critical system call invocation is found, the pre-handler tries to decrement a atomic counter, `atomic_t crit_avail`.

```c
static int __kprobes pre_handler_throttle(struct kprobe *p, struct pt_regs *regs)
{
	...
	if (unlikely(is_critical(&st_qr))) {
		...
        /* If curr_avail < 0 ==> syscall has to be delayed */
		if (atomic_dec_return(&st_cxt->crit_avail) < 0){
            ...
            /* Block */
        }
	}
    return 0;
}
```

The atomic counter `crit_avail` is "refilled" by a SoftIRQ timer BH (`TIMER_SOFTIRQ`) that runs approximately every 1 seconds:
```c
static void timer_callback(struct timer_list *t)
{
    ...
	update_limit_and_wake();

	/* Re-arm the timer to fire again in __ST_TIMER_INTERVAL milliseconds */
	mod_timer(&st_cxt->periodic_timer,
		  jiffies + msecs_to_jiffies(__ST_TIMER_INTERVAL));
}

void update_limit_and_wake(void)
{
	/* Updates the available token, as the current limit */
	atomic_set(&st_cxt->crit_avail, atomic_read(&st_cxt->crit_limit));
	...
}
```

### 2.4 Critical syscall blocking

In order to block critical system call invocations that do not find available tokens in the counter, a **wait event queue** is used.

```c
static int __kprobes pre_handler_throttle(struct kprobe *p, struct pt_regs *regs)
{
	...
	if (unlikely(is_critical(&st_qr))) {
		...
        /* If curr_avail < 0 ==> syscall has to be delayed */
		if (atomic_dec_return(&st_cxt->crit_avail) < 0){
            ...
            ret = wait_event_interruptible(
				st_cxt->critical_sleeping_wq,
				atomic_dec_return(&st_cxt->crit_avail) >= 0 || !__ST_IS_ON);
			...
        }
	}
    return 0;
}

/* Invoked by the timer */
void update_limit_and_wake(void)
{
	/* Updates the available token, as the current limit */
	atomic_set(&st_cxt->crit_avail, atomic_read(&st_cxt->crit_limit));
	
	/* Wakes up the event wait queue */
	wake_up_interruptible(&st_cxt->critical_sleeping_wq);
}
```

In particular the wake up condition is composed by two parts:
- `atomic_dec_return(&st_cxt->crit_avail) >= 0`: this condition allows only at most `crit_avail` sleeping threads to "really" wake up, **avoiding the thundering herd effect**.
- The macro `__ST_IS_ON` expands to `test_bit(__ST_FLAG_ON, __ST_FLAGS)`: this condition allows to wake up all the threads if the flag `__ST_FLAG_ON` is set to `false`. This is used when shutting down the throttling mechanism.

#### 2.4.1 Hacked preemptible `kprobe`

Natively the `kprobe` pre-handler runs in an **unpreemptible context**, which does not allow to call blocking APIs like `wait_event_interruptible`. Since these are mandatory to ensure the functionality of the throttling mechanism, an **hack** has been integrated that allows the `kprobe` pre-handler to call blocking APIs.

The hack involves the following steps:

1. Find the offset in the per-CPU memory layout corresponding to the `kprobe` context and save it (this is the same for all CPUs).
This has been implemented, in `kprobe` pre-handler, so that the brute-force search can search for its own known `kprobe` context.

```c
/* Pre-handler for searching the offset in the per-CPU memory corresponding to
 * the kprobe context */
static int __kprobes pre_handler_search(struct kprobe *p, struct pt_regs *regs)
{
	/* Brute force search for the position of the kprobe context */
	/* copy_from_kernel_nofault allows to avoid kernel panic */
	struct kprobe **temp_offset = 0;
	struct kprobe *temp;
	while (copy_from_kernel_nofault(&temp, this_cpu_ptr(temp_offset), sizeof(struct kprobe *)) != -EFAULT) {
		/* Check if **temp_offset points at the variable representing the kprobe context  */
		if (temp == p) {
			st_cxt->saved_kprobe_ctx_offset = temp_offset;
			atomic_inc(&st_cxt->hack_ready_on_cpu);
			return 0;
		}
		temp_offset++;
	}

	/* Not found, handle */
	...
}

int try_hack_search(void)
{
	int ret;
	struct kprobe search_probe;

	/* Initialize and register the search probe */
	search_probe.addr = (kprobe_opcode_t *)dummy_run;
	search_probe.pre_handler = pre_handler_search;

	ret = register_kprobe(&search_probe);
	if (ret < 0) {
		...
	}

	/* Execute "dummy_run" to trigger the search probe pre-handler */
	dummy_run(NULL);
	...
}
```

2. In the pre-handler, before calling a blocking APIs, write `NULL` in the `kprobe` context to hide it, and then enable the preemption.

3. In the pre-handler, after calling a blocking APIs, disable the preemption and re-write the current `kprobe` context (the one passed as argument) in the `kprobe` context.

```c
static int __kprobes pre_handler_throttle(struct kprobe *p, struct pt_regs *regs)
{
	...
	/* Write NULL in the kprobe context in the per-CPU memory */
	this_cpu_write(*st_cxt->saved_kprobe_ctx_offset, NULL);

	/* Enable preemption */
	preempt_enable();

	/* Go to sleep, and wake up when under limit or when
		* throttling is off */
	ret = wait_event_interruptible(
		st_cxt->critical_sleeping_wq,
		atomic_dec_return(&st_cxt->crit_avail) >= 0 || !__ST_IS_ON);

	/* Disable premption */
	preempt_disable();

	/* Restore kprobe context */
	this_cpu_write(*st_cxt->saved_kprobe_ctx_offset, p);
	...
}
```

### 2.5 Metrics gathering

Two types of metrics are gathered:

1. Sleep-based metrics: `max` and `avg` number of threads sleeping for each time unit.
These are simply collected during the timer BH, before waking up the threads.

2. Delay-based metrics: the `max` delay in $\text{msec}$ experienced by a system call invocations, with the relative `nr`, `program_name`, `euid` and which one triggered the critical status.
These have to be collected and compared for each critical system call that has been delayed, and are collected in the dispatcher pre-handler.

For the delay-based metrics, since the writers are in in the critical path of the system call, a mechanism implemented using per-CPU memory and sequece counter (`seqcount_t`) allows to reduce the time spent waiting for a lock.

```c
struct syscall_throttle_delay_metrics {
	seqcount_t count;
	s64 max_delay_ms;
	struct syscall_throttle_query_result qr;
};

DEFINE_PER_CPU(struct syscall_throttle_delay_metrics, st_dly_met);

static int __kprobes pre_handler_throttle(struct kprobe *p, struct pt_regs *regs)
{
	...
	/* Update per-CPU metrics */
	delay_ms = ktime_ms_delta(ktime_get(), start);
	target_dm = this_cpu_ptr(&st_dly_met);
	if (delay_ms > target_dm->max_delay_ms) {
		write_seqcount_begin(&target_dm->count);

		target_dm->max_delay_ms = delay_ms;
		memcpy(&target_dm->qr, &st_qr,
				sizeof(struct
					syscall_throttle_query_result));

		write_seqcount_end(&target_dm->count);
	}
	...
}

static int delay_metrics_show(struct seq_file *m, void *v)
{
	...
	/* Find the global max from the local max */
	max_global_dm.max_delay_ms = -1;
	for_each_possible_cpu(cpu)
	{
		do {
			cpu_dm = per_cpu_ptr(&st_dly_met, cpu);
			seq = read_seqcount_begin(&cpu_dm->count);
			memcpy(&max_local_dm, cpu_dm, sizeof(struct syscall_throttle_delay_metrics));
		} while (read_seqcount_retry(&cpu_dm->count, seq));

		if (max_local_dm.max_delay_ms > max_global_dm.max_delay_ms) {
			memcpy(&max_global_dm, &max_local_dm, sizeof(struct syscall_throttle_delay_metrics));
		}
	}
	...
}
```

### 2.6 Misc

#### 2.6.1 Signal handling

Due to the use of `wait_event_interruptible` if a thread is hit by a signal, it wakes up from the queue.
In this case, using a `x86-64` specific register manipulation, the "real" dispatcher invocation is discarded and a return value is returned to the caller.

```c
static int __kprobes pre_handler_throttle(struct kprobe *p, struct pt_regs *regs)
{
	ret = wait_event_interruptible(
	st_cxt->critical_sleeping_wq,
	atomic_dec_return(&st_cxt->crit_avail) >= 0 || !__ST_IS_ON);

	...

	if (ret == 0) {
		return 0;
	} else {
		// get return address from stack
		unsigned long ret_addr = *(unsigned long *)regs->sp;
		// change instruction pointer
		regs->ip = ret_addr;
		// simulate pop
		regs->sp += sizeof(long);
		// returns -EPERM
		regs->ax = -EPERM;
		// skip instruction single-stepping
		return 1;
	}
}
```

In this way, a malicious actor can not hit repeatedly a thread with a signal to make it avoid the critical limit. 

#### 2.6.2 `rmmod` handling

In order to **avoid kernel panics** it's mandatory that the module does not try to remove the `kprobe` pre-handler of the dispatcher while a thread is sleeping in the wait event queue.
Under standard conditions, probes cannot be removed during execution; however, because this specific implementation enables preemption, a manual safety mechanism is required to prevent premature removal and subsequent kernel panics.

```c
static int __kprobes pre_handler_throttle(struct kprobe *p, struct pt_regs *regs)
{
	...
	atomic_inc(&st_cxt->crit_sleep);
	...
	/* Enable preemption */
	preempt_enable();
	...
	/* Disable premption */
	preempt_disable();
	...
	atomic_dec(&st_cxt->crit_sleep);
	...
}

void unload_throttle(void)
{
	/* Wake up possible sleeping thread. running==0 makes all threads wake up */
	clear_bit(__ST_FLAG_ON, __ST_FLAGS);
	wake_up_interruptible(&st_cxt->critical_sleeping_wq);

	/* Wait for all thread to exit the  */
	while (atomic_read(&st_cxt->crit_sleep) != 0)
		msleep(20);

	/* Unregister the kprobe */
	unregister_kprobe(&st_cxt->probe_throttle);
}
```

A future work can be to switch this `msleep` mechanism to a more sophisticated `wait_for_completion` based one.

#### 2.6.3 `procfs` with `seq_file` magic

In order to display the registered eUIDs and program names in `procfs`, the `seq_file` API has been implemented together with the `rhashtable_walk_*` functions of the rhashtable, to ensure respect of the RCU and to allow to display an arbitrary amount of characters.

The `seq_file` exposes these function:
```c
static struct seq_operations rhash_seq_ops = {
	.start = rhash_seq_start,
	.show = rhash_seq_show,
	.next = rhash_seq_next,
	.stop = rhash_seq_stop,
};
```

These can be bound to the driver of a `procfs` file in the following way:
```c
static struct proc_ops rhash_proc_ops = {
	.proc_open = rhash_seq_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = rhash_seq_release
};

static int rhash_seq_open(struct inode *inode, struct file *file)
{
	struct rhash_seq_state *state;
	struct rhashtable *ht;

	/* Allocate state */
	state = __seq_open_private(file, &rhash_seq_ops, sizeof(*state));
	...
}
```

This allows to have 6 operations available, which are called as the following scheme:
- Read operations are wrapped by `proc_open` and `proc_release` calls, executing them in a strict head-to-tail order.
	- To manage the rhashtable walk lifecycle, `rhashtable_walk_enter` and `rhashtable_walk_exit` should be called here to handle the setup and teardown.

- Internally `proc_read` and `proc_llseek` call the `seq_operations` in the following scheme: each sequence starts with `start` and ends with `stop`, and inside `show` and `next` are alternated. If the character to print exceeds the allocated buffer, it automatically calls `stop`, allocate more space and resume with a `start` (possibly sleeping in the meantime).
	- Regarding the rhashtable walk lifecycle, in `start` the `rhashtable_walk_start` takes the RCU read lock, while in `stop` the `rhashtable_walk_stop` release it. This ensures that no element are freed while walking the hash table.
	- In `next` the `rhashtable_walk_next` allow to get the next element in the hashtable.
	-  When resuming from a `start` a `rhashtable_walk_peek` allows to resume the walk. If the element has been freed in the meantime then it restarts, meaning that entries are displayed at least once.

## 3. Performance analysis

The following table shows the impact of the LKM in term of performance for non-critical system call invocations.

| Test | p50 $\text{nsec}$ | p90 $\text{nsec}$ | p99 $\text{nsec}$ |
| - | - | - | - |
| Module not inserted | $454$ | $483$ | $532$ |
| Module inserted, is off | $489$ | $519$ | $583$ |
| Module inserted, is on, nr is not registered | $491$ | $523$ | $588$ |
| Module inserted, is on, nr is registered, euid and program name are not registered (0 registered each) | $542$ | $577$ | $690$ |
| Module inserted, is on, nr is registered, euid and program name are not registered (512 registered each) | $553$ | $588$ | $725$ |

## 4. Future works

### 4.1 Avoiding hacked `kprobe`

A proof-of-concept alternative throttling mechanism has been implemented in the `experimental` branch, in order to **avoid hacking the preemptible `kprobe`**.

This mechanism is based on the machine-dependent capabilities of the pre-handler to alter the CPU registry, in particular the Instruction Pointer (`ip`) register, by modifying the `pt_regs`.

```c
static noinline void throttle(void)
{
	// POC
	msleep(1000);
	return;
}

static __attribute__((naked)) noinline void dispatcher_inner_pre_handler(void)
{
	// Save the register state on the stack
	asm volatile("push %rdi\n\t"
		     "push %rsi\n\t"
		     "push %rdx\n\t"
		     "push %rcx\n\t"
		     "push %rax\n\t"
		     "push %r8\n\t"
		     "push %r9\n\t"
		     "push %r10\n\t"
		     "push %r11\n\t"
		     "push %rbx\n\t"
		     "push %rbp\n\t"
		     "push %r12\n\t"
		     "push %r13\n\t"
		     "push %r14\n\t"
		     "push %r15\n\t");
	// Call the function that will do the blocking and metrics --> this preservers the stack
	asm volatile("call *%0" : : "r"(throttle));
	// Recover the register state
	asm volatile("pop %r15\n\t"
		     "pop %r14\n\t"
		     "pop %r13\n\t"
		     "pop %r12\n\t"
		     "pop %rbp\n\t"
		     "pop %rbx\n\t"
		     "pop %r11\n\t"
		     "pop %r10\n\t"
		     "pop %r9\n\t"
		     "pop %r8\n\t"
		     "pop %rax\n\t"
		     "pop %rcx\n\t"
		     "pop %rdx\n\t"
		     "pop %rsi\n\t"
		     "pop %rdi\n\t");
	// Jump to the dispatcher function body, avoiding the initial JMP instruction installed by the probe
	asm volatile("jmp *%0" : : "r"(__st_dispatcher_addr + 5));
}

static int __kprobes pre_handler_throttle(struct kprobe *p, struct pt_regs *regs)
{
	...
	if (unlikely(is_critical(&st_qr))) {
		regs->ip = (unsigned long)dispatcher_inner_pre_handler;
		return 1;
	}
	...
}
```