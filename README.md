# Project 2: Bye, Synchronization!

## Building the project

The project can be built in a conventional way; the usual sequence of
`build-rpi3.sh`, `./setup-images.sh`, and `./qemu.sh` builds and boot
up from the built kernel and the Tizen userspace.

The test programs in the `test` directory must be compiled with
`aarch64-linux-gnu-gcc` with the `-static` option, as we have
implemented the syscalls as 64-bit only.

When testing, the command `echo 5 > /proc/sys/kernel/printk` can be
used to suppress superfluous logs.

## Implementation overview

The following files were modified:

- `include/linux/rotation.h`:
  - define the types `struct rotlock` and `enum rotlock_state`.
  - declare `exit_rotation` for cleanup.
- `kernel/exit.c`:
  - add a call to `exit_rotation` in `do_exit`.
- `kernel/rotation.c`:
  - declare all relevant variables that hold the rotlocks, the locks
    to protect them, etc.
  - implement the `update_rotlocks` function.
  - implement the syscalls `set_orientation`, `rotation_lock`, and
    `rotation_unlock`.

The overall implementation works as described below:

- The kernel represents all rotation locks as a linked list of `struct
  rotlock` instances, `rotlock_list`.
- Each syscall updates the list itself, or requests updating the state
  of rotlocks by calling `update_rotlocks`.
- Blocking is implemented with a wait queue that is woken up on each
  `update_rotlocks` call.
- Each task checks whether the rotlock it is waiting for is activated
  or not on wake up.

Below are additional explanations of the details.

### The structure `struct rotlock`

The structure `struct rotlock` is the representation of the rotation
lock in the kernel. The properties `id`, `type`, `low`, `high`, and
`pid` represent what the names suggest; they are immutable, and hence
can be read directly without any protection.

The mutable property `list` is a `struct list_head` to help create a
linked list of rotlocks. The mutable property `state` represents
whether the rotlock is acquired or not with an `enum rotlock_state`.
The value is either `ROTLOCK_WAITING` or `ROTLOCK_ACQUIRED`.

The mutable properties are protected from races with the spinlock
property `lock`. In practice, the only case the `list` property can
get modified concurrently is when the `rotlock_list` gets edited.
Since this case is protected by the lock for the list itself, the
`lock` is only used for protecting the `state` property.

### Updating lock states in `update_rotlocks`

The function `update_rotlocks`, implemented in `kernel/rotation.c`,
updates the state from waiting to acquired with the following logic:

- for each waiting write rotlock `W` in `rotlock_list` with a valid
  orientation…
  - traverse `rotlock_list`, checking if each acquired rotlock does
    not overlap with `W`…
  - and if none overlaps, set the state of `W` to acquired.

- for each waiting read rotlock `R` in `rotlock_list` with a valid
  orientation…
  - traverse `rotlock_list`, checking if each acquired or **valid
    waiting write** rotlock does not overlap with `R`…
  - and if none overlaps, set the state of `R` to acquired.

- mark all syscalls in the wait queue `wq` as runnable
  (`wake_up(&wq)`) to wake up syscalls waiting for the rotlock to be
  acquired.

The logic ensures that writer starvation cannot happen by only
acquiring read rotlocks when there are no waiting valid write
rotlocks.

### Implementation of `rotation_lock` and `rotation_unlock`

The syscalls themselves are quite simple. `rotation_lock` simply

- creates a new `struct rotlock` instance on the heap, handling errors
  appropriately…
- locks and adds the rotlock instance to the list…
- calls `update_rotlocks` since `rotlock_list` has changed…
- and waits until the rotlock gets acquired.

Similarly, `rotation_unlock` simply

- checks permissions with pid…
- locks and removes the specific rotlock instance from the list…
- calls `update_rotlocks` since `rotlock_list` has changed…
- and frees the removed rotlock instance.

### Cleanup on task exit in `exit_rotation`

We do not have to consider any rotlocks that are still being waited to
be acquired, because it seems that a task in an uninterruptible sleep
state cannot exit. This dramatically simplifies task cleanup: grabbing
the lock on `rotlock_list`, removing all rotlocks associated with the
specific task, and freeing them is sufficient.

`exit_rotation` is called from the task cleanup function `do_exit` in
`kernel/exit.c`.

### Implementation correctness

The state of rotlocks depends on two states: `orientation` and
`rotlock_list`. The function `update_rotlocks` can update the rotlock
states in one pass; one call to `update_rotlocks` is sufficient for
the rotlock states up-to-date with the two states.

Therefore, by calling `update_rotlocks` on every change of any of
these two states, we can guarantee that the kernel will eventually
always update the rotlock states; the kernel can not leave any
rotlocks to hang that should be activated but are not.

Notice that the only requirement here is that `update_rotlocks` gets
called on **any future point** after the two states change; in fact,
we believe that having a work queue and asynchronously executing
`update_rotlocks` will work as well. To go further, we might also be
able to avoid superfluous updates by only adding the update request to
the work queue when no update request is queued. However, we decided
to simplify the implementation at the cost of throughput and
synchronously call the function directly.

Also noteworthy to mention is that the implementation assumes that not
all value updates need to get seen by the locks. For example, in our
implementation, if the syscall `set_orientation` gets called by two
processes at the same time, the `update_rotlocks` call after the first
orientation update might get blocked until the second orientation
update finishes (and is waiting for the `update_rotlocks` call). The
same goes to lock and unlock; for example, if multiple unlocks happen
at the same time, the `update_rotlocks` call might get called after
the two unlocks, which results might differ from when the
`update_rotlocks` gets also called between the two unlocks.

Our understanding of the spec is that this is fine and permitted;
both the spec and clarifications on the spec only mention requirements
on whether the rotlock can be acquired or not; there are no hard
requirements on whether the rotlock **must** be acquired.

Indeed, any requirements on whether the rotlock must be acquired would
require all rotlock-related syscalls to have some determined order,
crushing any performance advantage of multi-core systems without
benefit. Even when the kernel does linearize related syscalls,
user-space cannot determine the order between two unrelated syscalls
(unless the user-space program has sufficient knowledge of the whole
system and hence can predict how the kernel schedules tasks).

Our implementation does also behave that if a syscall has returned,
the next syscall will have already seen the effects from the previous
syscall (by synchronously updating the rotlock states). For example,
if a program has called `rotation_unlock` from some thread and has
already returned, our implementation behaves as if all possible
rotlock state updates that could have happened from the unlock have
already happened. This means that a sequence of non-overlapping
syscalls starting from the same state will have deterministic behavior
in our implementation. However, this is strictly additional behavior
that happened from an implementation detail; this is not required from
the specification.

### Miscellaneous implementation details

#### Why use a spinlock for each rotlock?

While waiting in the blocking `rotation_lock` syscall, the kernel
cannot hold a lock while sleeping as it will deadlock. Hence each time
the task wakes up, the syscall must lock each time to access its
rotlock state; to prevent excessive blocking, we implemented a
fine-grained locking system with each rotlock having a spinlock to
protect its mutable state.

Using per-rotlock spinlocks allows checking state in `rotation_lock`
much lightweight at a small performance cost in `update_rotlocks`.
Since `is_acquired` gets checked for every waiting rotlock in the
whole system on each `update_rotlocks` call, we believe that this is a
good tradeoff; it is quite hard for a rotlock to be acquired, so we
expect many rotlocks to be waiting for most workloads.

### Possible improvements

#### Performance

We currently use a very simplistic implementation of iterating the
whole global list to update the rotlock states; this probably can be
optimized with a more careful data structure design to store the
rotlocks. Sorting the rotlock list or having a separate list of
activated rotlocks, maintaining a separate cache indexed by
orientation to quickly check if specific ranges are locked or not were
considered but were not implemented due to the lack of time.

#### Interruptible, restartable syscalls

The current `rotation_lock` syscall goes to an uninterruptible sleep
until the rotlock gets acquired; this effectively freezes the whole
process, preventing any signals to get to the process, etc. To avoid
this, `wait_event_interruptible` can be used instead of `wait_event`;
if the wait was interrupted, removing the new rotlock from the rotlock
list and updating the rotlock states again would have been sufficient.
Additionally, by returning `-ESYSRESTART`, we could have also
automatically restarted the syscall. Unfortunately, we did not check
correctness rigorously enough for the above solution; we did not
implement this specific feature due to correctness concerns.

## Lessons

- Fine-grained locking is complicated to implement
- Global reasoning of asynchronous logic is hard
- The order of locks REALLY matters
- All night coding is harmful
