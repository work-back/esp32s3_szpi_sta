/* SPDX-License-Identifier: Apache-2.0 */

#ifndef TOUCH_HID_H_
#define TOUCH_HID_H_

#include <stdbool.h>
#include <stdint.h>

int touch_hid_init(void);
int touch_hid_send(uint8_t slot, bool down, uint16_t x, uint16_t y);

#endif /* TOUCH_HID_H_ */
