#ifndef _LINUX_ROTATION_H
#define _LINUX_ROTATION_H

#include <linux/types.h>
#include <linux/spinlock_types.h>

#define ROT_READ 0
#define ROT_WRITE 1

enum rotlock_state { ROTLOCK_WAITING, ROTLOCK_ACQUIRED };

struct rotlock {
	long id;

	spinlock_t lock;
	struct list_head list;

	enum rotlock_state state;
	int type;
	int low;
	int high;
	pid_t pid;
};

void exit_rotation(struct task_struct *tsk);

#endif /* _LINUX_ROTATION_H */
