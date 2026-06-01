#include <zephyr/kernel.h>
#include <zephyr/fs/fs.h>
#include <psa/crypto.h> // 包含通用的 PSA Crypto 规范头文件
#include <stdio.h>

#define READ_BUFFER_SIZE 1024

/**
 * @brief 使用 PSA API 计算文件系统中指定文件的 SHA-256 校验和
 * 
 * @param filepath 文件路径 (例如 "/lfs/download.bin")
 * @param output_sha256 存储计算结果的 32 字节数组
 * @return int 0 表示成功，负数表示失败
 */
int calculate_file_sha256(const char *filepath, unsigned char output_sha256[32])
{
    struct fs_file_t file;
    psa_status_t status;
    int rc;

    // 1. 确保 PSA 密码学子系统已经初始化
    //（在应用生命周期内只需初始化一次，多次调用不会有副作用）
    status = psa_crypto_init();
    if (status != PSA_SUCCESS) {
        printk("Failed to initialize PSA crypto: %d\n", status);
        return -EIO;
    }

    fs_file_t_init(&file);

    // 2. 打开文件
    rc = fs_open(&file, filepath, FS_O_READ);
    if (rc < 0) {
        printk("Failed to open file %s: %d\n", filepath, rc);
        return rc;
    }

    // 3. 初始化并配置多段哈希操作
    psa_hash_operation_t operation = PSA_HASH_OPERATION_INIT;

    status = psa_hash_setup(&operation, PSA_ALG_SHA_256);
    if (status != PSA_SUCCESS) {
        printk("psa_hash_setup failed: %d\n", status);
        fs_close(&file);
        return -EIO;
    }

    // 4. 循环分块读取文件并更新哈希上下文
    //（此处的底层同样会自动路由至 ESP32S3 的硬件 SHA 加速引擎）
    static uint8_t buffer[READ_BUFFER_SIZE];
    ssize_t bytes_read;

    while ((bytes_read = fs_read(&file, buffer, sizeof(buffer))) > 0) {
        status = psa_hash_update(&operation, buffer, bytes_read);
        if (status != PSA_SUCCESS) {
            printk("psa_hash_update failed: %d\n", status);
            psa_hash_abort(&operation); // 出错时及时中止操作释放资源
            fs_close(&file);
            return -EIO;
        }
    }

    if (bytes_read < 0) {
        printk("Error reading file: %zd\n", bytes_read);
        psa_hash_abort(&operation);
        fs_close(&file);
        return bytes_read;
    }

    // 5. 完成哈希计算，并输出最终的 32 字节散列值
    size_t output_len;
    status = psa_hash_finish(&operation, output_sha256, 32, &output_len);
    if (status != PSA_SUCCESS) {
        printk("psa_hash_finish failed: %d\n", status);
        psa_hash_abort(&operation);
        fs_close(&file);
        return -EIO;
    }

    // 6. 关闭文件并完成
    fs_close(&file);
    return 0;
}

/**
 * @brief 将 32 字节 SHA-256 格式化输出为 64 位十六进制字符串
 */
void print_sha256_sum(const unsigned char sha256[32], const char *filename)
{
    char sha256_str[65];
    for (int i = 0; i < 32; i++) {
        sprintf(&sha256_str[i * 2], "%02x", sha256[i]);
    }
    printk("%s  %s\n", sha256_str, filename);
}