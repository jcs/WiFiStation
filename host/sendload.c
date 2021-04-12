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
#include <sys/ioctl.h>
#include <sys/stat.h>

static int debug = 0;
static char vbuf[512];
static int serial_fd = -1;

void
usage(void)
{
	errx(1, "usage: %s [-d] [-s serial speed] <serial device> <file>",
	    getprogname());
}

void
serial_setup(int fd, speed_t speed)
{
	struct termios tty;

	if (ioctl(fd, TIOCEXCL) != 0)
		err(1, "ioctl(TIOCEXCL)");
	if (tcgetattr(fd, &tty) < 0)
		err(1, "tcgetattr");

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

	cfsetspeed(&tty, speed);

	if (tcsetattr(fd, TCSAFLUSH, &tty) != 0)
		err(1, "tcsetattr");
}

size_t
serial_read(void *buf, size_t nbytes)
{
	size_t ret;

	ret = read(serial_fd, buf, nbytes);

	if (debug && ret > 0) {
		strvisx(vbuf, buf, ret, VIS_NL | VIS_CSTYLE | VIS_OCTAL);
		printf("<<< %s\n", vbuf);
	}

	return ret;
}

size_t
serial_write(const void *buf, size_t nbytes)
{
	if (debug) {
		strvisx(vbuf, buf, nbytes, VIS_NL | VIS_CSTYLE | VIS_OCTAL);
		printf(">>> %s\n", vbuf);
	}

	return write(serial_fd, buf, nbytes);
}

int
main(int argc, char *argv[])
{
	FILE *pFile;
	struct stat sb;
	struct pollfd pfd[1];
	char *fn, *serial_dev = NULL;
	char buf[128], b, cksum = 0, rcksum;
	unsigned int sent = 0, size = 0;
	int len, rlen, ch;
	int serial_speed = B115200;

	while ((ch = getopt(argc, argv, "ds:")) != -1) {
		switch (ch) {
		case 'd':
			debug = 1;
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

	if (argc != 2)
		usage();

	serial_dev = argv[0];
	serial_fd = open(serial_dev, O_RDWR|O_SYNC);
	if (serial_fd < 0)
		err(1, "open %s", serial_dev);

	serial_setup(serial_fd, serial_speed);

	fn = argv[1];
	pFile = fopen(fn, "rb");
	if (!pFile)
		err(1, "open %s", fn);

	if (fstat(fileno(pFile), &sb) != 0)
		err(1, "fstat %s", fn);

	/* we're never going to send huge files */
	size = (unsigned int)sb.st_size;

	if (debug)
		printf("sending %s (%d bytes) via %s\n", fn, size, serial_dev);

	/*
	 * spam some newlines since the TTL connection kinda sucks, and ^C in
	 * case the device is in AT$PINS? mode
	 */
	serial_write("\r\n\r\n", 4);
	b = 3;
	serial_write(&b, 1);

	/*
	 * send AT to get some output since sometimes the first character is
	 * lost and we'll just get 'T', and we need to see a full response to
	 * AT$UPLOAD later
	 */
	serial_write("AT\r", 4);
	pfd[0].fd = serial_fd;
	pfd[0].events = POLLIN;
	while (poll(pfd, 1, 100) > 0)
		serial_read(buf, sizeof(buf));

	len = snprintf(buf, sizeof(buf), "AT$UPLOAD %d\r", size);
	serial_write(buf, len);
	memset(buf, 0, sizeof(buf));

	/* it will echo, along with an OK */
	rlen = 0;
	while (poll(pfd, 1, 100) > 0) {
		len = serial_read(buf + rlen, sizeof(buf) - rlen);
		if (sizeof(buf) - rlen <= 0)
			break;
		rlen += len;
	}

	len = 0;
	if (sscanf(buf, "AT$UPLOAD %d\r\nOK%n", &len, &len) != 1 || len < 10) {
		strvis(vbuf, buf, VIS_NL | VIS_CSTYLE | VIS_OCTAL);
		errx(1, "bad response to AT$UPLOAD: %s", vbuf);
	}

	/* clear out any remaining response so we can read each byte echoed */
	pfd[0].fd = serial_fd;
	pfd[0].events = POLLIN;
	while (poll(pfd, 1, 100) > 0)
		serial_read(buf, sizeof(buf));

	while (sent < size) {
		b = fgetc(pFile);
		write(serial_fd, &b, 1);
		cksum ^= b;
		sent++;

		if (sent % 32 == 0) {
			if (poll(pfd, 1, 100) < 1 ||
			    read(serial_fd, &rcksum, 1) != 1 || rcksum != cksum) {
				printf("\n");
				errx(1, "failed checksum of byte %d/%d "
				    "(expected 0x%x, received 0x%x)",
				    sent, size, cksum & 0xff, rcksum & 0xff);
			}
		}

		printf("\rsent: %05d/%05d", sent, size);
		fflush(stdout);
	}
	fclose(pFile);

	printf("\n");

	/* wait for our final OK */
	while (poll(pfd, 1, 200) > 0)
		serial_read(buf, sizeof(buf));

	close(serial_fd);

	return 0;
}
