/*
 * WiFiStation
 * Serial port program loader
 *
 * Copyright (c) 2019-2021 joshua stein <jcs@jcs.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <termios.h>
#include <unistd.h>
#include <vis.h>
#include <sys/stat.h>

static int debug = 0;

void
usage(void)
{
	errx(1, "usage: %s [-d] [-p serial device] [-s serial speed] "
	    "<file to send>", getprogname());
}

void
setup_serial(int fd, speed_t speed)
{
	struct termios tty;

	if (tcgetattr(fd, &tty) < 0)
		err(1, "tcgetattr");

	cfsetospeed(&tty, speed);
	cfsetispeed(&tty, speed);

	tty.c_cflag |= (CLOCAL | CREAD);	/* ignore modem controls */
	tty.c_cflag &= ~CSIZE;
	tty.c_cflag |= CS8;			/* 8-bit characters */
	tty.c_cflag &= ~PARENB;			/* no parity bit */
	tty.c_cflag &= ~CSTOPB;			/* only need 1 stop bit */

	/* setup for non-canonical mode */
	tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
	tty.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
	tty.c_oflag &= ~OPOST;

	/* fetch bytes as they become available */
	tty.c_cc[VMIN] = 1;
	tty.c_cc[VTIME] = 1;

	if (tcsetattr(fd, TCSANOW, &tty) != 0)
		err(1, "tcsetattr");
}

int
main(int argc, char *argv[])
{
	FILE *pFile;
	struct stat sb;
	struct pollfd pfd[1];
	unsigned int sent = 0, size = 0;
	int len, ch, serial_fd;
	char *fn, *serial_dev = NULL;
	int serial_speed = B115200;
	char buf[128];
	char b, be;

	while ((ch = getopt(argc, argv, "dp:s:")) != -1) {
		switch (ch) {
		case 'd':
			debug = 1;
			break;
		case 'p':
			if ((serial_dev = strdup(optarg)) == NULL)
				err(1, "strdup");
			serial_fd = open(serial_dev, O_RDWR|O_NOCTTY|O_SYNC);
			if (serial_fd < 0)
				err(1, "can't open %s", optarg);
			break;
		case 's':
			serial_speed = (unsigned)strtol(optarg, NULL, 0);
			if (errno)
				err(1, "invalid serial port speed value");
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc != 1)
		usage();

	setup_serial(serial_fd, serial_speed);

	fn = argv[0];
	pFile = fopen(fn, "rb");
	if (!pFile)
		err(1, "open: %s", fn);

	if (fstat(fileno(pFile), &sb) != 0)
		err(1, "fstat: %s", fn);

	/* we're never going to send huge files */
	size = (unsigned int)sb.st_size;

	if (debug)
		printf("sending %s (%d bytes) via %s\n", fn, size, serial_dev);

	/* spam some newlines since the TTL connection kinda sucks */
	write(serial_fd, "\r\n\r\n", 4);

	/* clear out any junk before sending our command */
	pfd[0].fd = serial_fd;
	pfd[0].events = POLLIN;
	while (poll(pfd, 1, 100) > 0)
		read(serial_fd, buf, sizeof(buf));

	/* send the actual command */
	len = snprintf(buf, sizeof(buf), "AT$UPLOAD %d\r\n", size);
	write(serial_fd, &buf, len - 1);

	/* it will echo, along with an OK */
	memset(buf, 0, sizeof(buf));
	read(serial_fd, buf, sizeof(buf));
	len = 0;
	if (sscanf(buf, "AT$UPLOAD %d\r\nOK%n", &len, &len) != 1 || len < 10) {
		char *v;
		stravis(&v, buf, VIS_NL | VIS_CSTYLE);
		errx(1, "bad response to AT$UPLOAD: %s", v);
	}

	/* clear out any remaining response so we can read each byte echoed */
	pfd[0].fd = serial_fd;
	pfd[0].events = POLLIN;
	while (poll(pfd, 1, 250) > 0)
		read(serial_fd, buf, sizeof(buf));

	while (sent < size) {
		b = fgetc(pFile);
		write(serial_fd, &b, 1);
		if (read(serial_fd, &be, 1) != 1 || be != b) {
			printf("\n");
			errx(1, "failed echo of byte %d/%d (sent 0x%x, "
			    "received 0x%x)", sent, size, b, be);
		}

		sent++;
		printf("\rsent: %06d/%06d", sent, size);
		fflush(stdout);
	}
	fclose(pFile);

	printf("\n");

	return 0;
}
