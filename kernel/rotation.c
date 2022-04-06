#include <linux/rotation.h>
#include <linux/slab.h>
#include <linux/syscalls.h>
#include <linux/types.h>
#include <linux/wait.h>

static DECLARE_WAIT_QUEUE_HEAD(wq);

static DEFINE_RWLOCK(orientation_lock);
static int orientation = 0;

static DEFINE_RWLOCK(rotlock_list_lock);
static LIST_HEAD(rotlock_list);

static DEFINE_SPINLOCK(new_rotlock_id_lock);
static long new_rotlock_id = 0L;

#define VALID_ORIENTATION(low, high, ori)                                      \
	((low) <= (high) ? ((low) <= (ori) && (ori) <= (high)) :               \
			   ((low) <= (ori) && (ori) < 360) ||                  \
				   (0 <= (ori) && (ori) <= (high)))

#define OVERLAP_INTERVAL(l1, h1, l2, h2)                                       \
	((l1) <= (h1) ? ((l2) <= (h2) ? (l2) <= (h1) && (l1) <= (h2) :         \
					(l1) <= (h2) || (l2) <= (h1)) :        \
			((l2) <= (h2) ? (l2) <= (h1) || (l1) <= (h2) : 1))

static void update_rotlocks(void)
{
	struct rotlock *rl, *aux_rl, *candidate = NULL;

	read_lock(&rotlock_list_lock);

	list_for_each_entry (rl, &rotlock_list, list) {
		spin_lock(&rl->lock);
	}

	read_lock(&orientation_lock);

	list_for_each_entry (rl, &rotlock_list, list) {
		if (!(rl->type == ROT_WRITE && rl->state == ROTLOCK_WAITING)) {
			continue;
		}
		if (VALID_ORIENTATION(rl->low, rl->high, orientation)) {
			candidate = rl;
			list_for_each_entry (aux_rl, &rotlock_list, list) {
				if (aux_rl->state == ROTLOCK_WAITING) {
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
				candidate->state = ROTLOCK_ACQUIRED;
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
				if (aux_rl->type == ROT_READ ||
				    (aux_rl->state == ROTLOCK_WAITING &&
				     !VALID_ORIENTATION(aux_rl->low,
							aux_rl->high,
							orientation))) {
					continue;
				}
				if (OVERLAP_INTERVAL(rl->low, rl->high,
						     aux_rl->low,
						     aux_rl->high)) {
					candidate = NULL;
					break;
				}
			}
			if (candidate) {
				candidate->state = ROTLOCK_ACQUIRED;
			}
		}
	}

	read_unlock(&orientation_lock);

	list_for_each_entry_reverse (rl, &rotlock_list, list) {
		spin_unlock(&rl->lock);
	}
	read_unlock(&rotlock_list_lock);

	wake_up(&wq);
}

static int is_acquired(struct rotlock *rl)
{
	int retval;

	spin_lock(&rl->lock);
	retval = rl->state == ROTLOCK_ACQUIRED;
	spin_unlock(&rl->lock);

	return retval;
}

SYSCALL_DEFINE1(set_orientation, int, degree)
{
	printk(KERN_INFO "set_orientation degree=%d\n", degree);
	if (degree < 0 || degree >= 360)
		return -EINVAL;

	write_lock(&orientation_lock);
	orientation = degree;
	write_unlock(&orientation_lock);

	update_rotlocks();

	return 0;
}

SYSCALL_DEFINE3(rotation_lock, int, low, int, high, int, type)
{
	struct rotlock *newlock;

	printk(KERN_INFO "rotation_lock low=%d high=%d type=%d\n", low, high,
	       type);

	if (low < 0 || low >= 360 || high < 0 || high >= 360 ||
	    (type != ROT_READ && type != ROT_WRITE)) {
		return -EINVAL;
	}

	newlock = kmalloc(sizeof(*newlock), GFP_KERNEL);
	if (!newlock)
		return -ENOMEM;

	spin_lock(&new_rotlock_id_lock);
	newlock->id = new_rotlock_id++;
	spin_unlock(&new_rotlock_id_lock);

	spin_lock_init(&newlock->lock);

	newlock->state = ROTLOCK_WAITING;
	newlock->type = type;
	newlock->low = low;
	newlock->high = high;
	newlock->pid = current->pid;

	write_lock(&rotlock_list_lock);
	list_add_tail(&newlock->list, &rotlock_list);
	write_unlock(&rotlock_list_lock);

	update_rotlocks();

	wait_event(wq, is_acquired(newlock));

	return newlock->id;
}

SYSCALL_DEFINE1(rotation_unlock, long, id)
{
	struct rotlock *rl, *nrl;
	long retval = -EINVAL;

	printk(KERN_INFO "rotation_unlock id=%ld\n", id);

	if (id < 0)
		return -EINVAL;

	write_lock(&rotlock_list_lock);
	list_for_each_entry_safe (rl, nrl, &rotlock_list, list) {
		if (rl->id == id) {
			if (rl->pid != current->pid) {
				retval = -EPERM;
			} else {
				spin_lock(&rl->lock);
				if (rl->state == ROTLOCK_ACQUIRED) {
					list_del(&rl->list);
					retval = 0L;
				} else {
					retval = -EINVAL;
				}
				spin_unlock(&rl->lock);
			}

			break;
		}
	}

	if (retval) {
		rl = NULL;
	}

	write_unlock(&rotlock_list_lock);

	update_rotlocks();

	kfree(rl);

	return retval;
}

void exit_rotation(struct task_struct *tsk)
{
	struct rotlock *rl, *nrl;

	LIST_HEAD(rotlock_deleted_list);

	write_lock(&rotlock_list_lock);
	list_for_each_entry_safe (rl, nrl, &rotlock_list, list) {
		if (rl->pid == tsk->pid) {
			list_del(&rl->list);
			list_add(&rl->list, &rotlock_deleted_list);
		}
	}
	write_unlock(&rotlock_list_lock);

	list_for_each_entry_safe (rl, nrl, &rotlock_deleted_list, list) {
		list_del(&rl->list);
		kfree(rl);
	}

	update_rotlocks();
}

