#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/fs/fs.h>
#include <stdlib.h>
#include <string.h>

// Default test file path, typically FATFS is mounted under "/SD:"
#define DEFAULT_TEST_FILE "/SD:/speed_test.bin"

static int cmd_sd_speed(const struct shell *sh, size_t argc, char **argv)
{
    // Default parameters: 4MB file size, 16KB buffer size
    uint32_t size_mb = 4;
    uint32_t buf_kb = 16;
    const char *file_path = DEFAULT_TEST_FILE;

    // Parse input parameters
    if (argc >= 2) {
        size_mb = strtoul(argv[1], NULL, 10);
    }
    if (argc >= 3) {
        buf_kb = strtoul(argv[2], NULL, 10);
    }
    if (argc >= 4) {
        file_path = argv[3];
    }

    if (size_mb == 0 || buf_kb == 0) {
        shell_error(sh, "Invalid parameters! Usage: sd_speed [size_MB] [buf_KB] [file_path]");
        return -EINVAL;
    }

    uint32_t buf_size = buf_kb * 1024;
    uint32_t total_bytes = size_mb * 1024 * 1024;

    shell_print(sh, "==== SD Card R/W Speed Test Start ====");
    shell_print(sh, "File: %s, Size: %u MB, Buffer: %u KB", file_path, size_mb, buf_kb);

    // 1. Allocate buffer dynamically
    uint8_t *buf = k_malloc(buf_size);
    if (!buf) {
        shell_error(sh, "Error: Insufficient heap memory! Check CONFIG_HEAP_MEM_POOL_SIZE");
        return -ENOMEM;
    }

    // Fill dummy data to avoid compiler optimization and driver compression
    for (size_t i = 0; i < buf_size; i++) {
        buf[i] = (uint8_t)(i & 0xFF);
    }

    struct fs_file_t file;
    fs_file_t_init(&file);

    // Ensure any leftover file is deleted first
    (void)fs_unlink(file_path);

    // --- Write Speed Test ---
    shell_print(sh, "1/2 Writing data...");
    int ret = fs_open(&file, file_path, FS_O_CREATE | FS_O_WRITE);
    if (ret < 0) {
        shell_error(sh, "Failed to open file for writing: %d (Is SD card mounted?)", ret);
        k_free(buf);
        return ret;
    }

    int64_t start_time = k_uptime_get();
    uint32_t bytes_written = 0;

    while (bytes_written < total_bytes) {
        uint32_t chunk = total_bytes - bytes_written;
        if (chunk > buf_size) {
            chunk = buf_size;
        }

        ret = fs_write(&file, buf, chunk);
        if (ret < 0) {
            shell_error(sh, "Write error occurred: %d", ret);
            fs_close(&file);
            k_free(buf);
            return ret;
        }
        bytes_written += ret;
    }

    // Sync file system to ensure all buffers are written to physical media
    ret = fs_sync(&file);
    if (ret < 0) {
        shell_warn(sh, "File sync failed: %d", ret);
    }

    int64_t end_time = k_uptime_get();
    fs_close(&file);

    int64_t write_duration_ms = end_time - start_time;
    if (write_duration_ms <= 0) {
        write_duration_ms = 1; 
    }
    double write_speed = ((double)bytes_written / 1048576.0) / ((double)write_duration_ms / 1000.0);

    shell_print(sh, ">> Write success: %u bytes, Duration: %lld ms, Speed: %.2f MB/s", 
                bytes_written, (long long)write_duration_ms, write_speed);

    // --- Read Speed Test ---
    shell_print(sh, "2/2 Reading data...");
    fs_file_t_init(&file);
    ret = fs_open(&file, file_path, FS_O_READ);
    if (ret < 0) {
        shell_error(sh, "Failed to open file for reading: %d", ret);
        k_free(buf);
        return ret;
    }

    start_time = k_uptime_get();
    uint32_t bytes_read = 0;

    while (bytes_read < total_bytes) {
        uint32_t chunk = total_bytes - bytes_read;
        if (chunk > buf_size) {
            chunk = buf_size;
        }

        ret = fs_read(&file, buf, chunk);
        if (ret < 0) {
            shell_error(sh, "Read error occurred: %d", ret);
            fs_close(&file);
            k_free(buf);
            return ret;
        }
        if (ret == 0) {
            break; // End of file (EOF)
        }
        bytes_read += ret;
    }

    end_time = k_uptime_get();
    fs_close(&file);

    int64_t read_duration_ms = end_time - start_time;
    if (read_duration_ms <= 0) {
        read_duration_ms = 1;
    }
    double read_speed = ((double)bytes_read / 1048576.0) / ((double)read_duration_ms / 1000.0);

    shell_print(sh, ">> Read success: %u bytes, Duration: %lld ms, Speed: %.2f MB/s", 
                bytes_read, (long long)read_duration_ms, read_speed);

    // Clean up temporary files and free allocated memory
    fs_unlink(file_path);
    k_free(buf);

    shell_print(sh, "==== Speed Test Finished ====");
    return 0;
}

// Register Shell command
SHELL_CMD_REGISTER(sd_speed, NULL, "Measure SD card R/W speed.\nUsage: sd_speed [size_MB] [buf_KB] [file_path]", cmd_sd_speed);