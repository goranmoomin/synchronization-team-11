#include <stdio.h>
#include <unistd.h>

int main()
{
	int id = syscall(295, 1, 180, 0);
	printf("Lock acquired!\n");

	syscall(296, id);
	printf("Lock released!\n");
	return 0;
}
