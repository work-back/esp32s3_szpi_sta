#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zephyr/posix/sys/socket.h>
#include <zephyr/posix/arpa/inet.h>
#include <zephyr/posix/unistd.h>
#include <zephyr/posix/sys/eventfd.h>
#include <zephyr/posix/poll.h>

#include <zephyr/misc/lorem_ipsum.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>

#include "wscli.h"

LOG_MODULE_REGISTER(MAIN, LOG_LEVEL_DBG);

uint8_t recv_buf[MAX_RECV_BUF_LEN];
uint8_t send_buf[MAX_RECV_BUF_LEN];

static struct pollfd fds[2];

static void timer_looper_thrd(void);
static bool send_msg(int sock, uint8_t *buf, size_t buf_len);

#if defined(CONFIG_NET_TC_THREAD_PREEMPTIVE)
#define THREAD_PRIORITY K_PRIO_PREEMPT(8)
#else
#define THREAD_PRIORITY K_PRIO_COOP(CONFIG_NUM_COOP_PRIORITIES - 1)
#endif

#define STACK_SIZE 2048

K_THREAD_DEFINE(timer_thread_id, STACK_SIZE,
		timer_looper_thrd, NULL, NULL, NULL,
		THREAD_PRIORITY,
		IS_ENABLED(CONFIG_USERSPACE) ? K_USER : 0, -1);

static void timer_looper_thrd(void)
{
    LOG_INF("timer_looper_thrd start ...");

    while(1) {
        k_sleep(K_SECONDS(1));

        eventfd_write(fds[1].fd, 1);

        LOG_INF("Sent signal to main thread\n");
    }

    return;
}

static int poll_loop(void)
{
	int ret;

	ret = poll(fds, 2, 30 * 1000);
	if (ret < 0) {
		return ret;
	}

	if (ret == 0) {
		LOG_DBG("poll timeout!");
		return -EAGAIN;
	}

    LOG_DBG("poll event GOT!");

	if (fds[0].revents & POLLNVAL) {
		return -EBADF;
	}

	if (fds[0].revents & POLLERR) {
		return -EIO;
	}

	if (fds[0].revents & POLLIN) {
		wscli_recv(fds[0].fd, recv_buf, sizeof(recv_buf));
        LOG_DBG("receive [%s]", recv_buf);
	}

	if (fds[1].revents) {
		eventfd_t value;

		eventfd_read(fds[1].fd, &value);
		LOG_DBG("Received event.");

        send_msg(fds[0].fd, send_buf, sizeof(send_buf));
	}

	return 0;
}

static bool send_msg(int sock, uint8_t *buf, size_t buf_len)
{
	int ret;

	if (sock < 0) {
		return true;
	}

	ret = snprintf(buf, buf_len, "%s", "hello SRV");;

	ret = wscli_send(sock, buf, ret);
	if (ret <= 0) {
		if (ret < 0) {
			LOG_ERR("failed to send data using %s (%d)", "ws API", ret);
		} else {
			LOG_DBG("connection closed");
		}

		return false;
	} else {
		LOG_DBG("sent %d bytes", ret);
	}

	return true;
}

int main(void)
{
	k_sleep(K_SECONDS(2));

    fds[0].fd = -1;
	fds[1].fd = -1;

	int wait_ret = sta_tryconnect();
	if (wait_ret != 0) {
		LOG_ERR("WiFi connection failed.");
		k_sleep(K_FOREVER);
	}

	k_sleep(K_SECONDS(1));
    LOG_INF("WiFi connected, starting WSCLI...");

	wscli_init();

    int websock = wscli_getsock();
    if (websock < 0) {
		LOG_ERR("invalid websock.");
		k_sleep(K_FOREVER);
    }

    fds[0].fd = websock;
    fds[0].events = POLLIN;

    int evfd = eventfd(0, 0);
    if (evfd < 0) {
        LOG_ERR("eventfd failed: %d, errno: %d", evfd, errno);
		k_sleep(K_FOREVER);
    }
	fds[1].fd = evfd;
	fds[1].events = POLLIN;

	k_thread_start(timer_thread_id);

	while (1) {
        poll_loop();

		k_sleep(K_MSEC(250));
	}

    // k_thread_priority_set(k_current_get(), THREAD_PRIORITY);
	
	while (1) {
		k_sleep(K_SECONDS(1));
	}

	wscli_fini();

	return 0;
}
