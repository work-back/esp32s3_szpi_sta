# 项目状态跟进

## 用户目标

实现一个基于 Zephyr `native_sim` 的蓝牙 HID 多点触摸模拟器：将 PC 主机上的键盘按键映射到手机屏幕触点。

```text
PC 按键 <-- [高速进程间通信] --> Zephyr 协议栈 <-- [HCI] --> BT Dongle <-- [BLE HOGP] --> 手机
```

目标为端到端延迟低于 10 ms。

### 新目标
因为我想在手机上 测试 FPS 功能,
1.  鼠标其方向是通过 触摸一个虚拟摇杆控制的, 将 , x, y 转化为一个方向上的触点.
   视角方向 如 awsd 按键, 也 转化为 一个 方向 触点.
   将键盘按键 映射到 某个触点.
2. 实现一个UI工具,我上传,一个图片,游戏界面截图, 我可以圈定图片位置, 来绑定按键. 圈定哪里来, 实现视角方向控制. 可以圆形,方形.
   在圈定 里面做随机, 以中心,向四周 60%(可配置) 随机.
   保存生成 map.json 文件.
3. touch_input_bridge 加载上面 2 中生成的配置, 实现 上面1, 功能.


## 当前状态

原始代码为从其他地方复制的 BLE 遥控器/Wi-Fi 项目。当前已将实际编译的应用收敛为独立的 BLE HOGP 多点触摸最小实现；历史文件的既有删除状态保留，未还原或改动。

## 当前实现

- `src/main.c`：启动 BLE HID 服务。
- `src/ble_rc.c`：HID-over-GATT 服务、3 点 Digitizer/Touch Screen 报告描述符、连接/广播处理，以及诊断 shell 命令。
- `src/touch_hid.h`：后续 IPC 接收端应调用的发送接口：`touch_hid_send(slot, down, x, y)`。
- `prj.conf`：仅保留 native_sim 所需的 BLE Peripheral、Shell 与日志配置。
- `build.sh`：在 Docker 容器中以专用 `build-sim_rc` 构建目录执行构建，避免与顶层已有的其他应用构建目录冲突。
- `src/touch_ipc.c`：native_sim/Linux POSIX 共享内存输入后端；高优先级线程每 1 ms 检查最新状态邮箱并调用 `touch_hid_send()`。
- `src/touch_ipc_protocol.h`：桥接器和 Zephyr 共用的共享内存邮箱布局；以 seqlock 原子序号保证帧读取一致。
- `tools/touch_input_bridge.c`：Linux evdev 键盘/鼠标输入桥，将最新触点状态写入 POSIX 共享内存 `/zephyr-touch-ipc`。
- `uart_br.sh`：按 USB VID:PID 自动发现键盘和鼠标的当前 evdev 节点并启动共享内存桥接器；支持同一接收器或分离设备。

报告 ID 为 1，每份报告包括三个独立触点及 Contact Count。坐标采用 HID 绝对坐标范围 `0..32767`。该报告为 19 字节，可在手机尚未协商更大 ATT MTU 时通过默认 23 字节 MTU 发送。手机连接并开启通知后，可通过 shell 验证：

```text
touch 0 1 16384 16384
touch 0 0 16384 16384
```

Shell 路径仅用于验证，不满足 10 ms 延迟目标。

## 下一步计划

1. 使用 `tools/keymap_editor.html` 在游戏截图上标注鼠标摇杆、WASD 视角摇杆和按键触点，并导出 `map.json`。
2. 让 `touch_input_bridge` 加载 `map.json`，将鼠标、WASD 与绑定按键转换为三槽 HID 触点。
3. 用真实手机验证 FPS 游戏中的摇杆、视角和按键操作，并记录端到端延迟。

## 已验证

- 2026-08-18：在 Docker 容器中执行 `./build.sh` 成功。
- 构建板：`native_sim`；Zephyr：`4.4.99`；产物目录：`/home/langyj/zephyrproject/myprj/build-sim_rc`。
- 初次构建曾发现顶层 `build/` 属于 `bap_unicast_server`，已通过专用构建目录隔离，未删除该目录。
- 2026-08-18：针对手机连接后立即断开，已启用 BLE SMP，并要求 HID 报告映射、输入报告、CCC 和控制点使用 L2 加密。连接时主动请求配对；断线后延迟 200 ms 重启广播，避免控制器命令缓冲不足（`-12`）。`./build.sh` 已成功验证此修改。
- 2026-08-18：实机日志确认手机在未交换 ATT MTU 时保持默认 23 字节。原 5 点报告需 34 字节 ATT PDU，导致 `No ATT channel for MTU 34` 和 `-12`。报告已收敛为默认 MTU 可发送的 3 点版本，并将主机 ACL 发送上下文增至控制器报告的 8 个。
- 2026-08-18：三点化时发现报告描述符仍错误保留五个手指集合，而 C 结构已缩为三点；手机按 31 字节描述符解析 19 字节报告，导致所有坐标落在 `(0,0)`。已将描述符和发送结构统一为三个手指集合/19 字节报告。
- 2026-08-18：Android `getevent` 确认 HID 已收到坐标 `20000`，但后续解析出两个 `0,0`。根因为每个手指集合结束时 X/Y 字段将 Usage Page 切到 Generic Desktop，后续手指集合未切回 Digitizers。已在每个手指集合开头显式恢复 Digitizers Usage Page，并为所有槽位分配稳定 Contact ID。
- 2026-08-18：已实现 PC 输入通道：Linux evdev 键盘/鼠标桥经 native_sim `uart1` PTY 发送 10 字节固定帧；Zephyr UART ISR 校验后投递给高优先级发送线程。`./build.sh` 成功；桥接程序通过主机 `cc -Wall -Wextra -Werror -fsyntax-only` 检查。尚待真实桌面设备与手机端到端验证。
- 2026-08-18：为消除 native_sim PTY UART 的 10 ms 轮询延迟，输入通道已切换为 POSIX 共享内存 `/zephyr-touch-ipc`。桥接器以 seqlock 原子提交最新帧，Zephyr 高优先级线程以 1 ms 周期读取并调用 `touch_hid_send()`；不再启用 `uart1`。
- 2026-08-18：共享内存后端初版曾因接收线程在邮箱映射前自动启动而使 `zephyr.exe` 段错误；现已改为映射成功后显式创建线程。`./build.sh` 成功，实机启动正常；用户确认输入延迟已显著降低。
- 2026-08-18：已增加 FPS 键位配置工具 `tools/keymap_editor.html` 及 `map.json` 加载。桥接器的 `--map` 模式将鼠标左键/相对移动映射为槽位 0 摇杆，将 WASD 映射为槽位 1 摇杆，将配置按键映射为槽位 2 的区域随机触点。

首次测试此修复前，必须在手机蓝牙设置中忽略旧的 `Zephyr Multi-Touch` 设备，再重新扫描配对，以清除旧服务缓存。成功配对日志应依次包含 `Host connected`、`Link encrypted (security level 2)` 和 `Touch notifications enabled`。

## 强制构建规则

- 禁止在宿主机直接运行 `west build`。
- C/C++、Kconfig、DeviceTree 等修改后必须在本目录运行 `./build.sh`。
- 必须根据构建输出修复错误并重复构建，只有成功才算完成。
