/* SPDX-License-Identifier: Apache-2.0 */

#ifndef TOUCH_IPC_PROTOCOL_H_
#define TOUCH_IPC_PROTOCOL_H_

#include <stdint.h>

/* POSIX shared-memory object used by the native_sim input backend. */
#define TOUCH_IPC_SHM_NAME "/zephyr-touch-ipc"

/*
 * A latest-state mailbox, shared by the Linux input bridge and Zephyr.
 * sequence is a seqlock: odd while the producer updates a frame, even once
 * the update is committed.  All fields must remain naturally aligned 32-bit
 * values so both processes can access them with atomic builtins.
 */
struct touch_ipc_mailbox {
	uint32_t sequence;
	uint32_t position; /* x in bits 0..15, y in bits 16..31 */
	uint32_t state;    /* slot in bits 0..7, down in bit 8 */
};

#define TOUCH_IPC_STATE_DOWN (1U << 8)

#endif /* TOUCH_IPC_PROTOCOL_H_ */
