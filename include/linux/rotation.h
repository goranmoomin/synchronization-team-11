#ifndef _LINUX_ROTATION_H
#define _LINUX_ROTATION_H

#include <linux/types.h>

#define ROT_READ 0
#define ROT_WRITE 1

extern struct list_head rot_lock_list;

struct rot_lock {
	long id;
	int low;
	int high;
	pid_t pid;
};

#endif /* _LINUX_ROTATION_H */
