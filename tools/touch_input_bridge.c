/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Linux evdev to Zephyr touch IPC bridge.
 *
 * Build: cc -O2 -Wall -Wextra -o touch_input_bridge touch_input_bridge.c
 */

#define _DEFAULT_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define TOUCH_MAX_COORD   32767

#include "../src/touch_ipc_protocol.h"

struct bridge {
	volatile struct touch_ipc_mailbox *mailbox;
	uint8_t slot;
	uint16_t x;
	uint16_t y;
	unsigned int gain;
	unsigned int key_step;
	bool down;
};

static void usage(const char *program)
{
	fprintf(stderr,
		"Usage: %s [--shm NAME] [--keyboard EVENT] [--mouse EVENT] [options]\n"
		"  --shm NAME        POSIX shared-memory mailbox, default " TOUCH_IPC_SHM_NAME "\n"
		"  --keyboard EVENT  keyboard /dev/input/eventN (repeatable)\n"
		"  --mouse EVENT     mouse /dev/input/eventN (repeatable)\n"
		"  --slot N          touch slot, default 0\n"
		"  --gain N          HID units per mouse delta, default 32\n"
		"  --key-step N      HID units per arrow key, default 1024\n"
		"\nMappings: left mouse button or Space/Enter holds touch; mouse movement\n"
		"and arrow keys move the virtual touch position. Escape exits.\n", program);
}

static uint16_t clamp_coordinate(int value)
{
	if (value < 0) {
		return 0;
	}
	if (value > TOUCH_MAX_COORD) {
		return TOUCH_MAX_COORD;
	}
	return (uint16_t)value;
}

static int send_frame(struct bridge *bridge)
{
	uint32_t sequence = __atomic_load_n(&bridge->mailbox->sequence, __ATOMIC_RELAXED);
	uint32_t writing_sequence = (sequence + 1U) | 1U;
	uint32_t position = bridge->x | ((uint32_t)bridge->y << 16);
	uint32_t state = bridge->slot | (bridge->down ? TOUCH_IPC_STATE_DOWN : 0U);

	__atomic_store_n(&bridge->mailbox->sequence, writing_sequence, __ATOMIC_RELAXED);
	__atomic_store_n(&bridge->mailbox->position, position, __ATOMIC_RELAXED);
	__atomic_store_n(&bridge->mailbox->state, state, __ATOMIC_RELAXED);
	__atomic_store_n(&bridge->mailbox->sequence, writing_sequence + 1U, __ATOMIC_RELEASE);

	return 0;
}

static int update_position(struct bridge *bridge, int dx, int dy)
{
	bridge->x = clamp_coordinate((int)bridge->x + dx);
	bridge->y = clamp_coordinate((int)bridge->y + dy);
	return bridge->down ? send_frame(bridge) : 0;
}

static int handle_key(struct bridge *bridge, const struct input_event *event)
{
	bool touch_key = event->code == BTN_LEFT || event->code == KEY_SPACE || event->code == KEY_ENTER;

	if (event->code == KEY_ESC && event->value == 1) {
		return 1;
	}
	if (touch_key && event->value != 2) {
		bridge->down = event->value != 0;
		return send_frame(bridge);
	}
	if (event->value != 1 && event->value != 2) {
		return 0;
	}

	switch (event->code) {
	case KEY_LEFT:
		return update_position(bridge, -(int)bridge->key_step, 0);
	case KEY_RIGHT:
		return update_position(bridge, bridge->key_step, 0);
	case KEY_UP:
		return update_position(bridge, 0, -(int)bridge->key_step);
	case KEY_DOWN:
		return update_position(bridge, 0, bridge->key_step);
	default:
		return 0;
	}
}

int main(int argc, char **argv)
{
	struct pollfd fds[32];
	struct bridge bridge = { .x = TOUCH_MAX_COORD / 2,
		.y = TOUCH_MAX_COORD / 2, .gain = 32, .key_step = 1024 };
	const char *shm_name = TOUCH_IPC_SHM_NAME;
	int fd_count = 0;

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--shm") && ++i < argc) {
			shm_name = argv[i];
		} else if ((!strcmp(argv[i], "--keyboard") || !strcmp(argv[i], "--mouse")) && ++i < argc) {
			if (fd_count == (int)(sizeof(fds) / sizeof(fds[0]))) return EXIT_FAILURE;
			fds[fd_count].fd = open(argv[i], O_RDONLY | O_CLOEXEC);
			fds[fd_count].events = POLLIN;
			if (fds[fd_count].fd < 0) {
				perror(argv[i]);
				return EXIT_FAILURE;
			}
			fd_count++;
		} else if (!strcmp(argv[i], "--slot") && ++i < argc) {
			bridge.slot = (uint8_t)strtoul(argv[i], NULL, 0);
		} else if (!strcmp(argv[i], "--gain") && ++i < argc) {
			bridge.gain = strtoul(argv[i], NULL, 0);
		} else if (!strcmp(argv[i], "--key-step") && ++i < argc) {
			bridge.key_step = strtoul(argv[i], NULL, 0);
		} else {
			usage(argv[0]);
			return EXIT_FAILURE;
		}
	}

	if (fd_count == 0 || bridge.slot > 2) {
		usage(argv[0]);
		return EXIT_FAILURE;
	}

	int shm_fd = shm_open(shm_name, O_CREAT | O_RDWR, 0600);
	if (shm_fd < 0) {
		perror(shm_name);
		return EXIT_FAILURE;
	}
	if (ftruncate(shm_fd, sizeof(*bridge.mailbox)) != 0) {
		perror("ftruncate shared input mailbox");
		(void)close(shm_fd);
		return EXIT_FAILURE;
	}
	bridge.mailbox = mmap(NULL, sizeof(*bridge.mailbox), PROT_READ | PROT_WRITE,
				     MAP_SHARED, shm_fd, 0);
	(void)close(shm_fd);
	if (bridge.mailbox == MAP_FAILED) {
		perror("mmap shared input mailbox");
		return EXIT_FAILURE;
	}

	for (;;) {
		if (poll(fds, (nfds_t)fd_count, -1) < 0) {
			if (errno == EINTR) continue;
			perror("poll");
			return EXIT_FAILURE;
		}
		for (int i = 0; i < fd_count; i++) {
			struct input_event event;
			if (!(fds[i].revents & POLLIN)) continue;
			if (read(fds[i].fd, &event, sizeof(event)) != sizeof(event)) continue;
			int ret = 0;
			if (event.type == EV_KEY) ret = handle_key(&bridge, &event);
			if (event.type == EV_REL && bridge.down) {
				if (event.code == REL_X) ret = update_position(&bridge, event.value * (int)bridge.gain, 0);
				if (event.code == REL_Y) ret = update_position(&bridge, 0, event.value * (int)bridge.gain);
			}
			if (ret == 1) return EXIT_SUCCESS;
			if (ret < 0) {
				errno = -ret;
				perror("touch IPC write");
				return EXIT_FAILURE;
			}
		}
	}
}
