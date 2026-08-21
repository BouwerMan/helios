% SPINLOCK(9) Helios Kernel API | Helios Kernel Manual

# NAME

spinlock - test-and-set spinlocks for x86-64

# SYNOPSIS

	void spin_init(spinlock_t *lock);
	void spin_lock(spinlock_t *lock);
	void spin_unlock(spinlock_t *lock);
	bool spin_trylock(spinlock_t *lock);
	void spin_lock_irq(spinlock_t *lock);
	void spin_unlock_irq(spinlock_t *lock);
	ulong spin_lock_irqsave(spinlock_t *lock);
	void spin_unlock_irqrestore(spinlock_t *lock, ulong flags);

	spin_guard(lockp);
	scoped_spin_guard(lockp) { ... }

# DESCRIPTION

A spinlock is a two-state lock (0 = unlocked, 1 = locked) acquired by
busy-waiting. Holding one is expensive: never sleep, block, or call into
anything that might, while a spinlock is held, and keep critical sections
as short as possible.

These locks are **not recursive**: acquiring a lock you already hold
deadlocks the CPU unconditionally.

These locks are **not fair**: a waiter can be starved indefinitely by other
CPUs. If starvation ever becomes a real problem, replace the internals with
a ticket or MCS lock; the API described here does not need to change.

This header is x86-64 specific (EFLAGS, cli/sti, PAUSE).

## Choosing a variant

The danger with a plain `spin_lock()` is self-deadlock against an interrupt
handler: if CPU A holds the lock and then takes an IRQ whose handler wants
the same lock, that CPU spins forever waiting on itself. The `_irq` and
`_irqsave` variants exist to prevent exactly this, by masking interrupts on
the local CPU for the duration of the critical section.

- **`spin_lock_irqsave()`** — Default choice. Saves the caller's interrupt
  state, disables interrupts, and restores the prior state on unlock.
  Correct whether or not interrupts were already off, so it composes
  safely.

- **`spin_lock_irq()`** — Unconditionally enables interrupts on unlock. Only
  use where you can prove interrupts were enabled on entry, i.e. top-level
  code that is never reachable from an interrupt handler or from another
  critical section. Slightly cheaper; easy to get wrong.

- **`spin_lock()`** — Leaves the interrupt state alone. Only correct if the
  lock is never taken from interrupt context, or if interrupts are already
  known to be disabled (for example, you are already inside an interrupt
  handler). Otherwise it can self-deadlock.

Prefer the guard macros over calling the lock/unlock pairs by hand. They
release the lock automatically on every exit path, including early return
and goto.

## Scope-based locking

`spin_guard()` and `scoped_spin_guard()` take the lock with
`spin_lock_irqsave()` semantics and release it automatically when the
relevant scope ends, via the GNU C cleanup attribute. That covers falling
off the end of the block, return, and goto out of the block.

`spin_guard()` is a declaration; place it at the top of the block whose
extent should match the critical section:

	void queue_push(struct task* t)
	{
		spin_guard(&squeue.lock);

		list_add_tail(&squeue.head, &t->node);
		if (!wake_needed())
			return;		// released here
		wake_one();
	}				// and here

To scope the lock more tightly than a whole function, wrap it in a bare
block, or use `scoped_spin_guard()`, which is a single statement and can be
used as the body of an `if` or a loop without adding braces:

	scoped_spin_guard(&squeue.lock) {
		drain_queue();
	}

**WARNING:** `scoped_spin_guard()` expands to a loop. A `break` or
`continue` inside the body binds to that hidden loop, not to any enclosing
loop, and will silently do the wrong thing (the lock is still released
correctly; only the control flow is wrong). If the critical section needs
`break` or `continue`, use `spin_guard()` in a bare block instead. `break`
and `continue` belonging to a loop or switch written inside the body are
unaffected.

# RETURN VALUE

`spin_trylock()` returns true if the lock was acquired, false if it was
already held. On false the caller holds nothing and must not call
`spin_unlock()`.

`spin_lock_irqsave()` returns the interrupt state to restore on unlock. The
value is opaque and must be passed to the matching
`spin_unlock_irqrestore()`.

# LOCKING

When acquiring more than one spinlock, all code must acquire them in the
same global order, or two CPUs will deadlock against each other. Document
the order wherever nesting occurs.

# INTERACTIONS

`spin_unlock_irqrestore()` re-enables interrupts only if they were enabled
when the lock was taken, which is what makes nesting `_irqsave` critical
sections safe.

`spin_lock_irq()` / `spin_unlock_irq()` do not save or restore state; only
use them where interrupts are provably enabled on entry and the unlock path
is unconditional.

# IMPLEMENTATION NOTES

`spinlock_t` wraps the raw value in a struct so the compiler rejects direct
inspection or assignment (`if (lock)`, `lock = 0`); all access goes through
the accessors above. The field is deliberately not `volatile`: the
`__atomic_*` builtins already guarantee the access is emitted and provide
acquire/release ordering against ordinary memory, so `volatile` would add
nothing while pessimising every structure that embeds a lock.

The internal acquire path is a test-and-test-and-set: the outer exchange is
the actual acquisition attempt; once the lock is seen to be contended, the
CPU spins on a relaxed load (satisfiable from the local cache line) rather
than hammering the cache line with exchanges that force it exclusive on
every iteration. Each spin iteration executes `PAUSE` to de-pipeline the
spin and yield to the SMT sibling.

# SEE ALSO

scheduler(9)
