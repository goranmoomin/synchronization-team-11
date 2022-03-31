#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	long id = syscall(295, atoi(argv[1]), atoi(argv[2]), atoi(argv[3]));
	printf("Lock %ld acquired!\n", id);

	getchar();

	syscall(296, id);
	printf("Lock %ld released!\n", id);

	return 0;
}
