#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>
#include <poll.h>
#include "rpi.h"

#define GPIO 26

static int write_file(const char *filename, const char *string) {
	int fd = open(filename, O_WRONLY);
	if (fd == -1)
		return fd;

	write(fd, string, strlen(string));
	close(fd);
	return 0;
}

static int export_gpio(int gpio) {
	char filename[512];
	char string[128];

	snprintf(filename, sizeof(filename)-1, "/sys/class/gpio/export");
	snprintf(string, sizeof(string)-1, "%d", gpio);
	if (write_file(filename, string))
		return -1;

	snprintf(filename, sizeof(filename)-1, "/sys/class/gpio/gpio%d/direction", gpio);
	snprintf(string, sizeof(string)-1, "in");
	if (write_file(filename, string))
		return -1;

	snprintf(filename, sizeof(filename)-1, "/sys/class/gpio/gpio%d/edge", gpio);
	snprintf(string, sizeof(string)-1, "falling");
	if (write_file(filename, string))
		return -1;

	return 0;
}

int main(int argc, char *argv[])
{
	char str[256];
	struct pollfd pfd;
	int fd, gpio;
	char buf[8];

	if (argc > 1)
		gpio = atoi(argv[1]);
	else
		gpio = GPIO;

	if (gpioInitialise()) {
		fprintf(stderr, "Unable to initialize GPIO! Are you running as root?\n");
		return 0;
	}

	gpioSetPullUpDown(gpio, PI_PUD_UP);

	if (export_gpio(gpio)) {
		perror("Unable to export GPIO");
		return 1;
	}

	snprintf(str, sizeof(str)-1, "/sys/class/gpio/gpio%d/value", gpio);

	if ((fd = open(str, O_RDONLY)) < 0)
	{
		fprintf(stderr, "Failed, gpio %d not exported.\n", gpio);
		exit(1);
	}

	pfd.fd = fd;

	pfd.events = POLLPRI;

	lseek(fd, 0, SEEK_SET);    /* consume any prior interrupt */
	read(fd, buf, sizeof buf);

	while (1) {
		poll(&pfd, 1, -1);         /* wait for interrupt */

		lseek(fd, 0, SEEK_SET);    /* consume interrupt */
		read(fd, buf, sizeof buf);
		//fprintf(stderr, "Delay...");
		usleep(1000);

		/* Read value after brief debounce */
		lseek(fd, 0, SEEK_SET);
		//fprintf(stderr, "Re-read...");
		read(fd, buf, sizeof buf);
		if (buf[0] != '0')
			continue;
		printf("START\n");
		fflush(stdout);
		usleep(1000);
	}

	exit(0);
}
