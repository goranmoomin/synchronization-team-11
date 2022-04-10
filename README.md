# Project 2: Bye, Synchronization!

## Building the project

TODO

## Implementation overview

TLDR:

- The kernel represents all rotation locks as a linked list of `struct
  rotlock` instances, `rotlock_list`.
- Each syscall updates the list itself, or requests updating the state
  of rotlocks by calling `update_rotlocks`.
- Blocking is implemented with a wait queue that is woken up on each
  `update_rotlocks` call.
- Each task checks whether the rotlock it is waiting for is activated
  or not on wake up.

### The structure `struct rotlock`

The structrue `struct rotlock` is the representation of the rotation
lock in the kernel. The properties `id`, `type`, `low`, `high`, and
`pid` represent what the names suggests; they are immutable, and hence
can be read directly without any protection.

The mutable property `list` is a `struct list_head` to help creating a
linked list of rotlocks. The mutable property `state` represents
whether the rotlock is acquired or not with a `enum rotlock_state`.
The value is either `ROTLOCK_WAITING` or `ROTLOCK_ACQUIRED`.

The mutable properties are protected from races with the spinlock
property `lock`. In practice, the `list` property only gets modified
when the lock is unreachable from other tasks (no task can have nor
get a reference to the rotlock instance), so the `lock` is only used
for protecting the `state` property.

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
  aquired.

The logic ensures that writer starvation cannot happen by only
acquiring read rotlocks when there are no waiting valid write
rotlocks.

### Implementation of `rotation_lock` and `rotation_unlock`

The syscalls itself are quite simple. `rotation_lock` simply

- creates a new `struct rotlock` instance on the heap, handling errors
  appropriately…
- locks and adds the rotlock instance to the list…
- calls `update_rotlocks` since `rotlock_list` has changed…
- and waits until the rotlock’s state gets acquired.

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

`exit_rotation` is called from the task cleaup function `do_exit` in
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
able to avoid superflous updates by only adding the update request to
the work queue when no update request is queued. However, we decided
to simplify the implementation at the cost of throughput and
synchronously call the function directly.

Also noteworthy to mention is that the implementation assumes that not
all value updates needs to get seen by the locks. For example, in our
implementation, if the syscall `set_orientation` gets called by two
processes at the same time, the `update_rotlocks` call after the first
orientation update might get blocked until the second orientation
update finishes (and is waiting for the `update_rotlocks` call). Same
goes to lock and unlock; for example, if multiple unlocks happen in
the same time, the `update_rotlocks` call might get called after the
two unlocks, which results might differ from when the
`update_rotlocks` gets also called between the two unlocks.

Our understanding of the spec is that this is fine and permitted;
both the spec and clarifications on the spec only mentions
requirements on whether the rotlock can be acquired or not; there are
no hard requirements on whether the rotlock **must** be acquired.

Indeed, any requirements on whether the rotlock must be acquired would
require for all rotlock-related syscalls to have some determined
order, crushing any performance advantage of multi-core systems
without benefit. Even when the kernel does linearize related syscalls,
user-space cannot determine the order between two unrelated syscalls
(unless the user-space program has sufficient knowledge on the whole
system and hence can predict how the kernel schedules tasks).

Our implementation does also behave that if a syscall has returned,
the next syscall will have already seen the effects from the previous
syscall (by synchronously updating the rotlock states). For example,
if a program has called `rotation_unlock` from some thread and has
already returned, out implementation behaves as if all possible
rotlock state updates that could have happened from the unlock has
already happened. This means that a sequence of non-overlapping
syscalls starting from the same state will have deterministic behavior
in our implementation. However, this is strictly additional behavior
that happened from an implementation detail; this is not required from
the specification.

### Miscellaneous implementation details

#### Why have a spinlock for each rotlock?

While waiting in the blocking `rotation_lock` syscall, the kernel
cannot hold a lock while sleeping as it will deadlock. Hence each time
the task wakes up, the syscall must lock each time to access its own
rotlock’s state; to prevent excessive blocking, we implemented a
fine-grained locking system with each rotlock having a spinlock to
protect its own mutable state.

Using a spinlock is expected to have performance advantages compared
to more heavy locks when the syscalls are called relatively
infrequently, and multiple locks are in a waiting state, periodically
checking if it is acquired. We believe that most workloads for the
implemented syscalls will follow this pattern – it is generally quite
hard for the rotlock to be acquired, so we expect that rotlocks will
be waited quite a lot.

### Possible improvements

TODO

- performance
- ESYSRESTART

### Testing

TODO
