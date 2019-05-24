#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <string.h>
#include <termios.h>

int main(int argc, char **argv) {
	FILE *log = NULL;
	struct termios  tty;
	int i;
	FILE *fd;

	if (argc > 1) {
		log = fopen(argv[1], "wb");
		if (!log) {
			perror("Couldn't open log");
			return 1;
		}
	}

	for (i = 0; i < 65536; i++) {
		fd = fopen("/dev/ttyACM0", "rb");
		if (fd) {
			break;
		}
		usleep(1);
	}
	if (!fd) {
		fprintf(stderr, "Unable to open after %d tries\n", i);
		perror("Couldn't open!");
		return 1;
	}

	tcgetattr(fileno(fd), &tty);
	cfmakeraw(&tty);
	tty.c_cflag &= ~CSTOPB;
	tty.c_cflag &= ~CRTSCTS;    /* no HW flow control */
	tty.c_cflag |= CLOCAL | CREAD;
	tcsetattr(fileno(fd), TCSANOW, &tty);

	while (1) {
		char in_bfr[1024];

		memset(in_bfr, 0, sizeof(in_bfr));

		if (!fgets(in_bfr, sizeof(in_bfr), fd)) {
			return 1;
		}

		printf("%s", in_bfr);
		if (log)
			fprintf(log, "%s", in_bfr);
		if (!strncmp(in_bfr, "FOMU:", strlen("FOMU:"))) {
			break;
		}
	}

	return 0;
}
