#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/wait.h>

#include <liburing.h>

// gcc -o open open.c -luring

static int submit_and_wait(struct io_uring *ring) {
	struct io_uring_cqe *cqe;
	int ret = io_uring_submit(ring);
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

static int io_uring_openat(struct io_uring *ring, const char *path, int dfd, int flags) {
	struct io_uring_sqe *sqe;
	int ret;

	sqe = io_uring_get_sqe(ring);
	if (!sqe) {
		fprintf(stderr, "get sqe failed\n");
		return -1;
	}
	io_uring_prep_openat(sqe, dfd, path, flags, 0);

	return submit_and_wait(ring);
}

static int io_uring_socket(struct io_uring *ring, int domain, int type, int protocol, int flags)
{
	struct io_uring_cqe *cqe;
	struct io_uring_sqe *sqe;
	int ret;

	sqe = io_uring_get_sqe(ring);
	if (!sqe) {
		fprintf(stderr, "get sqe failed\n");
		return -1;
	}
	io_uring_prep_socket(sqe, domain, type, protocol, flags);

	return submit_and_wait(ring);
}

static int io_uring_connect(struct io_uring *ring, int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
	struct io_uring_sqe *sqe;
	int ret;

	sqe = io_uring_get_sqe(ring);
	if (!sqe) {
		fprintf(stderr, "get sqe failed\n");
		return -1;
	}
	io_uring_prep_connect(sqe, sockfd, addr, addrlen);

	return submit_and_wait(ring);
}

int start_server() {
	system("nc -lvnp 9001");
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

	//
	// Open a file for reading
	//
    int fd;
    fd = io_uring_openat(&ring, "/etc/passwd", AT_FDCWD, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "openat failed\n");
		return 1;
    }

    // Read the whole file from the file descriptor, demonstrating that the file was opened
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
	close(fd);

	//
	// Create a socket
	//
	int sockfd;
	sockfd = io_uring_socket(&ring, AF_INET, SOCK_STREAM, 0, 0);
	if (sockfd < 0) {
		fprintf(stderr, "socket failed\n");
		return 1;
	}

	// spawn a server
	pid_t pid = fork();
	if (pid == 0) {
		start_server();
		exit(0);
	} else if (pid < 0) {
		fprintf(stderr, "fork failed\n");
		return 1;
	}

	// Wait for the server to start
	sleep(2);
	struct sockaddr_in serv_addr;
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(9001);
	serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

	ret = io_uring_connect(&ring, sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
	if (ret < 0) {
		fprintf(stderr, "connect failed\n");
		return 1;
	}
	printf("Connected to server\n");
	write(sockfd, "Hello, world!\n", 14);

	// Close the socket and connection
	close(sockfd);

	// Kill the server
	sleep(1);
	kill(pid, SIGKILL);
	waitpid(pid, NULL, 0);
}