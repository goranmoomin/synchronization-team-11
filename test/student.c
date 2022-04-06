#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

#define ROT_READ 0
#define ROT_WRITE 1

#define NR_ROTATION_LOCK 295
#define NR_ROTATION_UNLOCK 296

void factorize(int n);

int main(int argc, char **argv)
{
	if (argc != 3) {
		fprintf(stderr, "Usage: %s [low] [high]\n", argv[0]);
		return 1;
	}

	int low = atoi(argv[1]);
	int high = atoi(argv[2]);

	long id;
	int val = 0;
	for (;;) {
		FILE *f = NULL;
		id = syscall(NR_ROTATION_LOCK, low, high, ROT_READ);
		if (id < 0) {
			continue;
		}
		f = fopen("quiz", "r");
		if (f) {
			fscanf(f, "%d", &val);
			fclose(f);
			printf("student-%d-%d: %d = ", low, high, val);

			if (val == 1) {
				printf("1");
			}

			for (int i = 2; i * i <= val;) {
				if (val % i) {
					i++;
				} else {
					val /= i;
					printf("%d%s", i,
					       val == 1 ? "" : " * ");
				}
			}

			if (val > 1) {
				printf("%d", val);
			}
			printf("\n");
		}
		syscall(NR_ROTATION_UNLOCK, id);
	}

	return 0;
}
