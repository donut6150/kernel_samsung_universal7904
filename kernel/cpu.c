/* CPU control.
 * (C) 2001, 2002, 2003, 2004 Rusty Russell
 *
 * This code is licenced under the GPL.
 */
#include <linux/proc_fs.h>
#include <linux/smp.h>
#include <linux/init.h>
#include <linux/notifier.h>
#include <linux/sched.h>
#include <linux/sched/smt.h>
#include <linux/unistd.h>
#include <linux/cpu.h>
#include <linux/oom.h>
#include <linux/rcupdate.h>
#include <linux/export.h>
#include <linux/bug.h>
#include <linux/kthread.h>
#include <linux/stop_machine.h>
#include <linux/mutex.h>
#include <linux/gfp.h>
#include <linux/suspend.h>
#include <linux/lockdep.h>
#include <linux/tick.h>
#include <linux/irq.h>
#include <linux/cpuidle.h>
#include <trace/events/power.h>

#include <trace/events/sched.h>

#include "smpboot.h"

#ifdef CONFIG_SMP
/* Serializes the updates to cpu_online_mask, cpu_present_mask */
static DEFINE_MUTEX(cpu_add_remove_lock);

/*
 * The following two APIs (cpu_maps_update_begin/done) must be used when
 * attempting to serialize the updates to cpu_online_mask & cpu_present_mask.
 * The APIs cpu_notifier_register_begin/done() must be used to protect CPU
 * hotplug callback (un)registration performed using __register_cpu_notifier()
 * or __unregister_cpu_notifier().
 */
void cpu_maps_update_begin(void)
{
	mutex_lock(&cpu_add_remove_lock);
}
EXPORT_SYMBOL(cpu_notifier_register_begin);

void cpu_maps_update_done(void)
{
	mutex_unlock(&cpu_add_remove_lock);
}
EXPORT_SYMBOL(cpu_notifier_register_done);

static RAW_NOTIFIER_HEAD(cpu_chain);
static RAW_NOTIFIER_HEAD(cpus_chain);

/* If set, cpu_up and cpu_down will return -EBUSY and do nothing.
 * Should always be manipulated under cpu_add_remove_lock
 */
static int cpu_hotplug_disabled;

#ifdef CONFIG_HOTPLUG_CPU

static struct {
	struct task_struct *active_writer;
	/* wait queue to wake up the active_writer */
	wait_queue_head_t wq;
	/* verifies that no writer will get active while readers are active */
	struct mutex lock;
	/*
	 * Also blocks the new readers during
	 * an ongoing cpu hotplug operation.
	 */
	atomic_t refcount;

#ifdef CONFIG_DEBUG_LOCK_ALLOC
	struct lockdep_map dep_map;
#endif
} cpu_hotplug = {
	.active_writer = NULL,
	.wq = __WAIT_QUEUE_HEAD_INITIALIZER(cpu_hotplug.wq),
	.lock = __MUTEX_INITIALIZER(cpu_hotplug.lock),
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	.dep_map = {.name = "cpu_hotplug.lock" },
#endif
};

/* Lockdep annotations for get/put_online_cpus() and cpu_hotplug_begin/end() */
#define cpuhp_lock_acquire_read() lock_map_acquire_read(&cpu_hotplug.dep_map)
#define cpuhp_lock_acquire_tryread() \
				  lock_map_acquire_tryread(&cpu_hotplug.dep_map)
#define cpuhp_lock_acquire()      lock_map_acquire(&cpu_hotplug.dep_map)
#define cpuhp_lock_release()      lock_map_release(&cpu_hotplug.dep_map)


void get_online_cpus(void)
{
	might_sleep();
	if (cpu_hotplug.active_writer == current)
		return;
	cpuhp_lock_acquire_read();
	mutex_lock(&cpu_hotplug.lock);
	atomic_inc(&cpu_hotplug.refcount);
	mutex_unlock(&cpu_hotplug.lock);
}
EXPORT_SYMBOL_GPL(get_online_cpus);

void put_online_cpus(void)
{
	int refcount;

	if (cpu_hotplug.active_writer == current)
		return;

	refcount = atomic_dec_return(&cpu_hotplug.refcount);
	if (WARN_ON(refcount < 0)) /* try to fix things up */
		atomic_inc(&cpu_hotplug.refcount);

	if (refcount <= 0 && waitqueue_active(&cpu_hotplug.wq))
		wake_up(&cpu_hotplug.wq);

	cpuhp_lock_release();

}
EXPORT_SYMBOL_GPL(put_online_cpus);

/*
 * This ensures that the hotplug operation can begin only when the
 * refcount goes to zero.
 *
 * Note that during a cpu-hotplug operation, the new readers, if any,
 * will be blocked by the cpu_hotplug.lock
 *
 * Since cpu_hotplug_begin() is always called after invoking
 * cpu_maps_update_begin(), we can be sure that only one writer is active.
 *
 * Note that theoretically, there is a possibility of a livelock:
 * - Refcount goes to zero, last reader wakes up the sleeping
 *   writer.
 * - Last reader unlocks the cpu_hotplug.lock.
 * - A new reader arrives at this moment, bumps up the refcount.
 * - The writer acquires the cpu_hotplug.lock finds the refcount
 *   non zero and goes to sleep again.
 *
 * However, this is very difficult to achieve in practice since
 * get_online_cpus() not an api which is called all that often.
 *
 */
#ifndef CONFIG_TINY_RCU
extern int rcu_expedited;
static int rcu_expedited_bak;
#endif
void cpu_hotplug_begin(void)
{
	DEFINE_WAIT(wait);

	cpu_hotplug.active_writer = current;
	cpuhp_lock_acquire();
#ifndef CONFIG_TINY_RCU
	rcu_expedited_bak = rcu_expedited;
	rcu_expedited = 0;
#endif

	for (;;) {
		mutex_lock(&cpu_hotplug.lock);
		prepare_to_wait(&cpu_hotplug.wq, &wait, TASK_UNINTERRUPTIBLE);
		if (likely(!atomic_read(&cpu_hotplug.refcount)))
				break;
		mutex_unlock(&cpu_hotplug.lock);
		schedule();
	}
	finish_wait(&cpu_hotplug.wq, &wait);
}

void cpu_hotplug_done(void)
{
	cpu_hotplug.active_writer = NULL;
	mutex_unlock(&cpu_hotplug.lock);
#ifndef CONFIG_TINY_RCU
	rcu_expedited = rcu_expedited_bak;
#endif
	cpuhp_lock_release();
}

/*
 * Wait for currently running CPU hotplug operations to complete (if any) and
 * disable future CPU hotplug (from sysfs). The 'cpu_add_remove_lock' protects
 * the 'cpu_hotplug_disabled' flag. The same lock is also acquired by the
 * hotplug path before performing hotplug operations. So acquiring that lock
 * guarantees mutual exclusion from any currently running hotplug operations.
 */
void cpu_hotplug_disable(void)
{
	cpu_maps_update_begin();
	cpu_hotplug_disabled++;
	cpu_maps_update_done();
}
EXPORT_SYMBOL_GPL(cpu_hotplug_disable);

static void __cpu_hotplug_enable(void)
{
	if (WARN_ONCE(!cpu_hotplug_disabled, "Unbalanced cpu hotplug enable\n"))
		return;
	cpu_hotplug_disabled--;
}

void cpu_hotplug_enable(void)
{
	cpu_maps_update_begin();
	__cpu_hotplug_enable();
	cpu_maps_update_done();
}
EXPORT_SYMBOL_GPL(cpu_hotplug_enable);
#endif	/* CONFIG_HOTPLUG_CPU */

/*
 * Architectures that need SMT-specific errata handling during SMT hotplug
 * should override this.
 */
void __weak arch_smt_update(void) { }

/* Need to know about CPUs going up/down? */
int register_cpu_notifier(struct notifier_block *nb)
{
	int ret;
	cpu_maps_update_begin();
	ret = raw_notifier_chain_register(&cpu_chain, nb);
	cpu_maps_update_done();
	return ret;
}

int __register_cpu_notifier(struct notifier_block *nb)
{
	return raw_notifier_chain_register(&cpu_chain, nb);
}

static int __cpu_notify(unsigned long val, void *v, int nr_to_call,
			int *nr_calls)
{
	int ret;

	ret = __raw_notifier_call_chain(&cpu_chain, val, v, nr_to_call,
					nr_calls);

	return notifier_to_errno(ret);
}

static int cpu_notify(unsigned long val, void *v)
{
	return __cpu_notify(val, v, -1, NULL);
}

EXPORT_SYMBOL(register_cpu_notifier);
EXPORT_SYMBOL(__register_cpu_notifier);

int register_cpus_notifier(struct notifier_block *nb)
{
	int ret;
	cpu_maps_update_begin();
	ret = raw_notifier_chain_register(&cpus_chain, nb);
	cpu_maps_update_done();
	return ret;
}

static int __cpus_notify(unsigned long val, void *v, int nr_to_call,
			int *nr_calls)
{
	int ret;

	ret = __raw_notifier_call_chain(&cpus_chain, val, v, nr_to_call,
					nr_calls);

	return notifier_to_errno(ret);
}

static int cpus_notify(unsigned long val, void *v)
{
	return __cpus_notify(val, v, -1, NULL);
}

#ifdef CONFIG_HOTPLUG_CPU
static void cpu_notify_nofail(unsigned long val, void *v)
{
	BUG_ON(cpu_notify(val, v));
}

void unregister_cpu_notifier(struct notifier_block *nb)
{
	cpu_maps_update_begin();
	raw_notifier_chain_unregister(&cpu_chain, nb);
	cpu_maps_update_done();
}
EXPORT_SYMBOL(unregister_cpu_notifier);

void __unregister_cpu_notifier(struct notifier_block *nb)
{
	raw_notifier_chain_unregister(&cpu_chain, nb);
}
EXPORT_SYMBOL(__unregister_cpu_notifier);

static void cpus_notify_nofail(unsigned long val, void *v)
{
	BUG_ON(cpus_notify(val, v));
}
EXPORT_SYMBOL(register_cpus_notifier);

void unregister_cpus_notifier(struct notifier_block *nb)
{
	cpu_maps_update_begin();
	raw_notifier_chain_unregister(&cpus_chain, nb);
	cpu_maps_update_done();
}
EXPORT_SYMBOL(unregister_cpus_notifier);

/**
 * clear_tasks_mm_cpumask - Safely clear tasks' mm_cpumask for a CPU
 * @cpu: a CPU id
 *
 * This function walks all processes, finds a valid mm struct for each one and
 * then clears a corresponding bit in mm's cpumask.  While this all sounds
 * trivial, there are various non-obvious corner cases, which this function
 * tries to solve in a safe manner.
 *
 * Also note that the function uses a somewhat relaxed locking scheme, so it may
 * be called only for an already offlined CPU.
 */
void clear_tasks_mm_cpumask(int cpu)
{
	struct task_struct *p;

	/*
	 * This function is called after the cpu is taken down and marked
	 * offline, so its not like new tasks will ever get this cpu set in
	 * their mm mask. -- Peter Zijlstra
	 * Thus, we may use rcu_read_lock() here, instead of grabbing
	 * full-fledged tasklist_lock.
	 */
	WARN_ON(cpu_online(cpu));
	rcu_read_lock();
	for_each_process(p) {
		struct task_struct *t;

		/*
		 * Main thread might exit, but other threads may still have
		 * a valid mm. Find one.
		 */
		t = find_lock_task_mm(p);
		if (!t)
			continue;
		cpumask_clear_cpu(cpu, mm_cpumask(t->mm));
		task_unlock(t);
	}
	rcu_read_unlock();
}

static inline void check_for_tasks(int dead_cpu)
{
	struct task_struct *g, *p;

	read_lock(&tasklist_lock);
	for_each_process_thread(g, p) {
		if (!p->on_rq)
			continue;
		/*
		 * We do the check with unlocked task_rq(p)->lock.
		 * Order the reading to do not warn about a task,
		 * which was running on this cpu in the past, and
		 * it's just been woken on another cpu.
		 */
		rmb();
		if (task_cpu(p) != dead_cpu)
			continue;

		pr_warn("Task %s (pid=%d) is on cpu %d (state=%ld, flags=%x)\n",
			p->comm, task_pid_nr(p), dead_cpu, p->state, p->flags);
	}
	read_unlock(&tasklist_lock);
}

struct take_cpu_down_param {
	unsigned long mod;
	void *hcpu;
};

/* Take this CPU down. */
static int take_cpu_down(void *_param)
{
	struct take_cpu_down_param *param = _param;
	void *hcpu = param->hcpu;
	int err;

	if ((long)hcpu == NR_CPUS)
		hcpu = (void *)(long)smp_processor_id();

	/* Ensure this CPU doesn't handle any more interrupts. */
	err = __cpu_disable();
	if (err < 0)
		return err;

	cpu_notify(CPU_DYING | param->mod, hcpu);
	/* Give up timekeeping duties */
	tick_handover_do_timer();
	/* Park the stopper thread */
	stop_machine_park((long)hcpu);
	return 0;
}

/* Requires cpu_add_remove_lock to be held */
static int _cpu_down(unsigned int cpu, int tasks_frozen)
{
	int err, nr_calls = 0;
	void *hcpu = (void *)(long)cpu;
	unsigned long mod = tasks_frozen ? CPU_TASKS_FROZEN : 0;
	struct take_cpu_down_param tcd_param = {
		.mod = mod,
		.hcpu = hcpu,
	};

	if (num_online_cpus() == 1)
		return -EBUSY;

	if (!cpu_online(cpu))
		return -EINVAL;

	cpu_hotplug_begin();

	cpuidle_disable_device(per_cpu(cpuidle_devices, cpu));

	err = __cpu_notify(CPU_DOWN_PREPARE | mod, hcpu, -1, &nr_calls);
	if (err) {
		nr_calls--;
		__cpu_notify(CPU_DOWN_FAILED | mod, hcpu, nr_calls, NULL);
		cpuidle_enable_device(per_cpu(cpuidle_devices, cpu));
		pr_warn("%s: attempt to take down CPU %u failed\n",
			__func__, cpu);
		goto out_release;
	}

	cpu_notify_nofail(CPU_DOWN_LATE_PREPARE | mod, 0);

	smpboot_park_threads(cpu);

	/*
	 * Prevent irq alloc/free while the dying cpu reorganizes the
	 * interrupt affinities.
	 */
	irq_lock_sparse();

	err = stop_machine(take_cpu_down, &tcd_param, cpumask_of(cpu));
	if (err) {
		/* CPU didn't die: tell everyone.  Can't complain. */
		cpu_notify_nofail(CPU_DOWN_FAILED | mod, hcpu);
		irq_unlock_sparse();
		goto out_release;
	}
	BUG_ON(cpu_online(cpu));

	/*
	 * The migration_call() CPU_DYING callback will have removed all
	 * runnable tasks from the cpu, there's only the idle task left now
	 * that the migration thread is done doing the stop_machine thing.
	 *
	 * Wait for the stop thread to go away.
	 */
	while (!per_cpu(cpu_dead_idle, cpu))
		cpu_relax();
	smp_mb(); /* Read from cpu_dead_idle before __cpu_die(). */
	per_cpu(cpu_dead_idle, cpu) = false;

	/* Interrupts are moved away from the dying cpu, reenable alloc/free */
	irq_unlock_sparse();

	hotplug_cpu__broadcast_tick_pull(cpu);
	/* This actually kills the CPU. */
	__cpu_die(cpu);

#ifdef CONFIG_HMP_SCHED
	if (cpumask_test_cpu(cpu, &hmp_fast_cpu_mask))
		cpus_notify_nofail(CPUS_DOWN_COMPLETE, (void *)cpu_online_mask);
#endif

	/* CPU is completely dead: tell everyone.  Too late to complain. */
	tick_cleanup_dead_cpu(cpu);
	cpu_notify_nofail(CPU_DEAD | mod, hcpu);

	check_for_tasks(cpu);

out_release:
	cpu_hotplug_done();
	trace_sched_cpu_hotplug(cpu, err, 0);
	if (!err)
		cpu_notify_nofail(CPU_POST_DEAD | mod, hcpu);
	arch_smt_update();
	return err;
}

int cpu_down(unsigned int cpu)
{
	struct cpumask newmask;
	int err;

	preempt_disable();
	cpumask_andnot(&newmask, cpu_online_mask, cpumask_of(cpu));
	preempt_enable();

	/* One big cluster CPU and one little cluster CPU must remain online */
	if (!cpumask_intersects(&newmask, cpu_perf_mask) ||
	    !cpumask_intersects(&newmask, cpu_lp_mask))
		return -EINVAL;

	cpu_maps_update_begin();

	if (cpu_hotplug_disabled) {
		err = -EBUSY;
		goto out;
	}

	err = _cpu_down(cpu, 0);

out:
	cpu_maps_update_done();
	return err;
}
EXPORT_SYMBOL(cpu_down);

int cpus_down(const struct cpumask *cpus)
{
	cpumask_t dest_cpus;
	cpumask_t prepared_cpus;
	int err = 0, cpu;
	int nr_calls[8] = {0};
	struct take_cpu_down_param tcd_param = {
		.mod = 0,
		.hcpu = (void *)NR_CPUS,
	};

	cpu_maps_update_begin();
	cpu_hotplug_begin();

	cpumask_and(&dest_cpus, cpus, cpu_online_mask);

	if (cpu_hotplug_disabled || !cpumask_weight(&dest_cpus)
			|| num_online_cpus() <= cpumask_weight(&dest_cpus)) {
		err = -EBUSY;
		goto out;
	}

	cpumask_clear(&prepared_cpus);

	for_each_cpu(cpu, &dest_cpus) {
		void *hcpu = (void *)(long)cpu;
		cpumask_set_cpu(cpu, &prepared_cpus);

		cpuidle_disable_device(per_cpu(cpuidle_devices, cpu));

		err = __cpu_notify(CPU_DOWN_PREPARE, hcpu, -1, &nr_calls[cpu]);
		if (err) {
			nr_calls[cpu]--;
			goto err_down_prepare;
		}
	}

	cpu_notify_nofail(CPU_DOWN_LATE_PREPARE, 0);

	for_each_cpu(cpu, &dest_cpus) {
		smpboot_park_threads(cpu);
	}

	/*
	 * Prevent irq alloc/free while the dying cpu reorganizes the
	 * interrupt affinities.
	 */
	irq_lock_sparse();

	err = stop_machine(take_cpu_down, &tcd_param, &dest_cpus);
	if (err)
		goto err_stop_machine;

	for_each_cpu(cpu, &dest_cpus) {
		BUG_ON(cpu_online(cpu));
		/*
		 * The migration_call() CPU_DYING callback will have removed all
		 * runnable tasks from the cpu, there's only the idle task left now
		 * that the migration thread is done doing the stop_machine thing.
		 *
		 * Wait for the stop thread to go away.
		 */
		while (!per_cpu(cpu_dead_idle, cpu))
			cpu_relax();
		smp_mb(); /* Read from cpu_dead_idle before __cpu_die(). */
		per_cpu(cpu_dead_idle, cpu) = false;
	}

	/* Interrupts are moved away from the dying cpu, reenable alloc/free */
	irq_unlock_sparse();

	for_each_cpu(cpu, &dest_cpus) {
		hotplug_cpu__broadcast_tick_pull(cpu);
		/* This actually kills the CPU. */
		__cpu_die(cpu);
	}

	cpus_notify_nofail(CPUS_DOWN_COMPLETE, (void *)cpu_online_mask);

	/* CPU is completely dead: tell everyone.  Too late to complain. */
	for_each_cpu(cpu, &dest_cpus) {
		void *hcpu = (void *)(long)cpu;

		tick_cleanup_dead_cpu(cpu);
		cpu_notify_nofail(CPU_DEAD, hcpu);

		check_for_tasks(cpu);
	}

	cpu_hotplug_done();

	for_each_cpu(cpu, &dest_cpus) {
		void *hcpu = (void *)(long)cpu;

		trace_sched_cpu_hotplug(cpu, err, 0);
		cpu_notify_nofail(CPU_POST_DEAD, hcpu);
	}

	cpu_maps_update_done();

	return 0;

err_stop_machine:
	for_each_cpu(cpu, &dest_cpus) {
		void *hcpu = (void *)(long)cpu;
		smpboot_unpark_threads(cpu);
		cpu_notify_nofail(CPU_DOWN_FAILED, hcpu);
	}
	goto out;

err_down_prepare:
	for_each_cpu(cpu, &prepared_cpus) {
		void *hcpu = (void *)(long)cpu;
		cpuidle_enable_device(per_cpu(cpuidle_devices, cpu));
		__cpu_notify(CPU_DOWN_FAILED, hcpu, nr_calls[cpu], NULL);
		printk("%s: attempt to take down CPU %u failed\n",
					__func__, cpu);
	}

out:
	cpu_hotplug_done();
	cpu_maps_update_done();

	return err;
}

int cpus_up(const struct cpumask *cpus)
{
	int err = 0;
	unsigned int cpu = 0;
	cpumask_t dest_cpus;

	cpumask_andnot(&dest_cpus, cpus, cpu_online_mask);
	for_each_cpu(cpu, &dest_cpus) {
		err = cpu_up(cpu);
		if (err)
			goto out;
	}
out:
	return err;
}
EXPORT_SYMBOL(cpus_up);
#endif /*CONFIG_HOTPLUG_CPU*/

/*
 * Unpark per-CPU smpboot kthreads at CPU-online time.
 */
static int smpboot_thread_call(struct notifier_block *nfb,
			       unsigned long action, void *hcpu)
{
	int cpu = (long)hcpu;

	switch (action & ~CPU_TASKS_FROZEN) {

	case CPU_DOWN_FAILED:
	case CPU_ONLINE:
		smpboot_unpark_threads(cpu);
		break;

	default:
		break;
	}

	return NOTIFY_OK;
}

static struct notifier_block smpboot_thread_notifier = {
	.notifier_call = smpboot_thread_call,
	.priority = CPU_PRI_SMPBOOT,
};

void smpboot_thread_init(void)
{
	register_cpu_notifier(&smpboot_thread_notifier);
}

/* Requires cpu_add_remove_lock to be held */
static int _cpu_up(unsigned int cpu, int tasks_frozen)
{
	int ret, nr_calls = 0;
	void *hcpu = (void *)(long)cpu;
	unsigned long mod = tasks_frozen ? CPU_TASKS_FROZEN : 0;
	struct task_struct *idle;

	cpu_hotplug_begin();

	if (cpu_online(cpu) || !cpu_present(cpu)) {
		ret = -EINVAL;
		goto out;
	}

	idle = idle_thread_get(cpu);
	if (IS_ERR(idle)) {
		ret = PTR_ERR(idle);
		goto out;
	}

	ret = smpboot_create_threads(cpu);
	if (ret)
		goto out;

	ret = __cpu_notify(CPU_UP_PREPARE | mod, hcpu, -1, &nr_calls);
	if (ret) {
		nr_calls--;
		pr_warn("%s: attempt to bring up CPU %u failed\n",
			__func__, cpu);
		goto out_notify;
	}

	/* Arch-specific enabling code. */
	ret = __cpu_up(cpu, idle);

	if (ret != 0)
		goto out_notify;
	BUG_ON(!cpu_online(cpu));

	/* Now call notifier in preparation. */
	cpu_notify(CPU_ONLINE | mod, hcpu);
	cpuidle_enable_device(per_cpu(cpuidle_devices, cpu));

out_notify:
	if (ret != 0)
		__cpu_notify(CPU_UP_CANCELED | mod, hcpu, nr_calls, NULL);
out:
	cpu_hotplug_done();
	arch_smt_update();
	return ret;
}

int cpu_up(unsigned int cpu)
{
	int err = 0;
#ifdef CONFIG_SCHED_HMP
	cpumask_t dest_cpus;
#endif

	if (!cpu_possible(cpu)) {
		pr_err("can't online cpu %d because it is not configured as may-hotadd at boot time\n",
		       cpu);
#if defined(CONFIG_IA64)
		pr_err("please check additional_cpus= boot parameter\n");
#endif
		return -EINVAL;
	}

	if (!cpumask_test_cpu(cpu, &early_cpu_mask)) {
		dump_stack();
		return -EINVAL;
	}

	err = try_online_node(cpu_to_node(cpu));
	if (err)
		return err;

	cpu_maps_update_begin();

	if (cpu_hotplug_disabled) {
		err = -EBUSY;
		goto out;
	}

#ifdef CONFIG_SCHED_HMP
	if (cpumask_test_cpu(cpu, &hmp_fast_cpu_mask)) {
		cpumask_or(&dest_cpus, cpumask_of(cpu), cpu_online_mask);

		err = cpus_notify(CPUS_UP_PREPARE, (void *)&dest_cpus);
		if (err)
			goto out;
	}
#endif

	err = _cpu_up(cpu, 0);

out:
	cpu_maps_update_done();
	return err;
}
EXPORT_SYMBOL_GPL(cpu_up);

#ifdef CONFIG_PM_SLEEP_SMP
static cpumask_var_t frozen_cpus;

int disable_nonboot_cpus(void)
{
	int cpu, first_cpu, error = 0;

	cpu_maps_update_begin();
	first_cpu = cpumask_first(cpu_online_mask);
	/*
	 * We take down all of the non-boot CPUs in one shot to avoid races
	 * with the userspace trying to use the CPU hotplug at the same time
	 */
	cpumask_clear(frozen_cpus);

	pr_info("Disabling non-boot CPUs ...\n");
	for_each_online_cpu(cpu) {
		if (cpu == first_cpu)
			continue;
		trace_suspend_resume(TPS("CPU_OFF"), cpu, true);
		error = _cpu_down(cpu, 1);
		trace_suspend_resume(TPS("CPU_OFF"), cpu, false);
		if (!error)
			cpumask_set_cpu(cpu, frozen_cpus);
		else {
			pr_err("Error taking CPU%d down: %d\n", cpu, error);
			break;
		}
	}

	if (!error)
		BUG_ON(num_online_cpus() > 1);
	else
		pr_err("Non-boot CPUs are not disabled\n");

	/*
	 * Make sure the CPUs won't be enabled by someone else. We need to do
	 * this even in case of failure as all disable_nonboot_cpus() users are
	 * supposed to do enable_nonboot_cpus() on the failure path.
	 */
	cpu_hotplug_disabled++;

	cpu_maps_update_done();
	return error;
}

void __weak arch_enable_nonboot_cpus_begin(void)
{
}

void __weak arch_enable_nonboot_cpus_end(void)
{
}

void enable_nonboot_cpus(void)
{
	int cpu, error;
	struct device *cpu_device;

	/* Allow everyone to use the CPU hotplug again */
	cpu_maps_update_begin();
	__cpu_hotplug_enable();
	if (cpumask_empty(frozen_cpus))
		goto out;

	pr_info("Enabling non-boot CPUs ...\n");

	arch_enable_nonboot_cpus_begin();

	for_each_cpu(cpu, frozen_cpus) {
		trace_suspend_resume(TPS("CPU_ON"), cpu, true);
		error = _cpu_up(cpu, 1);
		trace_suspend_resume(TPS("CPU_ON"), cpu, false);
		if (!error) {
			pr_info("CPU%d is up\n", cpu);
			cpu_device = get_cpu_device(cpu);
			if (!cpu_device)
				pr_err("%s: failed to get cpu%d device\n",
				       __func__, cpu);
			else
				kobject_uevent(&cpu_device->kobj, KOBJ_ONLINE);
			continue;
		}
		pr_warn("Error taking CPU%d up: %d\n", cpu, error);
	}

	arch_enable_nonboot_cpus_end();

	cpumask_clear(frozen_cpus);
out:
	cpu_maps_update_done();
}

static int __init alloc_frozen_cpus(void)
{
	if (!alloc_cpumask_var(&frozen_cpus, GFP_KERNEL|__GFP_ZERO))
		return -ENOMEM;
	return 0;
}
core_initcall(alloc_frozen_cpus);

/*
 * When callbacks for CPU hotplug notifications are being executed, we must
 * ensure that the state of the system with respect to the tasks being frozen
 * or not, as reported by the notification, remains unchanged *throughout the
 * duration* of the execution of the callbacks.
 * Hence we need to prevent the freezer from racing with regular CPU hotplug.
 *
 * This synchronization is implemented by mutually excluding regular CPU
 * hotplug and Suspend/Hibernate call paths by hooking onto the Suspend/
 * Hibernate notifications.
 */
static int
cpu_hotplug_pm_callback(struct notifier_block *nb,
			unsigned long action, void *ptr)
{
	switch (action) {

	case PM_SUSPEND_PREPARE:
	case PM_HIBERNATION_PREPARE:
		cpu_hotplug_disable();
		break;

	case PM_POST_SUSPEND:
	case PM_POST_HIBERNATION:
		cpu_hotplug_enable();
		break;

	default:
		return NOTIFY_DONE;
	}

	return NOTIFY_OK;
}


static int __init cpu_hotplug_pm_sync_init(void)
{
	/*
	 * cpu_hotplug_pm_callback has higher priority than x86
	 * bsp_pm_callback which depends on cpu_hotplug_pm_callback
	 * to disable cpu hotplug to avoid cpu hotplug race.
	 */
	pm_notifier(cpu_hotplug_pm_callback, 0);
	return 0;
}
core_initcall(cpu_hotplug_pm_sync_init);

#endif /* CONFIG_PM_SLEEP_SMP */

/**
 * notify_cpu_starting(cpu) - call the CPU_STARTING notifiers
 * @cpu: cpu that just started
 *
 * This function calls the cpu_chain notifiers with CPU_STARTING.
 * It must be called by the arch code on the new cpu, before the new cpu
 * enables interrupts and before the "boot" cpu returns from __cpu_up().
 */
void notify_cpu_starting(unsigned int cpu)
{
	unsigned long val = CPU_STARTING;

#ifdef CONFIG_PM_SLEEP_SMP
	if (frozen_cpus != NULL && cpumask_test_cpu(cpu, frozen_cpus))
		val = CPU_STARTING_FROZEN;
#endif /* CONFIG_PM_SLEEP_SMP */
	cpu_notify(val, (void *)(long)cpu);
}

#endif /* CONFIG_SMP */

/*
 * cpu_bit_bitmap[] is a special, "compressed" data structure that
 * represents all NR_CPUS bits binary values of 1<<nr.
 *
 * It is used by cpumask_of() to get a constant address to a CPU
 * mask value that has a single bit set only.
 */

/* cpu_bit_bitmap[0] is empty - so we can back into it */
#define MASK_DECLARE_1(x)	[x+1][0] = (1UL << (x))
#define MASK_DECLARE_2(x)	MASK_DECLARE_1(x), MASK_DECLARE_1(x+1)
#define MASK_DECLARE_4(x)	MASK_DECLARE_2(x), MASK_DECLARE_2(x+2)
#define MASK_DECLARE_8(x)	MASK_DECLARE_4(x), MASK_DECLARE_4(x+4)

const unsigned long cpu_bit_bitmap[BITS_PER_LONG+1][BITS_TO_LONGS(NR_CPUS)] = {

	MASK_DECLARE_8(0),	MASK_DECLARE_8(8),
	MASK_DECLARE_8(16),	MASK_DECLARE_8(24),
#if BITS_PER_LONG > 32
	MASK_DECLARE_8(32),	MASK_DECLARE_8(40),
	MASK_DECLARE_8(48),	MASK_DECLARE_8(56),
#endif
};
EXPORT_SYMBOL_GPL(cpu_bit_bitmap);

const DECLARE_BITMAP(cpu_all_bits, NR_CPUS) = CPU_BITS_ALL;
EXPORT_SYMBOL(cpu_all_bits);

#ifdef CONFIG_INIT_ALL_POSSIBLE
static DECLARE_BITMAP(cpu_possible_bits, CONFIG_NR_CPUS) __read_mostly
	= CPU_BITS_ALL;
#else
static DECLARE_BITMAP(cpu_possible_bits, CONFIG_NR_CPUS) __read_mostly;
#endif
const struct cpumask *const cpu_possible_mask = to_cpumask(cpu_possible_bits);
EXPORT_SYMBOL(cpu_possible_mask);

static DECLARE_BITMAP(cpu_online_bits, CONFIG_NR_CPUS) __read_mostly;
const struct cpumask *const cpu_online_mask = to_cpumask(cpu_online_bits);
EXPORT_SYMBOL(cpu_online_mask);

static DECLARE_BITMAP(cpu_present_bits, CONFIG_NR_CPUS) __read_mostly;
const struct cpumask *const cpu_present_mask = to_cpumask(cpu_present_bits);
EXPORT_SYMBOL(cpu_present_mask);

static DECLARE_BITMAP(cpu_active_bits, CONFIG_NR_CPUS) __read_mostly;
const struct cpumask *const cpu_active_mask = to_cpumask(cpu_active_bits);
EXPORT_SYMBOL(cpu_active_mask);

#if CONFIG_LITTLE_CPU_MASK
static const unsigned long lp_cpu_bits = CONFIG_LITTLE_CPU_MASK;
const struct cpumask *const cpu_lp_mask = to_cpumask(&lp_cpu_bits);
#else
const struct cpumask *const cpu_lp_mask = cpu_possible_mask;
#endif
EXPORT_SYMBOL(cpu_lp_mask);

#if CONFIG_BIG_CPU_MASK
static const unsigned long perf_cpu_bits = CONFIG_BIG_CPU_MASK;
const struct cpumask *const cpu_perf_mask = to_cpumask(&perf_cpu_bits);
#else
const struct cpumask *const cpu_perf_mask = cpu_possible_mask;
#endif
EXPORT_SYMBOL(cpu_perf_mask);

void set_cpu_possible(unsigned int cpu, bool possible)
{
	if (possible)
		cpumask_set_cpu(cpu, to_cpumask(cpu_possible_bits));
	else
		cpumask_clear_cpu(cpu, to_cpumask(cpu_possible_bits));
}

void set_cpu_present(unsigned int cpu, bool present)
{
	if (present)
		cpumask_set_cpu(cpu, to_cpumask(cpu_present_bits));
	else
		cpumask_clear_cpu(cpu, to_cpumask(cpu_present_bits));
}

void set_cpu_online(unsigned int cpu, bool online)
{
	if (online) {
		cpumask_set_cpu(cpu, to_cpumask(cpu_online_bits));
		cpumask_set_cpu(cpu, to_cpumask(cpu_active_bits));
	} else {
		cpumask_clear_cpu(cpu, to_cpumask(cpu_online_bits));
	}
}

void set_cpu_active(unsigned int cpu, bool active)
{
	if (active)
		cpumask_set_cpu(cpu, to_cpumask(cpu_active_bits));
	else
		cpumask_clear_cpu(cpu, to_cpumask(cpu_active_bits));
}

void init_cpu_present(const struct cpumask *src)
{
	cpumask_copy(to_cpumask(cpu_present_bits), src);
}

void init_cpu_possible(const struct cpumask *src)
{
	cpumask_copy(to_cpumask(cpu_possible_bits), src);
}

void init_cpu_online(const struct cpumask *src)
{
	cpumask_copy(to_cpumask(cpu_online_bits), src);
}

enum cpu_mitigations cpu_mitigations = CPU_MITIGATIONS_AUTO;

static int __init mitigations_parse_cmdline(char *arg)
{
	if (!strcmp(arg, "off"))
		cpu_mitigations = CPU_MITIGATIONS_OFF;
	else if (!strcmp(arg, "auto"))
		cpu_mitigations = CPU_MITIGATIONS_AUTO;
	else
		pr_crit("Unsupported mitigations=%s, system may still be vulnerable\n",
			arg);

	return 0;
}
early_param("mitigations", mitigations_parse_cmdline);

static ATOMIC_NOTIFIER_HEAD(idle_notifier);

void idle_notifier_register(struct notifier_block *n)
{
	atomic_notifier_chain_register(&idle_notifier, n);
}
EXPORT_SYMBOL_GPL(idle_notifier_register);

void idle_notifier_unregister(struct notifier_block *n)
{
	atomic_notifier_chain_unregister(&idle_notifier, n);
}
EXPORT_SYMBOL_GPL(idle_notifier_unregister);

void idle_notifier_call_chain(unsigned long val)
{
	atomic_notifier_call_chain(&idle_notifier, val, NULL);
}
EXPORT_SYMBOL_GPL(idle_notifier_call_chain);