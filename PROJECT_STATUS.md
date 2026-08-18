# 项目状态跟进

## 用户目标

实现一个基于 Zephyr `native_sim` 的蓝牙 HID 多点触摸模拟器：将 PC 主机上的键盘按键映射到手机屏幕触点。

```text
PC 按键 <-- [高速进程间通信] --> Zephyr 协议栈 <-- [HCI] --> BT Dongle <-- [BLE HOGP] --> 手机
```

目标为端到端延迟低于 10 ms。

### FPS 映射功能
在手机上测试 FPS 游戏操作：
1. **左侧移动摇杆（WASD / 槽位 0）**：
   - 按下按键时，触点从圈定中心向外移动至边界限制位置；
   - 支持组合按键（如 W+A、W+D、S+A、S+D）斜向角度归一化，连续切换按键时保持按下并平滑过渡；
   - 全部松开后触点释放（UP）。
2. **右侧视角摇杆（鼠标 / 槽位 1）**：
   - 鼠标相对移动直接驱动视角触点从中心向移动方向偏移，无需按住鼠标左键；
   - 触点受最大偏移限制，鼠标停止移动后触点自动释放并复位至中心。
3. **按键触点（按键与鼠标按键 / 槽位 2）**：
   - 鼠标左键（`BTN_LEFT`）、右键（`BTN_RIGHT`）及键盘按键可独立绑定到射击/技能等触点区域，按下时在区域内随机落点，松开抬起。
4. **摇杆自动释放映射**：
   - 支持配置全局快捷键（如 `KEY_TAB`、`KEY_M`）或在按键属性中配置 `release_movement`、`release_look`；
   - 当这些按键按下时，自动松开左摇杆（WASD）或右摇杆（视角），便于开地图、打开背包或切镜。
5. **UI 工具（`tools/keymap_editor.html`）**：
   - 支持上传游戏截图，圈定移动摇杆、视角摇杆及各个按键触点；
   - 支持配置死区、最大偏移、随机范围及释放摇杆选项，导出 `map.json`。
6. **桥接程序与 IPC 队列（`tools/touch_input_bridge.c`、`src/touch_ipc_protocol.h`）**：
   - 采用 128 项无锁环形队列传递所有按下、移动、释放帧，彻底消除高频事件下释放帧被覆盖导致触点无法松开的问题。


## 当前状态

原始代码为从其他地方复制的 BLE 遥控器/Wi-Fi 项目。当前已将实际编译的应用收敛为独立的 BLE HOGP 多点触摸最小实现；历史文件的既有删除状态保留，未还原或改动。

## 当前实现

- `src/main.c`：启动 BLE HID 服务。
- `src/ble_rc.c`：HID-over-GATT 服务、3 点 Digitizer/Touch Screen 报告描述符、连接/广播处理，以及诊断 shell 命令。
- `src/touch_hid.h`：后续 IPC 接收端应调用的发送接口：`touch_hid_send(slot, down, x, y)`。
- `prj.conf`：仅保留 native_sim 所需的 BLE Peripheral、Shell 与日志配置。
- `build.sh`：在 Docker 容器中以专用 `build-sim_rc` 构建目录执行构建，避免与顶层已有的其他应用构建目录冲突。
- `src/touch_ipc.c`：native_sim/Linux POSIX 共享内存输入后端；高优先级线程以 1 ms 周期读取事件环形队列并调用 `touch_hid_send()`。
- `src/touch_ipc_protocol.h`：桥接器和 Zephyr 共用的 128 项事件环形队列布局。
- `tools/touch_input_bridge.c`：Linux evdev 键盘/鼠标输入桥，将最新触点状态写入 POSIX 共享内存 `/zephyr-touch-ipc`。
- `uart_br.sh`：按 USB VID:PID 自动发现键盘和鼠标的当前 evdev 节点并启动共享内存桥接器；支持同一接收器或分离设备。

报告 ID 为 1，每份报告包括三个独立触点及 Contact Count。坐标采用 HID 绝对坐标范围 `0..32767`。该报告为 19 字节，可在手机尚未协商更大 ATT MTU 时通过默认 23 字节 MTU 发送。手机连接并开启通知后，可通过 shell 验证：

```text
touch 0 1 16384 16384
touch 0 0 16384 16384
```

Shell 路径仅用于验证，不满足 10 ms 延迟目标。

## 下一步计划

1. 使用 `tools/keymap_editor.html` 在游戏截图上标注鼠标视角摇杆、WASD 移动摇杆和按键触点（如鼠标左键射击），并导出 `map.json`。
2. 运行 `./uart_br.sh` 启动桥接器，加载 `map.json`。
3. 用真实手机验证 FPS 游戏中的移动、视角和射击操作，并记录端到端延迟。

## 已验证

- 2026-08-18：在 Docker 容器中执行 `./build.sh` 成功。
- 2026-08-18：优化左右摇杆与按键映射逻辑：WASD 移动摇杆支持从中心向外滑动至边界限制、组合键角度归一化与平滑过渡；鼠标直接通过相对位移驱动视角摇杆（无需按住鼠标左键）；鼠标左键（`BTN_LEFT`）支持绑定为普通按键（如射击）。
- 2026-08-18：修复高频事件下单帧覆盖导致触点松开事件丢失的问题（改为 128 项无锁事件队列）；增加释放左摇杆（WASD）与右摇杆（视角）的快捷按键配置。
- 宿主机测试与 `./build.sh` 编译均通过。
