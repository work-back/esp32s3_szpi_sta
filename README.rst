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

PC keyboard and mouse bridge
----------------------------

For ``native_sim``, input uses the POSIX shared-memory mailbox
``/zephyr-touch-ipc``. The bridge writes only the latest touch state, while a
high-priority Zephyr thread checks its atomic sequence number every 1 ms. This
avoids the native PTY-UART driver's 10 ms receive polling delay. It is a Linux
simulation backend, not a physical-UART protocol.

Build the Linux input bridge with the host compiler::

   cc -O2 -Wall -Wextra -o tools/touch_input_bridge tools/touch_input_bridge.c

Start the simulator normally (after the Bluetooth setup commands in
``run_hci.sh``)::

   /home/langyj/w2/zephyr/project/myprj/build-sim_rc/zephyr/zephyr.exe \
       --bt-dev=hci0

Locate the event devices, preferably through stable names::

   ls -l /dev/input/by-id/

Then launch the bridge. Access to ``/dev/input/event*`` normally requires
``sudo`` or an equivalent input-device permission rule::

   ./uart_br.sh --id 046d:c52b

``uart_br.sh --list`` lists USB vendor/product IDs for recognised input event
devices. Use ``--id VID:PID`` when keyboard and mouse are exposed by the same
USB receiver, or use separate ``--keyboard-id`` and ``--mouse-id`` options.
The script discovers the current event nodes through ``udevadm`` and invokes
``sudo`` only for reading those nodes.

Mouse movement updates a virtual position; hold the left mouse button and move
to drag the primary touch. Space or Enter also holds/releases the touch, while
arrow keys move the virtual position by 1024 HID units. Escape stops the
bridge. ``--gain`` sets HID units per mouse delta (default 32), and
``--key-step`` sets the arrow-key movement.

The mailbox is a seqlock-protected latest-state frame, so mouse movement is
coalesced instead of queuing stale coordinates. The Zephyr thread calls
``touch_hid_send()`` outside any host callback. End-to-end latency still needs
timestamped measurement on the target phone before claiming the 10 ms goal.
