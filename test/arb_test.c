#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	if (argc != 4) {
		fprintf(stderr, "Usage: %s [low] [high] [type]\n", argv[0]);
		return 1;
	}
	long id = syscall(295, atoi(argv[1]), atoi(argv[2]), atoi(argv[3]));
	printf("Lock %ld acquired!\n", id);

	getchar();

	syscall(296, id);
	printf("Lock %ld released!\n", id);

	return 0;
}
