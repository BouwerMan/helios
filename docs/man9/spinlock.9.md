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

A spinlock is a lock with two states: 0 = unlocked, 1 = locked. A CPU
that waits for a spinlock spins in a busy loop.

A held spinlock is costly. Do not sleep while you hold a spinlock. Do
not call a function that can sleep. Keep each critical section short.

These locks are **not recursive**. If a CPU tries to acquire a lock that
it already holds, the CPU deadlocks.

These locks are **not fair**. Other CPUs can keep a waiter out of the
lock for an unknown time. If starvation becomes a problem, replace the
internal mechanism with a ticket lock or an MCS lock. The API in this
page does not change.

This header is specific to x86-64 (EFLAGS, cli/sti, PAUSE).

## Choice of a variant

A plain `spin_lock()` can deadlock against an interrupt handler. Example:
CPU A holds the lock. An interrupt occurs on CPU A. The interrupt
handler tries to acquire the same lock. The CPU then spins forever. The
`_irq` and `_irqsave` variants prevent this fault. They mask interrupts
on the local CPU for the full critical section.

- **`spin_lock_irqsave()`** — This is the default choice. It saves the
  interrupt state of the caller, disables interrupts, and restores the
  saved state on unlock. It is correct if interrupts were on at entry,
  and if they were off. Thus it is safe to nest.

- **`spin_lock_irq()`** — The unlock enables interrupts in all cases.
  Use it only where interrupts are on at entry: top-level code that is
  not reachable from an interrupt handler or from an other critical
  section. It is a small optimization. Incorrect use is easy.

- **`spin_lock()`** — It does not change the interrupt state. Use it
  only if no interrupt handler takes the lock, or if interrupts are off
  at entry (for example, in an interrupt handler). In other conditions
  it can deadlock.

Use the guard macros, not manual lock and unlock pairs. The guards
release the lock on each exit path. This includes an early return and a
goto.

## Scope-based locking

`spin_guard()` and `scoped_spin_guard()` acquire the lock with the
`spin_lock_irqsave()` procedure. They release it when the applicable
scope ends. The GNU C cleanup attribute causes the release. The release
occurs at the end of the block, at a return, and at a goto out of the
block.

`spin_guard()` is a declaration. Put it at the top of the block that
must be the critical section:

	void queue_push(struct task* t)
	{
		spin_guard(&squeue.lock);

		list_add_tail(&squeue.head, &t->node);
		if (!wake_needed())
			return;		// released here
		wake_one();
	}				// and here

To hold the lock for less than a full function, put `spin_guard()` in a
bare block. As an alternative, use `scoped_spin_guard()`. It is one
statement and can be the body of an `if` or of a loop without braces:

	scoped_spin_guard(&squeue.lock) {
		drain_queue();
	}

**WARNING: `scoped_spin_guard()` expands to a loop. A `break` or a
`continue` in the body binds to that hidden loop, not to an outer loop.
The control flow is then incorrect, with no diagnostic. The lock release
stays correct. If the critical section needs `break` or `continue`, use
`spin_guard()` in a bare block.** A `break` or a `continue` that belongs
to a loop or to a switch written in the body is not affected.

# RETURN VALUE

`spin_trylock()` returns true if it acquired the lock. It returns false
if an other holder had the lock. On false, the caller holds nothing and
must not call `spin_unlock()`.

`spin_lock_irqsave()` returns the interrupt state. The value is opaque.
Pass it to the matching `spin_unlock_irqrestore()`.

# LOCKING

If code takes two or more spinlocks, all code must take them in the same
global order. If two CPUs take two locks in different orders, the two
CPUs deadlock. Document the order at each point where locks nest.

# INTERACTIONS

`spin_unlock_irqrestore()` enables interrupts only if they were on when
the lock was taken. This makes nested `_irqsave` critical sections safe.

`spin_lock_irq()` and `spin_unlock_irq()` do not save or restore the
interrupt state. Use them only where interrupts are on at entry and the
unlock path is unconditional.

# IMPLEMENTATION NOTES

`spinlock_t` puts the raw value in a struct. The compiler then rejects a
direct examination or assignment (`if (lock)`, `lock = 0`). All access
goes through the functions above. The field is intentionally not
`volatile`. The `__atomic_*` builtins make sure that each access is
emitted, and they give acquire and release order against other memory.
`volatile` would give no gain, and each structure that contains a lock
would become slower.

The internal acquire path is a test-and-test-and-set loop. The outer
exchange is the acquisition attempt. When the lock is contended, the CPU
spins on a relaxed load. The local cache can satisfy that load. An
exchange on each iteration would make the cache line exclusive each
time; the relaxed load prevents this cost. Each spin iteration executes
`PAUSE`, which slows the spin and gives execution resources to the SMT
sibling.

# SEE ALSO

waitqueue(9)
