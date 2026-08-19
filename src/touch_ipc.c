/* SPDX-License-Identifier: Apache-2.0 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "touch_hid.h"
#include "touch_ipc.h"
#include "touch_ipc_protocol.h"

LOG_MODULE_REGISTER(touch_ipc, LOG_LEVEL_INF);

static volatile struct touch_ipc_mailbox *mailbox;

static void touch_ipc_thread(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	while (true) {
		uint32_t head = __atomic_load_n(&mailbox->head, __ATOMIC_ACQUIRE);
		uint32_t tail = __atomic_load_n(&mailbox->tail, __ATOMIC_RELAXED);

		while (tail != head) {
			uint32_t index = tail & TOUCH_IPC_QUEUE_MASK;
			uint8_t slot = mailbox->queue[index].slot;
			bool down = mailbox->queue[index].down != 0;
			uint16_t x = mailbox->queue[index].x;
			uint16_t y = mailbox->queue[index].y;
			int err = touch_hid_send(slot, down, x, y);

			tail++;
			__atomic_store_n(&mailbox->tail, tail, __ATOMIC_RELEASE);

			if (err != 0 && err != -ENOTCONN) {
				LOG_WRN("Touch frame was not sent: %d", err);
			}
		}
		k_sleep(K_MSEC(1));
	}
}

K_THREAD_STACK_DEFINE(touch_ipc_thread_stack, 1024);
static struct k_thread touch_ipc_thread_data;

int touch_ipc_init(void)
{
	int fd;

	fd = shm_open(TOUCH_IPC_SHM_NAME, O_CREAT | O_RDWR, 0600);
	if (fd < 0) {
		LOG_ERR("Could not open shared input mailbox: %d", errno);
		return -errno;
	}
	if (ftruncate(fd, sizeof(*mailbox)) != 0) {
		int err = errno;

		(void)close(fd);
		LOG_ERR("Could not size shared input mailbox: %d", err);
		return -err;
	}

	mailbox = mmap(NULL, sizeof(*mailbox), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	(void)close(fd);
	if (mailbox == MAP_FAILED) {
		mailbox = NULL;
		LOG_ERR("Could not map shared input mailbox: %d", errno);
		return -errno;
	}

	(void)k_thread_create(&touch_ipc_thread_data, touch_ipc_thread_stack,
			      K_THREAD_STACK_SIZEOF(touch_ipc_thread_stack),
			      touch_ipc_thread,
			      NULL, NULL, NULL, K_PRIO_PREEMPT(2), 0, K_NO_WAIT);
	LOG_INF("Touch IPC ready on POSIX shared memory %s", TOUCH_IPC_SHM_NAME);
	return 0;
}
