#include <linux/rotation.h>
#include <linux/slab.h>
#include <linux/syscalls.h>
#include <linux/types.h>
#include <linux/wait.h>

static DECLARE_WAIT_QUEUE_HEAD(wq);

static DEFINE_MUTEX(orientation_lock);
static int orientation = 0;

static DEFINE_MUTEX(rotlock_list_lock);
static LIST_HEAD(rotlock_list);

static DEFINE_MUTEX(next_rotlock_lock);
static struct rotlock *next_rotlock = NULL;

static DEFINE_MUTEX(new_rotlock_id_lock);
static long new_rotlock_id = 0L;

#define VALID_ORIENTATION(low, high, ori)                                      \
	(low) <= (high) ? ((low) <= (ori) && (ori) <= (high)) :                \
			  ((low) <= (ori) && (ori) < 360) ||                   \
				  (0 <= (ori) && (ori) <= (high))

#define OVERLAP_INTERVAL(l1, h1, l2, h2)                                       \
	(l1) <= (h1) ? ((l2) <= (h2) ? (l2) <= (h1) && (l1) <= (h2) :          \
				       (l1) <= (h2) || (l2) <= (h1)) :         \
		       ((l2) <= (h2) ? (l2) <= (h1) || (l1) <= (h2) : 1)

static void update_next_rotlock(void)
{
	struct rotlock *rl, *aux_rl, *candidate = NULL;

	mutex_lock(&rotlock_list_lock);

	list_for_each_entry (rl, &rotlock_list, list) {
		spin_lock(&rl->lock);
	}

	mutex_lock(&orientation_lock);

	list_for_each_entry (rl, &rotlock_list, list) {
		if (!(rl->type == ROT_WRITE && rl->state == ROTLOCK_WAITING)) {
			continue;
		}
		if (VALID_ORIENTATION(rl->low, rl->high, orientation)) {
			candidate = rl;
			list_for_each_entry (aux_rl, &rotlock_list, list) {
				if (!(aux_rl->state == ROTLOCK_ACQUIRED)) {
					continue;
				}
				if ((OVERLAP_INTERVAL(rl->low, rl->high,
						      aux_rl->low,
						      aux_rl->high))) {
					candidate = NULL;
					break;
				}
			}
			if (candidate) {
				goto exit;
			}
		}
	}

	list_for_each_entry (rl, &rotlock_list, list) {
		if (!(rl->type == ROT_READ && rl->state == ROTLOCK_WAITING)) {
			continue;
		}
		if (VALID_ORIENTATION(rl->low, rl->high, orientation)) {
			candidate = rl;
			list_for_each_entry (aux_rl, &rotlock_list, list) {
				if (!(aux_rl->type == ROT_WRITE &&
				      aux_rl->state == ROTLOCK_ACQUIRED)) {
					continue;
				}
				if ((OVERLAP_INTERVAL(rl->low, rl->high,
						      aux_rl->low,
						      aux_rl->high))) {
					candidate = NULL;
					break;
				}
			}
			if (candidate) {
				goto exit;
			}
		}
	}

exit:
	mutex_unlock(&orientation_lock);

	list_for_each_entry_reverse (rl, &rotlock_list, list) {
		spin_unlock(&rl->lock);
	}
	mutex_unlock(&rotlock_list_lock);

	mutex_lock(&next_rotlock_lock);
	next_rotlock = candidate;
	mutex_unlock(&next_rotlock_lock);
}

static int is_next(struct rotlock *rl)
{
	int retval;

	mutex_lock(&next_rotlock_lock);
	retval = next_rotlock == rl;
	mutex_unlock(&next_rotlock_lock);

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

	update_next_rotlock();
	wake_up(&wq);

	return 0;
}

SYSCALL_DEFINE3(rotation_lock, int, low, int, high, int, type)
{
	struct rotlock *newlock;

	if (low < 0 || low >= 360 || high < 0 || high >= 360 ||
	    (type != ROT_READ && type != ROT_WRITE)) {
		return -EINVAL;
	}

	newlock = kmalloc(sizeof(*newlock), GFP_KERNEL);
	if (!newlock)
		return -ENOMEM;

	newlock->id = new_rotlock_id++;

	spin_lock_init(&newlock->lock);

	newlock->state = ROTLOCK_WAITING;
	newlock->type = type;
	newlock->low = low;
	newlock->high = high;
	newlock->pid = current->pid;

	mutex_lock(&rotlock_list_lock);
	list_add(&newlock->list, &rotlock_list);
	mutex_unlock(&rotlock_list_lock);

	update_next_rotlock();
	wake_up(&wq);

	for (;;) {
		wait_event(wq, is_next(newlock));

		mutex_lock(&next_rotlock_lock);
		if (next_rotlock == newlock)
			break;
		mutex_unlock(&next_rotlock_lock);
	}

	spin_lock(&newlock->lock);
	newlock->state = ROTLOCK_ACQUIRED;
	spin_unlock(&newlock->lock);

	mutex_unlock(&next_rotlock_lock);

	update_next_rotlock();
	wake_up(&wq);

	return newlock->id;
}

SYSCALL_DEFINE1(rotation_unlock, long, id)
{
	struct rotlock *rl, *nrl;
	long retval = -EINVAL;

	if (id < 0)
		return -EINVAL;

	mutex_lock(&rotlock_list_lock);
	list_for_each_entry_safe (rl, nrl, &rotlock_list, list) {
		if (rl->id == id) {
			if (rl->pid != current->pid) {
				retval = -EPERM;
			} else {
				list_del(&rl->list);
				retval = 0L;
			}

			break;
		}
	}

	if (&rl->list == &rotlock_list) {
		retval = -EINVAL;
		rl = NULL;
	}

	mutex_unlock(&rotlock_list_lock);

	update_next_rotlock();
	wake_up(&wq);

	kfree(rl);
	printk(KERN_INFO "Dobby is free :)\n");

	return retval;
}

void exit_rotation(struct task_struct *tsk)
{
	struct rotlock *rl, *nrl;

	LIST_HEAD(rotlock_deleted_list);

	mutex_lock(&rotlock_list_lock);
	list_for_each_entry_safe (rl, nrl, &rotlock_list, list) {
		if (rl->pid == tsk->pid) {
			list_del(&rl->list);
			list_add(&rl->list, &rotlock_deleted_list);
		}
	}
	mutex_unlock(&rotlock_list_lock);

	list_for_each_entry_safe (rl, nrl, &rotlock_deleted_list, list) {
		list_del(&rl->list);
		printk(KERN_INFO "rotlock %ld is freed\n", rl->id);
		kfree(rl);
	}
}

