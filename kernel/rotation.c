#include <linux/list.h>
#include <linux/rotation.h>
#include <linux/syscalls.h>

LIST_HEAD(rot_lock_list);

SYSCALL_DEFINE1(set_orientation, int, degree)
{
	return 0;
}

SYSCALL_DEFINE3(rotation_lock, int, low, int, high, int, type)
{
	return 0;
}

SYSCALL_DEFINE1(rotation_unlock, long, id)
{
	return 0;
}
