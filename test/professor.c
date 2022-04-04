#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

#define ROT_READ 0
#define ROT_WRITE 1

#define NR_ROTATION_LOCK 295
#define NR_ROTATION_UNLOCK 296

int main(int argc, char **argv)
{
	if (argc != 2) {
		fprintf(stderr, "Usage: %s [start]\n", argv[0]);
		return 1;
	}
	long id;
	int val = 0;
	for (;;) {
		FILE *f = NULL;
		id = syscall(NR_ROTATION_LOCK, 0, 180, ROT_WRITE);
		if (id < 0) {
			continue;
		}
		f = fopen("quiz", "w");
		if (f) {
			fprintf(f, "%d\n", val);
			fclose(f);
			printf("professor: %d\n", val);
			val++;
		}
		syscall(NR_ROTATION_UNLOCK, id);
	}

	return 0;
}
