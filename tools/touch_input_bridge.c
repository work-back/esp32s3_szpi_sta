/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Linux evdev to Zephyr touch IPC bridge.
 *
 * Build: gcc -O2 -Wall -Wextra -o touch_input_bridge touch_input_bridge.c
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

#define TOUCH_MAX_COORD          32767
#define MAP_MAX_KEYS             32
#define MAP_MAX_RELEASE_KEYS     16

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
	int start_radius;
	int random_percent;
	int dead_zone_percent;
	int max_deflection_percent;
	int enter_min_delay_us;
	int enter_max_delay_us;
	int turn_min_delay_us;
	int turn_max_delay_us;
};

struct mapped_key {
	unsigned int code;
	struct touch_region region;
	bool down;
	bool release_movement;
	bool release_look;
};

struct touch_map {
	int width;
	int height;
	bool has_movement;
	bool has_look;
	struct touch_region movement;
	struct touch_region look;
	struct mapped_key keys[MAP_MAX_KEYS];
	size_t key_count;
	unsigned int release_movement_keys[MAP_MAX_RELEASE_KEYS];
	size_t release_movement_count;
	unsigned int release_look_keys[MAP_MAX_RELEASE_KEYS];
	size_t release_look_count;
};

struct movement_state {
	bool down;
	int cur_x;
	int cur_y;
	bool wasd_w;
	bool wasd_a;
	bool wasd_s;
	bool wasd_d;
};

struct look_state {
	bool down;
	int offset_x;
	int offset_y;
};

static uint32_t isqrt(uint64_t n)
{
	uint64_t root = 0;
	uint64_t bit = 1ULL << 62;

	while (bit > n) {
		bit >>= 2;
	}
	while (bit != 0) {
		if (n >= root + bit) {
			n -= root + bit;
			root = (root >> 1) + bit;
		} else {
			root >>= 1;
		}
		bit >>= 2;
	}
	return (uint32_t)root;
}

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
		"\nMappings (no --map): left mouse button or Space/Enter holds touch; mouse movement\n"
		"and arrow keys move the virtual touch position. Escape exits.\n"
		"Mappings (--map): WASD controls movement joystick (slot 0); mouse relative move\n"
		"controls look joystick (slot 1); mapped keys/buttons control slot 2. Escape exits.\n", program);
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

static bool json_bool(const char *object, const char *name, bool *result)
{
	const char *value = json_value(object, name);
	if (!value) return false;
	if (!strncmp(value, "true", 4)) { *result = true; return true; }
	if (!strncmp(value, "false", 5)) { *result = false; return true; }
	return false;
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

static void json_int_default(const char *object, const char *name, int *result, int fallback)
{
	if (!json_int(object, name, result)) *result = fallback;
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
	    !json_int(object, "y", &region->y)) return false;
	json_int_default(object, "random_percent", &region->random_percent, 60);
	json_int_default(object, "dead_zone_percent", &region->dead_zone_percent, 10);
	json_int_default(object, "max_deflection_percent", &region->max_deflection_percent, 100);
	region->random_percent = region->random_percent < 0 ? 0 : region->random_percent > 100 ? 100 : region->random_percent;
	region->dead_zone_percent = region->dead_zone_percent < 0 ? 0 : region->dead_zone_percent > 90 ? 90 : region->dead_zone_percent;
	region->max_deflection_percent = region->max_deflection_percent < 1 ? 1 : region->max_deflection_percent > 100 ? 100 : region->max_deflection_percent;
	region->circle = !strcmp(shape, "circle");
	return region->circle ? json_int(object, "radius", &region->radius) && region->radius > 0 :
		json_int(object, "width", &region->width) && json_int(object, "height", &region->height) && region->width > 0 && region->height > 0;
}

static bool parse_movement_region(const char *object, struct touch_region *region)
{
	char shape[12];

	if (!json_string(object, "shape", shape, sizeof(shape)) || strcmp(shape, "circle") ||
	    !json_int(object, "x", &region->x) || !json_int(object, "y", &region->y) ||
	    !json_int(object, "radius", &region->radius) || region->radius <= 1 ||
	    !json_int(object, "start_radius", &region->start_radius) ||
	    !json_int(object, "enter_min_delay_us", &region->enter_min_delay_us) ||
	    !json_int(object, "enter_max_delay_us", &region->enter_max_delay_us) ||
	    !json_int(object, "turn_min_delay_us", &region->turn_min_delay_us) ||
	    !json_int(object, "turn_max_delay_us", &region->turn_max_delay_us)) return false;

	region->circle = true;
	return region->start_radius >= 0 && region->start_radius < region->radius &&
		region->enter_min_delay_us >= 0 &&
		region->enter_max_delay_us >= region->enter_min_delay_us &&
		region->turn_min_delay_us >= 0 &&
		region->turn_max_delay_us >= region->turn_min_delay_us;
}

static unsigned int key_code(const char *name)
{
	if (!strcmp(name, "BTN_LEFT")) return BTN_LEFT;
	if (!strcmp(name, "BTN_RIGHT")) return BTN_RIGHT;
	if (!strcmp(name, "BTN_MIDDLE")) return BTN_MIDDLE;
	if (!strcmp(name, "BTN_SIDE")) return BTN_SIDE;
	if (!strcmp(name, "BTN_EXTRA")) return BTN_EXTRA;
	if (!strcmp(name, "KEY_SPACE")) return KEY_SPACE;
	if (!strcmp(name, "KEY_ENTER")) return KEY_ENTER;
	if (!strcmp(name, "KEY_TAB")) return KEY_TAB;
	if (!strcmp(name, "KEY_CAPSLOCK")) return KEY_CAPSLOCK;
	if (!strcmp(name, "KEY_ESC") || !strcmp(name, "KEY_ESCAPE")) return KEY_ESC;
	if (!strcmp(name, "KEY_BACKSPACE")) return KEY_BACKSPACE;
	if (!strcmp(name, "KEY_SHIFT") || !strcmp(name, "KEY_LEFTSHIFT")) return KEY_LEFTSHIFT;
	if (!strcmp(name, "KEY_RIGHTSHIFT")) return KEY_RIGHTSHIFT;
	if (!strcmp(name, "KEY_CTRL") || !strcmp(name, "KEY_LEFTCTRL")) return KEY_LEFTCTRL;
	if (!strcmp(name, "KEY_RIGHTCTRL")) return KEY_RIGHTCTRL;
	if (!strcmp(name, "KEY_ALT") || !strcmp(name, "KEY_LEFTALT")) return KEY_LEFTALT;
	if (!strcmp(name, "KEY_RIGHTALT")) return KEY_RIGHTALT;
	if (!strcmp(name, "KEY_UP")) return KEY_UP;
	if (!strcmp(name, "KEY_DOWN")) return KEY_DOWN;
	if (!strcmp(name, "KEY_LEFT")) return KEY_LEFT;
	if (!strcmp(name, "KEY_RIGHT")) return KEY_RIGHT;
	if (!strncmp(name, "KEY_F", 5) && name[5] >= '1' && name[5] <= '9') {
		if (!name[6]) return KEY_F1 + (name[5] - '1');
		if (name[5] == '1' && name[6] >= '0' && name[6] <= '2' && !name[7]) return KEY_F10 + (name[6] - '0');
	}
	if (!strncmp(name, "KEY_", 4) && name[4] && !name[5] && name[4] >= 'A' && name[4] <= 'Z') return KEY_A + name[4] - 'A';
	if (!strncmp(name, "KEY_", 4) && name[4] >= '0' && name[4] <= '9' && !name[5]) return KEY_0 + name[4] - '0';
	return 0;
}

static void parse_key_array(const char *document, const char *name, unsigned int *keys, size_t *count, size_t max_count)
{
	const char *array = json_value(document, name);
	*count = 0;
	if (!array || *array != '[') return;
	for (const char *p = array + 1; (p = strchr(p, '"')) && *count < max_count;) {
		p++;
		const char *end = strchr(p, '"');
		if (!end) break;
		char key_name[32];
		size_t len = (size_t)(end - p);
		if (len < sizeof(key_name)) {
			memcpy(key_name, p, len);
			key_name[len] = '\0';
			unsigned int code = key_code(key_name);
			if (code) {
				keys[(*count)++] = code;
			}
		}
		p = end + 1;
	}
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
	int version;
	if (!document) return false;
	memset(map, 0, sizeof(*map));
	if (!json_int(document, "version", &version) || version != 2) goto out;
	object = json_named_object(document, "screen", &end);
	if (!object || !json_int(object, "width", &map->width) || !json_int(object, "height", &map->height) || map->width <= 0 || map->height <= 0) goto out;
	if ((object = json_named_object(document, "movement_joystick", &end))) map->has_movement = parse_movement_region(object, &map->movement);
	if ((object = json_named_object(document, "look_joystick", &end))) map->has_look = parse_region(object, &map->look);
	parse_key_array(document, "release_movement_keys", map->release_movement_keys, &map->release_movement_count, MAP_MAX_RELEASE_KEYS);
	parse_key_array(document, "release_look_keys", map->release_look_keys, &map->release_look_count, MAP_MAX_RELEASE_KEYS);
	const char *array = json_value(document, "keys");
	if (array && *array == '[') for (const char *p = array + 1; (p = strchr(p, '{')) && map->key_count < MAP_MAX_KEYS;) {
		const char *next = json_object_end(p); char name[32]; struct mapped_key *key = &map->keys[map->key_count];
		if (!next) goto out;
		if (json_string(p, "key", name, sizeof(name)) && parse_region(p, &key->region) && (key->code = key_code(name))) {
			json_bool(p, "release_movement", &key->release_movement);
			json_bool(p, "release_look", &key->release_look);
			map->key_count++;
		}
		p = next;
	}
	free(document); return map->has_movement || map->has_look || map->key_count;
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

static int send_touch(struct bridge *bridge, uint8_t slot, bool down, int x, int y)
{
	bridge->slot = slot;
	bridge->down = down;
	bridge->x = clamp_coordinate(x);
	bridge->y = clamp_coordinate(y);

	uint32_t head = __atomic_load_n(&bridge->mailbox->head, __ATOMIC_RELAXED);
	uint32_t tail = __atomic_load_n(&bridge->mailbox->tail, __ATOMIC_ACQUIRE);

	if (head - tail >= TOUCH_IPC_QUEUE_SIZE) {
		/* Queue is full: advance tail to make space */
		__atomic_store_n(&bridge->mailbox->tail, tail + 1U, __ATOMIC_RELEASE);
	}

	uint32_t index = head & TOUCH_IPC_QUEUE_MASK;
	bridge->mailbox->queue[index].x = bridge->x;
	bridge->mailbox->queue[index].y = bridge->y;
	bridge->mailbox->queue[index].slot = slot;
	bridge->mailbox->queue[index].down = down ? 1U : 0U;
	bridge->mailbox->queue[index].reserved = 0U;

	__atomic_store_n(&bridge->mailbox->head, head + 1U, __ATOMIC_RELEASE);
	return 0;
}

static int send_frame(struct bridge *bridge)
{
	return send_touch(bridge, bridge->slot, bridge->down, bridge->x, bridge->y);
}

static int screen_x(const struct touch_map *map, int x) { return x * TOUCH_MAX_COORD / map->width; }
static int screen_y(const struct touch_map *map, int y) { return y * TOUCH_MAX_COORD / map->height; }

static void region_center(const struct touch_region *region, int *x, int *y)
{
	*x = region->x;
	*y = region->y;
	if (!region->circle) { *x += region->width / 2; *y += region->height / 2; }
}

static int random_between(int minimum, int maximum)
{
	return minimum + (maximum > minimum ? rand() % (maximum - minimum + 1) : 0);
}

static void random_point_in_circle(int center_x, int center_y, int radius, int *x, int *y)
{
	int dx, dy;

	if (radius <= 0) {
		*x = center_x;
		*y = center_y;
		return;
	}
	do {
		dx = random_between(-radius, radius);
		dy = random_between(-radius, radius);
	} while (dx * dx + dy * dy > radius * radius);
	*x = center_x + dx;
	*y = center_y + dy;
}

static int update_movement(struct bridge *bridge, const struct touch_map *map, struct movement_state *mov)
{
	int dir_x = (mov->wasd_d ? 1 : 0) - (mov->wasd_a ? 1 : 0);
	int dir_y = (mov->wasd_s ? 1 : 0) - (mov->wasd_w ? 1 : 0);
	int cx, cy;
	region_center(&map->movement, &cx, &cy);

	if (dir_x == 0 && dir_y == 0) {
		if (mov->down) {
			int last_x = mov->cur_x ? mov->cur_x : cx;
			int last_y = mov->cur_y ? mov->cur_y : cy;
			mov->down = false;
			mov->cur_x = cx;
			mov->cur_y = cy;
			return send_touch(bridge, 0, false, screen_x(map, last_x), screen_y(map, last_y));
		}
		return 0;
	}

	int length = random_between(map->movement.start_radius + 1, map->movement.radius);
	int target_dx;
	int target_dy;

	if (dir_x != 0 && dir_y != 0) {
		target_dx = (int)((int64_t)dir_x * length * 70711LL / 100000LL);
		target_dy = (int)((int64_t)dir_y * length * 70711LL / 100000LL);
	} else {
		target_dx = dir_x * length;
		target_dy = dir_y * length;
	}

	int target_x = cx + target_dx;
	int target_y = cy + target_dy;

	if (!mov->down) {
		int start_x;
		int start_y;

		random_point_in_circle(cx, cy, map->movement.start_radius, &start_x, &start_y);
		mov->down = true;
		mov->cur_x = start_x;
		mov->cur_y = start_y;
		send_touch(bridge, 0, true, screen_x(map, start_x), screen_y(map, start_y));
		usleep((useconds_t)random_between(map->movement.enter_min_delay_us,
			map->movement.enter_max_delay_us));
		mov->cur_x = target_x;
		mov->cur_y = target_y;
		return send_touch(bridge, 0, true, screen_x(map, target_x), screen_y(map, target_y));
	}

	usleep((useconds_t)random_between(map->movement.turn_min_delay_us,
		map->movement.turn_max_delay_us));
	mov->cur_x = target_x;
	mov->cur_y = target_y;
	return send_touch(bridge, 0, true, screen_x(map, target_x), screen_y(map, target_y));
}

static int release_movement(struct bridge *bridge, const struct touch_map *map, struct movement_state *mov)
{
	if (mov->down) {
		int last_x = mov->cur_x;
		int last_y = mov->cur_y;
		int cx, cy;
		region_center(&map->movement, &cx, &cy);
		mov->wasd_w = mov->wasd_a = mov->wasd_s = mov->wasd_d = false;
		mov->down = false;
		mov->cur_x = cx;
		mov->cur_y = cy;
		return send_touch(bridge, 0, false, screen_x(map, last_x), screen_y(map, last_y));
	}
	mov->wasd_w = mov->wasd_a = mov->wasd_s = mov->wasd_d = false;
	return 0;
}

static int update_look(struct bridge *bridge, const struct touch_map *map,
		       struct look_state *look, int dx, int dy)
{
	int cx, cy;
	region_center(&map->look, &cx, &cy);
	int extent = map->look.circle ? map->look.radius :
		     (map->look.width < map->look.height ? map->look.width : map->look.height) / 2;
	int limit = extent * map->look.max_deflection_percent / 100;
	int dead = extent * map->look.dead_zone_percent / 100;

	look->offset_x += dx;
	look->offset_y += dy;

	uint64_t dist_sq = (int64_t)look->offset_x * look->offset_x + (int64_t)look->offset_y * look->offset_y;
	uint32_t dist = isqrt(dist_sq);
	int cur_dx = look->offset_x;
	int cur_dy = look->offset_y;

	if (dist > (uint32_t)limit && dist > 0) {
		cur_dx = (int)((int64_t)look->offset_x * limit / dist);
		cur_dy = (int)((int64_t)look->offset_y * limit / dist);
	}

	if (!look->down) {
		look->down = true;
		send_touch(bridge, 1, true, screen_x(map, cx), screen_y(map, cy));
	}

	if (dist <= (uint32_t)dead) {
		cur_dx = 0;
		cur_dy = 0;
	}

	return send_touch(bridge, 1, true, screen_x(map, cx + cur_dx), screen_y(map, cy + cur_dy));
}

static int release_look(struct bridge *bridge, const struct touch_map *map, struct look_state *look)
{
	if (look->down) {
		int cx, cy;
		region_center(&map->look, &cx, &cy);
		int last_x = cx + look->offset_x;
		int last_y = cy + look->offset_y;
		look->down = false;
		look->offset_x = 0;
		look->offset_y = 0;
		return send_touch(bridge, 1, false, screen_x(map, last_x), screen_y(map, last_y));
	}
	return 0;
}

static int random_touch(struct bridge *bridge, const struct touch_map *map,
			const struct touch_region *region, int *out_x, int *out_y)
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
	if (out_x) *out_x = x;
	if (out_y) *out_y = y;
	return send_touch(bridge, 2, true, screen_x(map, x), screen_y(map, y));
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
	const char *map_path = NULL;
	struct touch_map map;
	struct movement_state mov = { 0 };
	struct look_state look = { 0 };
	int last_key_x = 0, last_key_y = 0;
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
	if (map_path) {
		fprintf(stderr, "Loaded %s: movement=%d look=%d keys=%zu rel_mov=%zu rel_look=%zu\n",
			map_path, map.has_movement, map.has_look, map.key_count,
			map.release_movement_count, map.release_look_count);
	}
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
		int poll_timeout = (map_path && map.has_look && look.down) ? 25 : -1;
		int poll_res = poll(fds, (nfds_t)fd_count, poll_timeout);
		if (poll_res < 0) {
			if (errno == EINTR) continue;
			perror("poll");
			return EXIT_FAILURE;
		}
		if (poll_res == 0) {
			/* Timeout with no mouse movement: release look touch */
			if (map_path && map.has_look) {
				int ret = release_look(&bridge, &map, &look);
				if (ret < 0) {
					errno = -ret;
					perror("touch IPC write");
					return EXIT_FAILURE;
				}
			}
			continue;
		}

		for (int i = 0; i < fd_count; i++) {
			struct input_event event;
			if (!(fds[i].revents & POLLIN)) continue;
			if (read(fds[i].fd, &event, sizeof(event)) != sizeof(event)) continue;
			int ret = 0;
			if (!map_path) {
				if (event.type == EV_KEY) ret = handle_key(&bridge, &event);
				if (event.type == EV_REL && bridge.down) {
					if (event.code == REL_X) ret = update_position(&bridge, event.value * (int)bridge.gain, 0);
					if (event.code == REL_Y) ret = update_position(&bridge, 0, event.value * (int)bridge.gain);
				}
			} else {
				if (event.type == EV_REL && map.has_look) {
					int dx = (event.code == REL_X) ? event.value * (int)bridge.gain : 0;
					int dy = (event.code == REL_Y) ? event.value * (int)bridge.gain : 0;
					if (dx != 0 || dy != 0) {
						ret = update_look(&bridge, &map, &look, dx, dy);
					}
				} else if (event.type == EV_KEY) {
					if (event.code == KEY_ESC && event.value == 1) return EXIT_SUCCESS;

					/* Check if key press triggers joystick release */
					if (event.value == 1) {
						bool trigger_rel_mov = false;
						bool trigger_rel_look = false;

						for (size_t k = 0; k < map.release_movement_count; k++) {
							if (event.code == map.release_movement_keys[k]) {
								trigger_rel_mov = true;
								break;
							}
						}
						for (size_t k = 0; k < map.release_look_count; k++) {
							if (event.code == map.release_look_keys[k]) {
								trigger_rel_look = true;
								break;
							}
						}
						for (size_t k = 0; k < map.key_count; k++) {
							if (event.code == map.keys[k].code) {
								if (map.keys[k].release_movement) trigger_rel_mov = true;
								if (map.keys[k].release_look) trigger_rel_look = true;
							}
						}

						if (trigger_rel_mov && map.has_movement) {
							ret = release_movement(&bridge, &map, &mov);
						}
						if (trigger_rel_look && map.has_look) {
							ret = release_look(&bridge, &map, &look);
						}
					}

					/* Handle WASD movement keys */
					if (map.has_movement && event.value != 2 &&
					    (event.code == KEY_W || event.code == KEY_A ||
					     event.code == KEY_S || event.code == KEY_D)) {
						if (event.code == KEY_W) mov.wasd_w = (event.value != 0);
						if (event.code == KEY_A) mov.wasd_a = (event.value != 0);
						if (event.code == KEY_S) mov.wasd_s = (event.value != 0);
						if (event.code == KEY_D) mov.wasd_d = (event.value != 0);
						ret = update_movement(&bridge, &map, &mov);
					}

					/* Handle custom mapped keys & mouse buttons (Slot 2) */
					for (size_t key = 0; key < map.key_count && event.value != 2; key++) {
						if (event.code == map.keys[key].code) {
							map.keys[key].down = (event.value != 0);
							if (map.keys[key].down) {
								ret = random_touch(&bridge, &map, &map.keys[key].region, &last_key_x, &last_key_y);
							} else {
								/* Check if any other key is still held */
								bool any_held = false;
								for (size_t k = 0; k < map.key_count; k++) {
									if (map.keys[k].down) {
										ret = random_touch(&bridge, &map, &map.keys[k].region, &last_key_x, &last_key_y);
										any_held = true;
										break;
									}
								}
								if (!any_held) {
									int rel_x = last_key_x ? last_key_x : map.keys[key].region.x;
									int rel_y = last_key_y ? last_key_y : map.keys[key].region.y;
									ret = send_touch(&bridge, 2, false, screen_x(&map, rel_x), screen_y(&map, rel_y));
								}
							}
						}
					}
				}
			}
			if (ret < 0) {
				errno = -ret;
				perror("touch IPC write");
				return EXIT_FAILURE;
			}
		}
	}
}
