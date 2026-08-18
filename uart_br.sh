#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
#
# Start the Linux evdev -> Zephyr shared-memory touch bridge without hard-coding
# /dev/input/eventN. USB IDs are written as lowercase or uppercase VID:PID.

set -euo pipefail

# set -x

ROOT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
BRIDGE_SOURCE="$ROOT_DIR/tools/touch_input_bridge.c"
BRIDGE_BIN="$ROOT_DIR/tools/touch_input_bridge"
SHM_NAME=${TOUCH_SHM_NAME:-/zephyr-touch-ipc}

usage() {
	cat <<EOF
Usage:
  $0 --id VID:PID [bridge options]
  $0 --keyboard-id VID:PID --mouse-id VID:PID [bridge options]
  $0 --list

Examples:
  $0 --id 046d:c52b
  $0 --keyboard-id 04d9:0169 --mouse-id 046d:c077 --gain 24

Options after the device IDs are passed to touch_input_bridge, for example:
  --slot N --gain N --key-step N

The shared-memory mailbox defaults to /zephyr-touch-ipc. Override it with TOUCH_SHM_NAME.
EOF
}

normalize_id() {
	local value=${1,,}
	if [[ ! $value =~ ^[0-9a-f]{4}:[0-9a-f]{4}$ ]]; then
		echo "Invalid USB ID '$1'; expected VID:PID, for example 046d:c52b." >&2
		exit 2
	fi
	printf '%s\n' "$value"
}

event_properties() {
	udevadm info --query=property --name="$1" 2>/dev/null || true
}

property_value() {
	local properties=$1 key=$2
	awk -F= -v key="$key" '$1 == key { print $2; exit }' <<<"$properties"
}

find_event() {
	local usb_id=$1 kind=$2 event properties vendor product
	local expected_vendor=${usb_id%%:*}
	local expected_product=${usb_id##*:}

	for event in /dev/input/event*; do
		[[ -e $event ]] || continue
		properties=$(event_properties "$event")
		vendor=$(property_value "$properties" ID_VENDOR_ID)
		product=$(property_value "$properties" ID_MODEL_ID)
		[[ ${vendor,,} == "$expected_vendor" && ${product,,} == "$expected_product" ]] || continue

		case $kind in
		keyboard)
			[[ $(property_value "$properties" ID_INPUT_KEYBOARD) == 1 ]] || continue
			;;
		mouse)
			[[ $(property_value "$properties" ID_INPUT_MOUSE) == 1 ]] || continue
			;;
		esac
		printf '%s\n' "$event"
		return 0
	done

	return 1
}

list_devices() {
	local event properties vendor product name keyboard mouse
	printf '%-22s %-10s %-7s %-7s %s\n' 'EVENT' 'USB ID' 'KEYBOARD' 'MOUSE' 'NAME'
	for event in /dev/input/event*; do
		[[ -e $event ]] || continue
		properties=$(event_properties "$event")
		vendor=$(property_value "$properties" ID_VENDOR_ID)
		product=$(property_value "$properties" ID_MODEL_ID)
		name=$(property_value "$properties" NAME)
		keyboard=$(property_value "$properties" ID_INPUT_KEYBOARD)
		mouse=$(property_value "$properties" ID_INPUT_MOUSE)
		[[ -n $vendor && -n $product ]] || continue
		printf '%-22s %-10s %-7s %-7s %s\n' "$event" "$vendor:$product" \
			"${keyboard:-0}" "${mouse:-0}" "${name:-unknown}"
	done
}

keyboard_id=
mouse_id=
bridge_args=()

while (($#)); do
	case $1 in
	--id)
		(($# >= 2)) || { usage >&2; exit 2; }
		keyboard_id=$(normalize_id "$2")
		mouse_id=$keyboard_id
		shift 2
		;;
	--keyboard-id)
		(($# >= 2)) || { usage >&2; exit 2; }
		keyboard_id=$(normalize_id "$2")
		shift 2
		;;
	--mouse-id)
		(($# >= 2)) || { usage >&2; exit 2; }
		mouse_id=$(normalize_id "$2")
		shift 2
		;;
	--list)
		list_devices
		exit 0
		;;
	--help|-h)
		usage
		exit 0
		;;
	*)
		bridge_args+=("$1")
		shift
		;;
	esac
done

if [[ -z $keyboard_id || -z $mouse_id ]]; then
	usage >&2
	exit 2
fi

keyboard_event=$(find_event "$keyboard_id" keyboard) || {
	echo "Keyboard with USB ID $keyboard_id was not found. Use $0 --list." >&2
	exit 1
}
mouse_event=$(find_event "$mouse_id" mouse) || {
	echo "Mouse with USB ID $mouse_id was not found. Use $0 --list." >&2
	exit 1
}

if [[ ! -x $BRIDGE_BIN || $BRIDGE_SOURCE -nt $BRIDGE_BIN ]]; then
	echo "[INFO] Building touch_input_bridge"
	cc -std=c17 -O2 -Wall -Wextra -Werror -o "$BRIDGE_BIN" "$BRIDGE_SOURCE"
fi

echo "[INFO] Keyboard: $keyboard_event ($keyboard_id)"
echo "[INFO] Mouse:    $mouse_event ($mouse_id)"
echo "[INFO] Shared memory: $SHM_NAME"

sudo setcap cap_dac_read_search+ep $BRIDGE_BIN

exec "$BRIDGE_BIN" --shm "$SHM_NAME" --keyboard "$keyboard_event" --mouse "$mouse_event" "${bridge_args[@]}"
