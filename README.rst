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

.. code-block:: text

   touch 0 1 16384 16384
   touch 0 0 16384 16384

低延迟输入通道
--------------

键盘和鼠标事件通过 POSIX 共享内存 ``/zephyr-touch-ipc`` 的 128 项无锁环形队列
传递给 ``native_sim`` 中的高优先级接收线程。该线程以 1 ms 周期批量消费事件，
并在主机回调之外调用 ``touch_hid_send()``，确保多触点与高频松开（UP）事件不丢失。

在宿主机上启动桥接器：

.. code-block:: bash

   ./uart_br.sh --id 046d:c52b

如果鼠标和键盘属于不同的 USB 设备，分别指定 ``--keyboard-id`` 和 ``--mouse-id``。
运行 ``./uart_br.sh --list`` 可查看当前已连接输入设备的 VID:PID。

退出时按 ``Escape``。

FPS 键位映射
------------

用浏览器直接打开 ``tools/keymap_editor.html``，选择横屏游戏截图并设置截图对应的
屏幕宽高。在画布上拖拽创建以下区域，然后点击“导出 map.json”：

* **左侧移动摇杆（WASD / 槽位 0）**：
  按下 W/A/S/D 时，触点从摇杆中心向外滑动至设定边界；支持组合键（如 W+D、S+A 等）
  角度归一化与平滑过渡；松开全部按键后抬起。
* **右侧视角摇杆（鼠标 / 槽位 1）**：
  鼠标相对移动直接驱动视角触点从中心向移动方向偏移，无需按住鼠标左键；受最大偏移限制，
  鼠标停止移动后触点自动释放并复位至中心。
* **按键触点（键盘按键与鼠标按键 / 槽位 2）**：
  支持绑定 Linux 输入键名（如 ``BTN_LEFT`` 鼠标左键射击、``BTN_RIGHT`` 鼠标右键开镜、
  ``KEY_SPACE`` 跳跃、``KEY_LEFTSHIFT`` 冲刺等）；按下时在设定区域内随机落点，松开抬起。
* **摇杆释放快捷键**：
  支持配置全局快捷键（如 ``KEY_TAB``、``KEY_M``）或勾选单个按键的“按下时松开左摇杆/右摇杆”，
  在打开背包、查看地图或换弹时自动复位并释放摇杆触点。

圆形或方形区域创建后可点击选中、拖拽移动，也可以在左侧精确修改坐标与尺寸。摇杆的
死区用于消除中心附近的误触，最大偏移用于限制摇杆输出力度；按键区域的随机范围表示从
中心向四周可随机的百分比，圆形区域的落点始终位于圆内。

将导出的文件传给桥接器：

.. code-block:: bash

   ./uart_br.sh --id 046d:c52b --map /path/to/map.json

构建与运行
----------

Zephyr 构建运行在 Docker 容器中。不要在宿主机直接执行 ``west build``。

.. code-block:: bash

   ./build.sh

构建产物位于 ``/home/langyj/zephyrproject/myprj/build-sim_rc``。
