#include <zephyr/kernel.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/fs/fs.h>            // 引入 Zephyr 文件系统头文件
#include <zephyr/sys/util.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/sys/reboot.h>

#include <zephyr/shell/shell.h>

#include "wscli.h"


#define CHUNK_SIZE 1024  // 每次读取/写入的块大小（1KB）

/* 获取您在 DTS 中定义的两套分区 ID */
#define SLOT0_PART_ID FIXED_PARTITION_ID(slot0_partition)
#define SLOT1_PART_ID FIXED_PARTITION_ID(slot1_partition)

/* 动态获取当前空闲、应当写入的升级目标槽位 */
uint8_t get_upgrade_target_partition_id(void)
{
    // 获取当前正在运行的 Slot 对应的 Flash Area ID
    // uint8_t active_area_id = boot_fetch_active_slot();

    // if (active_area_id == SLOT0_PART_ID) {
    //     printk("Current running: Slot 0. Targeted for upgrade: Slot 1\n");
    //     return SLOT1_PART_ID; // 目标写到 Slot 1
    // } else {
    //     printk("Current running: Slot 1. Targeted for upgrade: Slot 0\n");
    //     return SLOT0_PART_ID; // 目标写到 Slot 0
    // }
    return SLOT1_PART_ID;
}

/**
 * @brief 从指定文件读取固件并写入到目标 Flash 分区
 * 
 * @param file_path      升级包文件的绝对路径（如 "/SD:/update.bin" 或 "/lfs/update.bin"）
 * @param partition_id   目标分区 ID (如 SLOT0_PART_ID 或 SLOT1_PART_ID)
 * @return 0 成功，负数表示失败
 */
int write_ota_file_to_partition(const char *file_path, uint8_t partition_id)
{
    struct fs_file_t file;
    struct fs_dirent file_stat;
    const struct flash_area *fa = NULL;
    uint8_t *read_buf = NULL;
    int rc;

    fs_file_t_init(&file); // 初始化文件对象

    // 1. 获取文件状态（主要是获取文件实际大小）
    rc = fs_stat(file_path, &file_stat); //
    if (rc < 0) {
        printk("Failed to get file status for %s: %d\n", file_path, rc);
        return rc;
    }

    if (file_stat.type != FS_DIR_ENTRY_FILE || file_stat.size == 0) { //
        printk("Invalid file or empty file: %s (size: %u)\n", file_path, file_stat.size);
        return -EINVAL;
    }

    // 2. 打开目标 Flash 分区
    rc = flash_area_open(partition_id, &fa);
    if (rc < 0) {
        printk("Failed to open flash partition: %d\n", rc);
        return rc;
    }

    // 3. 打开待读取的文件
    rc = fs_open(&file, file_path, FS_O_READ); //
    if (rc < 0) {
        printk("Failed to open file %s: %d\n", file_path, rc);
        flash_area_close(fa);
        return rc;
    }

    // 4. 擦除 Flash
    // 根据获取的文件大小，按 4KB 扇区对齐向上取整，仅擦除文件实际所需的空间（避免完整擦除数兆字节浪费时间）
    size_t erase_size = (file_stat.size + 4095) & ~4095;
    printk("Erasing flash... size: %u bytes\n", erase_size);
    rc = flash_area_erase(fa, 0, erase_size);
    if (rc < 0) {
        printk("Failed to erase partition: %d\n", rc);
        goto cleanup;
    }

    // 5. 分配堆内存缓冲区，用于读取文件并传输到 Flash
    read_buf = k_malloc(CHUNK_SIZE);
    if (!read_buf) {
        printk("Failed to allocate read buffer\n");
        rc = -ENOMEM;
        goto cleanup;
    }

    // 6. 循环读取并写入
    size_t write_offset = 0;
    while (write_offset < file_stat.size) {
        // 计算本次需要读取的大小
        size_t bytes_to_read = MIN(CHUNK_SIZE, file_stat.size - write_offset);
        
        // 读取文件
        ssize_t bytes_read = fs_read(&file, read_buf, bytes_to_read); //
        if (bytes_read < 0) {
            printk("Failed to read file: %d\n", (int)bytes_read);
            rc = bytes_read;
            goto cleanup;
        }

        if (bytes_read == 0) {
            break; // 读取结束
        }

        // 写入 Flash
        rc = flash_area_write(fa, write_offset, read_buf, bytes_read);
        if (rc < 0) {
            printk("Failed to write to flash at offset %d: %d\n", write_offset, rc);
            goto cleanup;
        }

        write_offset += bytes_read;
    }

    printk("OTA file successfully written to flash. Total bytes: %u\n", write_offset);
    rc = 0;

cleanup:
    if (read_buf) {
        k_free(read_buf); // 释放缓冲区
    }
    fs_close(&file);       // 关闭文件句柄
    flash_area_close(fa);  // 关闭分区句柄
    return rc;
}

void trigger_upgrade(void)
{
    // 标记 slot1 中的镜像为“待测试运行”
    // 采用 BOOT_UPGRADE_TEST 可以在新固件启动挂掉时自动回滚，安全系数高
    // int rc = boot_request_upgrade(BOOT_UPGRADE_TEST);
    // if (rc < 0) {
    //     printk("Failed to request boot upgrade: %d\n", rc);
    //     return;
    // }

    printk("Upgrade requested. Rebooting...\n");
    k_msleep(100);

    // 重启设备，ESP32-S3 进入 MCUboot 进行固件交换
    sys_reboot(SYS_REBOOT_WARM);
}

void perform_ota_upgrade_from_file(const char *bin_file_path)
{
    // 1. 动态判断当前闲置的槽位（如果是跑在 Slot 0，就往 Slot 1 写；反之亦然）
    uint8_t target_partition_id = get_upgrade_target_partition_id();

    // 2. 从文件读取并写入到闲置槽位
    int rc = write_ota_file_to_partition(bin_file_path, target_partition_id);
    if (rc == 0) {
        // 3. 写入成功，申请下一次启动进行 Direct-XIP 切换并重启
        trigger_upgrade();
    } else {
        printk("OTA Upgrade Failed.\n");
    }
}

static int cmd_upgrade(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 2) return -EINVAL;

    const char *image_path  = argv[1];

    char full_path[128];
	const char * root_path = fatfs_get_root_path();
	snprintf(full_path, sizeof(full_path), "%s/%s", root_path, image_path);

    perform_ota_upgrade_from_file(full_path);

    return 0;
}

SHELL_CMD_REGISTER(upgrade, NULL, 
	        		"Download file over HTTP\n"
        			"Usage: http_dl <image_path>\n"
					"        http_dl 1.img"
					, cmd_upgrade);