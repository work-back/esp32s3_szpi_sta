BLE 多点触摸模拟器
==================

本应用是低延迟蓝牙 HID-over-GATT 多点触摸模拟器的 Zephyr 端实现。开发阶段
使用 ``native_sim``；native simulator 运行时配置会将其蓝牙 HCI 流量连接到目标
BLE Dongle。

当前实现
--------

应用提供符合标准的 HID Digitizers/Touch Screen 服务和报告描述符。报告 ID 为 1，
包含三个独立手指集合；每个触点包括 Tip Switch、In Range、Contact Identifier，
以及范围为 0..32767 的绝对 X/Y 坐标，报告末尾为 Contact Count。

手机连接并启用输入报告通知后，可通过 Zephyr shell 进行初步验证：

.. code-block:: console

   touch <slot> <down> <x> <y>

例如，在屏幕中心按下并松开：

.. code-block:: console

   touch 0 1 16384 16384
   touch 0 0 16384 16384

``slot`` 取值为 0..2，``down`` 取值为 0 或 1。Shell 仅用于诊断验证，不满足
低延迟目标。

构建
----

只能使用项目构建包装脚本；它会在 Docker 容器内运行 Zephyr：

.. code-block:: console

   ./build.sh

该脚本使用专用的 ``build-sim_rc`` 构建目录，不会影响其他应用可能使用的顶层
``build`` 目录。

PC 键盘和鼠标桥接器
--------------------

在 ``native_sim`` 上，输入通过 POSIX 共享内存邮箱 ``/zephyr-touch-ipc`` 传输。
桥接器只写入最新的触点状态；Zephyr 的高优先级线程每 1 ms 检查其原子序号。该方式
避免了 native PTY-UART 驱动约 10 ms 的接收轮询延迟。它是 Linux 模拟阶段使用的
后端，并非物理 UART 协议。

使用主机编译器构建 Linux 输入桥接器：

.. code-block:: console

   cc -O2 -Wall -Wextra -o tools/touch_input_bridge tools/touch_input_bridge.c

在完成 ``run_hci.sh`` 中的蓝牙准备操作后，正常启动模拟器：

.. code-block:: console

   /home/langyj/w2/zephyr/project/myprj/build-sim_rc/zephyr/zephyr.exe \
       --bt-dev=hci0

建议通过稳定名称定位输入事件设备：

.. code-block:: console

   ls -l /dev/input/by-id/

然后启动桥接器。读取 ``/dev/input/event*`` 通常需要 ``sudo`` 或等效的输入设备
访问权限规则：

.. code-block:: console

   ./uart_br.sh --id 046d:c52b

``uart_br.sh --list`` 会列出已识别输入事件设备的 USB 厂商/产品 ID。键盘和鼠标
来自同一 USB 接收器时使用 ``--id VID:PID``；来自不同设备时，分别使用
``--keyboard-id`` 和 ``--mouse-id``。脚本通过 ``udevadm`` 查找当前的事件节点，
仅在读取节点时调用 ``sudo``。

鼠标移动会更新虚拟触点位置；按住鼠标左键并移动可执行拖动。Space 或 Enter 可按下/
松开触点，方向键每次移动 1024 个 HID 坐标单位，Escape 退出。``--gain`` 设置鼠标
增量对应的 HID 坐标单位（默认 32），``--key-step`` 设置方向键的移动步长。

FPS 键位映射
------------

用浏览器直接打开 ``tools/keymap_editor.html``，选择横屏游戏截图并设置截图对应的
屏幕宽高。在画布上拖拽创建以下区域，然后点击“导出 map.json”：

* 鼠标移动摇杆：槽位 0。按住鼠标左键时，鼠标相对移动会让该触点从摇杆中心向相应方向移动。
* WASD 视角摇杆：槽位 1。按下 W/A/S/D 时生成相应方向的触点；松开全部按键后抬起。
* 按键触点：槽位 2。填写如 ``KEY_SPACE``、``KEY_F``、``KEY_SHIFT`` 的 Linux 输入键名；按下时在设定区域内随机落点，松开时抬起。

圆形或方形区域创建后可点击选中、拖拽移动，也可以在左侧精确修改坐标与尺寸。随机范围
表示从区域中心向四周可随机的百分比；圆形区域的落点始终位于圆内。

将导出的文件传给桥接器：

.. code-block:: console

   ./uart_br.sh --id 046d:c52b --map /path/to/map.json

``--map`` 模式下会启用上述 FPS 映射，替代原有的鼠标拖动/方向键诊断映射。当前 HID
报告有三个触点槽，因此多个“按键触点”在同一时刻共用槽位 2。

邮箱使用 seqlock 保护最新状态帧，因此鼠标移动会合并，不会让过期坐标排队。Zephyr
线程在主机回调之外调用 ``touch_hid_send()``。在声称端到端延迟达到 10 ms 目标前，
仍需以真实手机进行带时间戳的测量。
