/* SPDX-License-Identifier: Apache-2.0 */

#ifndef TOUCH_IPC_PROTOCOL_H_
#define TOUCH_IPC_PROTOCOL_H_

#include <stdint.h>

/* POSIX shared-memory object used by the native_sim input backend. */
#define TOUCH_IPC_SHM_NAME "/zephyr-touch-ipc"
#define TOUCH_IPC_QUEUE_SIZE 128U
#define TOUCH_IPC_QUEUE_MASK (TOUCH_IPC_QUEUE_SIZE - 1U)

struct touch_ipc_event {
	uint16_t x;
	uint16_t y;
	uint8_t slot;
	uint8_t down;
	uint16_t reserved;
};

/*
 * Lock-free single-producer single-consumer ring buffer between Linux input bridge
 * and Zephyr native_sim receiver thread.
 */
struct touch_ipc_mailbox {
	uint32_t head; /* Incremented by producer (bridge) */
	uint32_t tail; /* Incremented by consumer (Zephyr) */
	struct touch_ipc_event queue[TOUCH_IPC_QUEUE_SIZE];
};

#define TOUCH_IPC_STATE_DOWN (1U << 8)

#endif /* TOUCH_IPC_PROTOCOL_H_ */
