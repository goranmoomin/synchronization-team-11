#ifndef _LINUX_ROTATION_H
#define _LINUX_ROTATION_H

#include <linux/types.h>

#define ROT_READ 0
#define ROT_WRITE 1

extern struct mutex rotlock_list_lock;
extern struct list_head rotlock_list;

extern struct mutex rotlock_waitlist_lock;
extern struct list_head rotlock_waitlist;

extern struct mutex next_rotlock_id_lock;
extern long next_rotlock_id;

struct rotlock {
	long id;

	struct list_head list;

	int type;
	int low;
	int high;
	pid_t pid;
};

#endif /* _LINUX_ROTATION_H */
