% WAITQUEUE(9) Helios Kernel API | Helios Kernel Manual

# NAME

waitqueue - block a task until an event occurs

# SYNOPSIS

	void waitqueue_init(struct waitqueue *wqueue);
	bool waitqueue_empty(struct waitqueue *wqueue);
	bool waitqueue_has_waiters(struct waitqueue *wqueue);

	void waitqueue_prepare_wait(struct waitqueue *wqueue);
	void waitqueue_commit_sleep(struct waitqueue *wqueue);
	void waitqueue_cancel_wait(struct waitqueue *wqueue);
	void waitqueue_sleep(struct waitqueue *wqueue);

	void waitqueue_wake_one(struct waitqueue *wqueue);
	void waitqueue_wake_all(struct waitqueue *wqueue);

# DESCRIPTION

A waitqueue holds tasks that wait for an event. A different task, or an
interrupt handler, sends a wake signal when the event occurs.

A wake signal is not stored. If a wake signal arrives when the queue is
empty, the signal is lost. A task that examines its wait condition and
then goes to sleep in two steps can thus miss the signal between the two
steps. This fault is a lost wakeup. The two-phase wait protocol below
prevents it.

Each task has one wait state:

- **WAIT_NONE** — The task does not wait.
- **WAIT_PREPARING** — The task is on a waitqueue but does not sleep yet.
- **WAIT_SLEEPING** — The task is blocked and sleeps.
- **WAIT_WOKEN** — A wake signal arrived while the task was in the
  WAIT_PREPARING state.

# THE WAIT PROTOCOL

**WARNING: Do the steps in this sequence. If you examine the condition
before you call `waitqueue_prepare_wait()`, a wake signal can be lost.
The task can then sleep while work is available.**

1. Call `waitqueue_prepare_wait()`. This puts the current task on the
   queue in the WAIT_PREPARING state. The task does not sleep.
2. Examine the wait condition.
3. If the task does not have to wait, call `waitqueue_cancel_wait()` and
   continue.
4. If the task must wait, call `waitqueue_commit_sleep()`.
5. When `waitqueue_commit_sleep()` returns, go to step 1 and examine the
   condition again.

The protocol is safe because each possible order gives a correct result:

- The wake signal arrives before step 1. Then the condition in step 2
  shows the event, and the task does not sleep.
- The wake signal arrives after step 1. Then the wake function sets the
  task state to WAIT_WOKEN, and `waitqueue_commit_sleep()` returns
  immediately.

There is no order in which the event occurs and the task stays asleep.

`waitqueue_commit_sleep()` can return before the condition is true. Always
examine the condition in a loop, as in step 5.

`waitqueue_sleep()` does steps 1 and 4 with no examination between them.
Use it only if a lost wake signal is acceptable, for example if a
subsequent signal always follows.

## Example

This loop is the worker thread of work_queue(9):

	while (true) {
		waitqueue_prepare_wait(&g_work_queue.wq);

		struct work_item* work = take_from_queue();

		if (work) {
			waitqueue_cancel_wait(&g_work_queue.wq);
			work->func(work->data);
			kfree(work);
		} else {
			waitqueue_commit_sleep(&g_work_queue.wq);
		}
	}

## Waits with an external lock

Some callers hold a lock that protects the wait condition, for example
the semaphore guard lock in semaphores.c.

**WARNING: Do not hold a spinlock when you call
`waitqueue_commit_sleep()`. The task that must send the wake signal can
need that lock. The system then deadlocks.**

1. Take the lock.
2. Examine the condition. If the task does not have to wait, do the work
   and release the lock.
3. Call `waitqueue_prepare_wait()`.
4. Release the lock.
5. Call `waitqueue_commit_sleep()`.

This order is safe for the same reason as before: after step 3, a wake
signal sets WAIT_WOKEN, and step 5 does not sleep.

# WAKE FUNCTIONS

`waitqueue_wake_one()` wakes the first waiter on the queue:

- A waiter in the WAIT_PREPARING state gets the WAIT_WOKEN state and
  leaves the queue. Its `waitqueue_commit_sleep()` call then returns
  immediately.
- A waiter in the WAIT_SLEEPING state leaves the queue and becomes ready
  to run.

`waitqueue_wake_all()` calls `waitqueue_wake_one()` until the queue is
empty.

A wake call on an empty queue does nothing. It is always safe.

# CONTEXT

`waitqueue_prepare_wait()`, `waitqueue_commit_sleep()`,
`waitqueue_cancel_wait()`, and `waitqueue_sleep()` are for process
context only. `waitqueue_commit_sleep()` and `waitqueue_sleep()` can
sleep.

`waitqueue_wake_one()` and `waitqueue_wake_all()` are safe in interrupt
context.

# LOCKING

Each waitqueue has an internal spinlock, `waiters_lock`. All waitqueue
functions take it. Callers do not take it.

The wake path takes `squeue.lock` while it holds `waiters_lock`. Thus the
lock order is: external condition lock, then `waiters_lock`, then
`squeue.lock`. Do not take these locks in a different order.

# SEE ALSO

spinlock(9), scheduler.c, semaphores.c, work_queue.c
