#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>

#include <liburing.h>

// gcc -o open open.c -luring

static int io_uring_openat(struct io_uring *ring, const char *path, int dfd, int flags)
{
	struct io_uring_cqe *cqe;
	struct io_uring_sqe *sqe;
	int ret;

	sqe = io_uring_get_sqe(ring);
	if (!sqe) {
		fprintf(stderr, "get sqe failed\n");
		goto err;
	}
	io_uring_prep_openat(sqe, dfd, path, flags, 0);

	ret = io_uring_submit(ring);
	if (ret <= 0) {
		fprintf(stderr, "sqe submit failed: %d\n", ret);
		goto err;
	}

	ret = io_uring_wait_cqe(ring, &cqe);
	if (ret < 0) {
		fprintf(stderr, "wait completion %d\n", ret);
		goto err;
	}

    // The result contains the file descriptor
	ret = cqe->res;
	io_uring_cqe_seen(ring, cqe);
	return ret;
err:
	return -1;
}

int main() {
    struct io_uring ring;
	int ret;

    // setup io_uring submission and completion queues
	ret = io_uring_queue_init(8, &ring, 0);
	if (ret) {
		fprintf(stderr, "ring setup failed\n");
		return 1;
	}

    int fd;
    fd = io_uring_openat(&ring, "/etc/passwd", AT_FDCWD, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "openat failed\n");
		return 1;
    }

    // Read the whole file from the file descriptor
    char buf[1024];
    while (1) {
        ret = read(fd, buf, sizeof(buf));
        if (ret < 0) {
            fprintf(stderr, "read failed\n");
            return 1;
        }
        if (ret == 0) {
            break;
        }
        write(1, buf, ret);
    }
}