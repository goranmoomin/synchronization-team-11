#include <linux/rotation.h>
#include <linux/slab.h>
#include <linux/syscalls.h>
#include <linux/types.h>
#include <linux/wait.h>

static DECLARE_WAIT_QUEUE_HEAD(wq);

static DEFINE_MUTEX(orientation_lock);
static int orientation = 0;

DEFINE_MUTEX(rotlock_list_lock);
LIST_HEAD(rotlock_list);

DEFINE_MUTEX(rotlock_waitlist_lock);
LIST_HEAD(rotlock_waitlist);

DEFINE_MUTEX(next_rotlock_id_lock);
long next_rotlock_id = -1L;

void update_next_rotlock_id(void)
{
	struct rotlock *rl;

	mutex_lock(&orientation_lock);
	mutex_lock(&rotlock_list_lock);
	mutex_lock(&rotlock_waitlist_lock);

	list_for_each_entry (rl, &rotlock_waitlist, list) {
		if (rl->low <= orientation && rl->high >= orientation) {
			break;
		}
	}

	mutex_lock(&next_rotlock_id_lock);
	next_rotlock_id = &rl->list != &rotlock_waitlist ? rl->id : -1L;
	mutex_unlock(&next_rotlock_id_lock);

	mutex_unlock(&rotlock_waitlist_lock);
	mutex_unlock(&rotlock_list_lock);
	mutex_unlock(&orientation_lock);
}

int is_next(struct rotlock *rl)
{
	int retval;

	mutex_lock(&next_rotlock_id_lock);
	retval = next_rotlock_id == rl->id;
	mutex_unlock(&next_rotlock_id_lock);

	return retval;
}

SYSCALL_DEFINE1(set_orientation, int, degree)
{
	if (degree < 0 || degree >= 360)
		return -EINVAL;

	printk(KERN_INFO "set_orientation degree=%d\n", degree);

	mutex_lock(&orientation_lock);
	orientation = degree;
	mutex_unlock(&orientation_lock);

	update_next_rotlock_id();
	wake_up(&wq);

	return 0;
}

SYSCALL_DEFINE3(rotation_lock, int, low, int, high, int, type)
{
	static int newid = 0;
	struct rotlock *newlock;

	if (low < 0 || low >= 360 || high < 0 || high >= 360 ||
	    (type != ROT_READ && type != ROT_WRITE)) {
		return -EINVAL;
	}

	newlock = kmalloc(sizeof(*newlock), GFP_KERNEL);
	if (!newlock)
		return -ENOMEM;

	newlock->id = newid++;
	newlock->type = type;
	newlock->low = low;
	newlock->high = high;
	newlock->pid = current->pid;

	mutex_lock(&rotlock_waitlist_lock);
	list_add(&newlock->list, &rotlock_waitlist);
	mutex_unlock(&rotlock_waitlist_lock);

	update_next_rotlock_id();
	wake_up(&wq);

	while (wait_event_interruptible(wq, is_next(newlock)))
		;

	mutex_lock(&rotlock_waitlist_lock);
	list_del(&newlock->list);
	mutex_unlock(&rotlock_waitlist_lock);

	mutex_lock(&rotlock_list_lock);
	list_add(&newlock->list, &rotlock_list);
	mutex_unlock(&rotlock_list_lock);

	update_next_rotlock_id();
	wake_up(&wq);

	return newlock->id;
}

SYSCALL_DEFINE1(rotation_unlock, long, id)
{
	update_next_rotlock_id();
	wake_up(&wq);

	return 0;
}
