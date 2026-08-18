# 项目状态跟进

## 用户目标

实现一个基于 Zephyr `native_sim` 的蓝牙 HID 多点触摸模拟器：将 PC 主机上的键盘按键映射到手机屏幕触点。

```text
PC 按键 <-- [高速进程间通信] --> Zephyr 协议栈 <-- [HCI] --> BT Dongle <-- [BLE HOGP] --> 手机
```

目标为端到端延迟低于 10 ms。

## 当前状态

原始代码为从其他地方复制的 BLE 遥控器/Wi-Fi 项目。当前已将实际编译的应用收敛为独立的 BLE HOGP 多点触摸最小实现；历史文件的既有删除状态保留，未还原或改动。

## 当前实现

- `src/main.c`：启动 BLE HID 服务。
- `src/ble_rc.c`：HID-over-GATT 服务、3 点 Digitizer/Touch Screen 报告描述符、连接/广播处理，以及诊断 shell 命令。
- `src/touch_hid.h`：后续 IPC 接收端应调用的发送接口：`touch_hid_send(slot, down, x, y)`。
- `prj.conf`：仅保留 native_sim 所需的 BLE Peripheral、Shell 与日志配置。
- `build.sh`：在 Docker 容器中以专用 `build-sim_rc` 构建目录执行构建，避免与顶层已有的其他应用构建目录冲突。

报告 ID 为 1，每份报告包括三个独立触点及 Contact Count。坐标采用 HID 绝对坐标范围 `0..32767`。该报告为 19 字节，可在手机尚未协商更大 ATT MTU 时通过默认 23 字节 MTU 发送。手机连接并开启通知后，可通过 shell 验证：

```text
touch 0 1 16384 16384
touch 0 0 16384 16384
```

Shell 路径仅用于验证，不满足 10 ms 延迟目标。

## 下一步计划

1. 确定 PC 到 native_sim 的高速 IPC 协议与进程边界（建议固定大小的二进制帧，包含单调时钟时间戳、slot、状态和坐标）。
2. 在 native_sim 中实现 IPC 接收线程，收到帧后直接调用 `touch_hid_send()`；避免 JSON、动态内存和轮询等待。
3. 连接真实 BLE Dongle 和手机，确认 Android/iOS 对报告描述符的枚举与触摸行为。
4. 在 IPC 接收、GATT 通知提交及手机侧可观测位置埋点，实测并优化端到端延迟至 10 ms 内。

## 已验证

- 2026-08-18：在 Docker 容器中执行 `./build.sh` 成功。
- 构建板：`native_sim`；Zephyr：`4.4.99`；产物目录：`/home/langyj/zephyrproject/myprj/build-sim_rc`。
- 初次构建曾发现顶层 `build/` 属于 `bap_unicast_server`，已通过专用构建目录隔离，未删除该目录。
- 2026-08-18：针对手机连接后立即断开，已启用 BLE SMP，并要求 HID 报告映射、输入报告、CCC 和控制点使用 L2 加密。连接时主动请求配对；断线后延迟 200 ms 重启广播，避免控制器命令缓冲不足（`-12`）。`./build.sh` 已成功验证此修改。
- 2026-08-18：实机日志确认手机在未交换 ATT MTU 时保持默认 23 字节。原 5 点报告需 34 字节 ATT PDU，导致 `No ATT channel for MTU 34` 和 `-12`。报告已收敛为默认 MTU 可发送的 3 点版本，并将主机 ACL 发送上下文增至控制器报告的 8 个。
- 2026-08-18：三点化时发现报告描述符仍错误保留五个手指集合，而 C 结构已缩为三点；手机按 31 字节描述符解析 19 字节报告，导致所有坐标落在 `(0,0)`。已将描述符和发送结构统一为三个手指集合/19 字节报告。
- 2026-08-18：Android `getevent` 确认 HID 已收到坐标 `20000`，但后续解析出两个 `0,0`。根因为每个手指集合结束时 X/Y 字段将 Usage Page 切到 Generic Desktop，后续手指集合未切回 Digitizers。已在每个手指集合开头显式恢复 Digitizers Usage Page，并为所有槽位分配稳定 Contact ID。

首次测试此修复前，必须在手机蓝牙设置中忽略旧的 `Zephyr Multi-Touch` 设备，再重新扫描配对，以清除旧服务缓存。成功配对日志应依次包含 `Host connected`、`Link encrypted (security level 2)` 和 `Touch notifications enabled`。

## 强制构建规则

- 禁止在宿主机直接运行 `west build`。
- C/C++、Kconfig、DeviceTree 等修改后必须在本目录运行 `./build.sh`。
- 必须根据构建输出修复错误并重复构建，只有成功才算完成。
