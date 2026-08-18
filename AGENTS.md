# Zephyr 项目开发说明


## 构建

Zephyr 开发环境运行在 Docker 容器中。

**禁止直接在宿主机执行 `west build`。**

构建项目时，必须使用：

```bash
./build.sh
```

`build.sh` 会自动进入 Docker 环境，并执行 Zephyr 构建。

构建可能需要几分钟，请等待构建完成。

## 构建流程

每次修改 C/C++、Kconfig、DeviceTree 或其他 Zephyr 相关源代码后：

1. 执行 `./build.sh`
2. 仔细检查完整的构建输出
3. 如果构建失败，分析编译器或链接器错误
4. 根据错误修改源代码
5. 再次执行 `./build.sh`
6. 重复以上过程，直到构建成功

**不要因为第一次构建失败就停止。应当根据编译错误继续分析和修改代码。**

## 重要规则

Zephyr 的工具链、Python 虚拟环境和 `west` 都运行在 Docker 容器中。

不要在宿主机安装或修改 Zephyr 的 Python 依赖。

不要直接在宿主机运行：

```bash
west build
```

不要修改 Docker 配置，除非确认构建问题确实由 Docker 环境导致。

不要为了绕过编译错误而修改 Docker 环境。

## 项目目录

宿主机项目目录：

```text
/home/langyj/w2/zephyr/project/myprj/
```

Docker 容器中的项目目录：

```text
/home/langyj/zephyrproject/myprj/
```

应用程序目录：

```text
sim_rc/
```

宿主机 Zephyr 源码目录：

```text
/home/langyj/w2/zephyr/project/zephyr
```

## 构建命令

实际构建命令为：

```bash
cd /home/langyj/zephyrproject/myprj
west build -b native_sim sim_rc
```

但是该命令必须在 Docker 容器中执行。

在宿主机上统一使用：

```bash
./build.sh
```

## Docker 环境

Docker Compose 文件：

```text
/home/langyj/w2/zephyr/docker/docker-compose.yml
```

Docker 容器名称：

```text
zephyr
```

不要自行创建其他 Zephyr Docker 容器。

## 修改代码后的要求

修改代码后必须主动运行：

```bash
./build.sh
```

不能仅通过静态分析判断代码应该可以编译。

如果构建失败：

* 仔细阅读错误信息
* 找到真正的根本原因
* 修改相关源代码
* 再次运行 `./build.sh`

如果出现多个错误，优先解决最早出现的编译错误，因为后续错误可能是由第一个错误引起的。

## 完成条件

只有在：

```bash
./build.sh
```

成功完成，并且没有编译错误时，才认为本次代码修改完成。

如果构建失败，不要声称修改已经完成。
