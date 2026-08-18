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

static bool read_frame(uint32_t *last_sequence, uint8_t *slot, bool *down,
		       uint16_t *x, uint16_t *y)
{
	uint32_t begin;
	uint32_t end;
	uint32_t position;
	uint32_t state;

	begin = __atomic_load_n(&mailbox->sequence, __ATOMIC_ACQUIRE);
	if (begin == *last_sequence || (begin & 1U)) {
		return false;
	}

	position = __atomic_load_n(&mailbox->position, __ATOMIC_RELAXED);
	state = __atomic_load_n(&mailbox->state, __ATOMIC_RELAXED);
	end = __atomic_load_n(&mailbox->sequence, __ATOMIC_ACQUIRE);
	if (begin != end || (end & 1U)) {
		return false;
	}

	*last_sequence = end;
	*slot = state & 0xffU;
	*down = (state & TOUCH_IPC_STATE_DOWN) != 0U;
	*x = position & 0xffffU;
	*y = position >> 16;
	return true;
}

static void touch_ipc_thread(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	uint32_t last_sequence = __atomic_load_n(&mailbox->sequence, __ATOMIC_ACQUIRE);

	while (true) {
		uint8_t slot;
		bool down;
		uint16_t x;
		uint16_t y;

		if (read_frame(&last_sequence, &slot, &down, &x, &y)) {
			int err = touch_hid_send(slot, down, x, y);

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
