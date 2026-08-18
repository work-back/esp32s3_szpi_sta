BLE multi-touch simulator
=========================

This application is the Zephyr side of a low-latency Bluetooth HID-over-GATT
multi-touch simulator. It targets ``native_sim`` for development and expects
its Bluetooth HCI traffic to be connected to the target BLE dongle by the
native simulator runtime configuration.

Current implementation
----------------------

The application exposes a standard HID service with a Digitizers/Touch Screen
report descriptor. Report ID 1 has three independent finger collections. Each
contact contains Tip Switch, In Range, Contact Identifier, and absolute X/Y
coordinates in the HID range 0..32767; the report ends with Contact Count.

For initial verification, use the Zephyr shell after a phone has connected and
enabled input-report notifications::

   touch <slot> <down> <x> <y>

For example, a press and release at the centre of the display are::

   touch 0 1 16384 16384
   touch 0 0 16384 16384

``slot`` is 0..2 and ``down`` is 0 or 1. The shell is only a diagnostic input
path; it is not intended to meet the latency target.

Building
--------

Use only the project build wrapper, which runs Zephyr in the Docker container::

   ./build.sh

The wrapper uses the dedicated ``build-sim_rc`` directory, so it does not
disturb another application that may be using the shared top-level ``build``
directory.

Next step
---------

Replace the shell command input with the chosen host-to-simulator IPC transport
and call ``touch_hid_send(slot, down, x, y)`` directly from its receive path.
The transport needs a timestamped latency measurement before claiming the
end-to-end 10 ms target.
