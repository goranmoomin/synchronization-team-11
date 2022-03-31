#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

int main(int argc, char **argv)
{
	syscall(294, atoi(*++argv));
	return errno;
}
