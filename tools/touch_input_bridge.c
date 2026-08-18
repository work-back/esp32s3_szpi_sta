/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Linux evdev to Zephyr touch IPC bridge.
 *
 * Build: cc -O2 -Wall -Wextra -o touch_input_bridge touch_input_bridge.c
 */

#define _DEFAULT_SOURCE

#include <errno.h>
#include <ctype.h>
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
#define MAP_MAX_KEYS      32

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

struct touch_region {
	bool circle;
	int x;
	int y;
	int width;
	int height;
	int radius;
	int random_percent;
};

struct mapped_key {
	unsigned int code;
	struct touch_region region;
	bool down;
};

struct touch_map {
	int width;
	int height;
	bool has_mouse;
	bool has_view;
	struct touch_region mouse;
	struct touch_region view;
	struct mapped_key keys[MAP_MAX_KEYS];
	size_t key_count;
};

static void usage(const char *program)
{
	fprintf(stderr,
		"Usage: %s [--shm NAME] [--map FILE] [--keyboard EVENT] [--mouse EVENT] [options]\n"
		"  --shm NAME        POSIX shared-memory mailbox, default " TOUCH_IPC_SHM_NAME "\n"
		"  --map FILE        map.json exported by keymap_editor.html\n"
		"  --keyboard EVENT  keyboard /dev/input/eventN (repeatable)\n"
		"  --mouse EVENT     mouse /dev/input/eventN (repeatable)\n"
		"  --slot N          touch slot, default 0\n"
		"  --gain N          HID units per mouse delta, default 32\n"
		"  --key-step N      HID units per arrow key, default 1024\n"
		"\nMappings: left mouse button or Space/Enter holds touch; mouse movement\n"
		"and arrow keys move the virtual touch position. Escape exits.\n", program);
}

static const char *skip_space(const char *text)
{
	while (*text && isspace((unsigned char)*text)) text++;
	return text;
}

static const char *json_value(const char *object, const char *name)
{
	char needle[64];
	int length = snprintf(needle, sizeof(needle), "\"%s\"", name);
	const char *value;

	if (length < 0 || (size_t)length >= sizeof(needle)) return NULL;
	value = strstr(object, needle);
	if (!value) return NULL;
	value = strchr(value + length, ':');
	return value ? skip_space(value + 1) : NULL;
}

static bool json_int(const char *object, const char *name, int *result)
{
	char *end;
	const char *value = json_value(object, name);
	if (!value) return false;
	double number = strtod(value, &end);
	if (end == value || number < INT32_MIN || number > INT32_MAX) return false;
	*result = (int)(number >= 0 ? number + 0.5 : number - 0.5);
	return true;
}

static bool json_string(const char *object, const char *name, char *result, size_t size)
{
	const char *value = json_value(object, name);
	const char *end;
	if (!value || *value != '"') return false;
	value++;
	end = strchr(value, '"');
	if (!end || (size_t)(end - value) >= size) return false;
	memcpy(result, value, (size_t)(end - value));
	result[end - value] = '\0';
	return true;
}

static const char *json_object_end(const char *start)
{
	int depth = 0;
	bool quote = false;
	for (const char *p = start; *p; p++) {
		if (*p == '"' && (p == start || p[-1] != '\\')) quote = !quote;
		if (quote) continue;
		if (*p == '{') depth++;
		else if (*p == '}' && --depth == 0) return p + 1;
	}
	return NULL;
}

static const char *json_named_object(const char *document, const char *name, const char **end)
{
	const char *value = json_value(document, name);
	if (!value || *value != '{') return NULL;
	*end = json_object_end(value);
	return *end ? value : NULL;
}

static bool parse_region(const char *object, struct touch_region *region)
{
	char shape[12];
	if (!json_string(object, "shape", shape, sizeof(shape)) || !json_int(object, "x", &region->x) ||
	    !json_int(object, "y", &region->y) || !json_int(object, "random_percent", &region->random_percent)) return false;
	region->random_percent = region->random_percent < 0 ? 0 : region->random_percent > 100 ? 100 : region->random_percent;
	region->circle = !strcmp(shape, "circle");
	return region->circle ? json_int(object, "radius", &region->radius) && region->radius > 0 :
		json_int(object, "width", &region->width) && json_int(object, "height", &region->height) && region->width > 0 && region->height > 0;
}

static unsigned int key_code(const char *name)
{
	if (!strcmp(name, "KEY_SPACE")) return KEY_SPACE;
	if (!strcmp(name, "KEY_ENTER")) return KEY_ENTER;
	if (!strcmp(name, "KEY_SHIFT")) return KEY_LEFTSHIFT;
	if (!strcmp(name, "KEY_CTRL")) return KEY_LEFTCTRL;
	if (!strcmp(name, "KEY_ALT")) return KEY_LEFTALT;
	if (!strncmp(name, "KEY_", 4) && name[4] && !name[5] && name[4] >= 'A' && name[4] <= 'Z') return KEY_A + name[4] - 'A';
	if (!strncmp(name, "KEY_", 4) && name[4] >= '0' && name[4] <= '9' && !name[5]) return KEY_0 + name[4] - '0';
	return 0;
}

static char *read_file(const char *path)
{
	FILE *file = fopen(path, "rb");
	long length;
	char *contents;
	if (!file) return NULL;
	if (fseek(file, 0, SEEK_END) || (length = ftell(file)) < 0 || fseek(file, 0, SEEK_SET)) { fclose(file); return NULL; }
	contents = calloc((size_t)length + 1, 1);
	if (!contents || fread(contents, 1, (size_t)length, file) != (size_t)length) { free(contents); fclose(file); return NULL; }
	fclose(file);
	return contents;
}

static bool load_map(const char *path, struct touch_map *map)
{
	char *document = read_file(path);
	const char *end;
	const char *object;
	if (!document) return false;
	memset(map, 0, sizeof(*map));
	object = json_named_object(document, "screen", &end);
	if (!object || !json_int(object, "width", &map->width) || !json_int(object, "height", &map->height) || map->width <= 0 || map->height <= 0) goto out;
	if ((object = json_named_object(document, "mouse_joystick", &end))) map->has_mouse = parse_region(object, &map->mouse);
	if ((object = json_named_object(document, "view_joystick", &end))) map->has_view = parse_region(object, &map->view);
	const char *array = json_value(document, "keys");
	if (array && *array == '[') for (const char *p = array + 1; (p = strchr(p, '{')) && map->key_count < MAP_MAX_KEYS;) {
		const char *next = json_object_end(p); char name[32]; struct mapped_key *key = &map->keys[map->key_count];
		if (!next) goto out;
		if (json_string(p, "key", name, sizeof(name)) && parse_region(p, &key->region) && (key->code = key_code(name))) map->key_count++;
		p = next;
	}
	free(document); return map->has_mouse || map->has_view || map->key_count;
out: free(document); return false;
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

static int send_touch(struct bridge *bridge, uint8_t slot, bool down, int x, int y)
{
    fprintf(stderr, "T(%d, %d)\n", x, y);
	bridge->slot = slot;
	bridge->down = down;
	bridge->x = clamp_coordinate(x);
	bridge->y = clamp_coordinate(y);
	return send_frame(bridge);
}

static int screen_x(const struct touch_map *map, int x) { return x * TOUCH_MAX_COORD / map->width; }
static int screen_y(const struct touch_map *map, int y) { return y * TOUCH_MAX_COORD / map->height; }

static void region_center(const struct touch_region *region, int *x, int *y)
{
	*x = region->x;
	*y = region->y;
	if (!region->circle) { *x += region->width / 2; *y += region->height / 2; }
}

static int joystick_touch(struct bridge *bridge, const struct touch_map *map,
			  const struct touch_region *region, int dx, int dy, bool down)
{
	int x, y, extent;
	region_center(region, &x, &y);
	if (!down) return send_touch(bridge, region == &map->mouse ? 0 : 1, false, screen_x(map, x), screen_y(map, y));
	extent = region->circle ? region->radius : (region->width < region->height ? region->width : region->height) / 2;
	if (dx > extent) dx = extent;
	if (dx < -extent) dx = -extent;
	if (dy > extent) dy = extent;
	if (dy < -extent) dy = -extent;
	return send_touch(bridge, region == &map->mouse ? 0 : 1, true, screen_x(map, x + dx), screen_y(map, y + dy));
}

static int random_touch(struct bridge *bridge, const struct touch_map *map, const struct touch_region *region)
{
	int x, y, spread = region->random_percent;
	if (region->circle) {
		int radius = region->radius * spread / 100;
		int dx, dy;
		do {
			dx = (rand() % (radius * 2 + 1)) - radius;
			dy = (rand() % (radius * 2 + 1)) - radius;
		} while (dx * dx + dy * dy > radius * radius);
		x = region->x + dx;
		y = region->y + dy;
	} else {
		int half_width = region->width * spread / 200, half_height = region->height * spread / 200;
		x = region->x + region->width / 2 + (rand() % (half_width * 2 + 1)) - half_width;
		y = region->y + region->height / 2 + (rand() % (half_height * 2 + 1)) - half_height;
	}
	return send_touch(bridge, 2, true, screen_x(map, x), screen_y(map, y));
}

static int update_position(struct bridge *bridge, int dx, int dy)
{
    fprintf(stderr, "M(%d, %d)\n", dx, dy);
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
	const char *map_path = NULL;
	struct touch_map map;
	int mouse_dx = 0, mouse_dy = 0;
	bool mouse_down = false, wasd_left = false, wasd_right = false, wasd_up = false, wasd_down = false;
	int fd_count = 0;

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--shm") && ++i < argc) {
			shm_name = argv[i];
		} else if (!strcmp(argv[i], "--map") && ++i < argc) {
			map_path = argv[i];
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
	if (map_path && !load_map(map_path, &map)) {
		fprintf(stderr, "Unable to load map file: %s\n", map_path);
		return EXIT_FAILURE;
	}
	if (map_path) fprintf(stderr, "Loaded %s: mouse=%d view=%d keys=%zu\n", map_path, map.has_mouse, map.has_view, map.key_count);
	srand((unsigned int)getpid());

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
            // fprintf(stderr, "GotPollEvt:fd:%d\n", fds[i].fd);
			int ret = 0;
			if (!map_path) {
				if (event.type == EV_KEY) ret = handle_key(&bridge, &event);
				if (event.type == EV_REL && bridge.down) {
					if (event.code == REL_X) ret = update_position(&bridge, event.value * (int)bridge.gain, 0);
					if (event.code == REL_Y) ret = update_position(&bridge, 0, event.value * (int)bridge.gain);
				}
			} else if (event.type == EV_REL && map.has_mouse) {
				if (event.code == REL_X) mouse_dx += event.value * (int)bridge.gain;
				if (event.code == REL_Y) mouse_dy += event.value * (int)bridge.gain;
				if (mouse_down) ret = joystick_touch(&bridge, &map, &map.mouse, mouse_dx, mouse_dy, true);
            } else if (event.type == EV_KEY) {
                // fprintf(stderr, "EV_KEY:fd:%d:code:%d, KEY_A:%d\n", fds[i].fd, event.code, KEY_A);
                if (event.code == KEY_ESC && event.value == 1) ret = 1;
                if (event.code == BTN_LEFT && event.value != 2 && map.has_mouse) { mouse_down = event.value != 0; if (mouse_down) mouse_dx = mouse_dy = 0; ret = joystick_touch(&bridge, &map, &map.mouse, mouse_dx, mouse_dy, mouse_down); }
                if (map.has_view && event.value != 2) {
                    if (event.code == KEY_A) wasd_left = event.value != 0;
                    if (event.code == KEY_D) wasd_right = event.value != 0;
                    if (event.code == KEY_W) wasd_up = event.value != 0;
                    if (event.code == KEY_S) wasd_down = event.value != 0;
                    if (event.code == KEY_A || event.code == KEY_D || event.code == KEY_W || event.code == KEY_S) {
                        int distance = map.view.circle ? map.view.radius : (map.view.width < map.view.height ? map.view.width : map.view.height) / 2;
                        ret = joystick_touch(&bridge, &map, &map.view, (wasd_right - wasd_left) * distance, (wasd_down - wasd_up) * distance, wasd_left || wasd_right || wasd_up || wasd_down);
                    }
                }
               // fprintf(stderr, "EV_KEY:fd:%d:code:%d, KEY_A:%d\n", fds[i].fd, event.code, KEY_A);
                for (size_t key = 0; key < map.key_count && event.value != 2; key++) {
                    fprintf(stderr, "event.code == map.keys[key].code:[%d == %d]\n", event.code, map.keys[key].code);
                    if (event.code == map.keys[key].code) {
                        map.keys[key].down = event.value != 0;
                        if (map.keys[key].down) {
                            fprintf(stderr, "random_touch :[%d == %d]\n", event.code, map.keys[key].code);
                            ret = random_touch(&bridge, &map, &map.keys[key].region);
                        } else  {
                            fprintf(stderr, "random_touch :[%d == %d]\n", event.code, map.keys[key].code);
                            ret = send_touch(&bridge, 2, false, 0, 0);
                        }
                    }
                }
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
