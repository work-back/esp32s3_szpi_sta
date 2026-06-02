import os

# 1. 定义版本号和配置文件路径
build_file = "build_number.txt"
conf_file = "build_version.conf"

# 您可以自定义固件的主版本和次版本号，这里我们固定为 1.0.2
major_minor_patch = "1.0.2"

# 2. 读取当前的编译号
build_num = 1
if os.path.exists(build_file):
    with open(build_file, "r") as f:
        try:
            build_num = int(f.read().strip()) + 1
        except ValueError:
            pass

# 3. 将新的编译号写回本地文件（无 255 限制，支持到 42 亿）
with open(build_file, "w") as f:
    f.write(str(build_num))

# 4. 动态生成一个临时的 Kconfig 配置文件
# 这里的格式为 "major.minor.patchlevel+build_num" (例如 "1.0.2+1024")
# 这是 MCUBoot imgtool 官方标准支持的语义化版本格式
with open(conf_file, "w") as f:
    f.write(f'CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION="{major_minor_patch}+{build_num}"\n')
    # 顺便把这个编译号作为宏传给 C 代码，方便您在程序里 print 打印
    f.write(f'CONFIG_CUSTOM_VERSION="{major_minor_patch}"\n')
    f.write(f'CONFIG_CUSTOM_BUILD_NUMBER="{build_num}"\n')

print(f"--- Auto-incremented build number: {build_num} ---")