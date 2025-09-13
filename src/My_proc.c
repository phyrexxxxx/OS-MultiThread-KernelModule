/*
 * My_proc.c
 *
 * Linux Kernel Module - /proc filesystem 介面實作
 *
 * 此模組建立 /proc/thread_info 虛擬檔案，提供 kernel space 和 user space
 * 之間的通訊管道。展示以下重要的 OS 核心概念：
 *
 * 1. Kernel Module Programming
 * 2. /proc filesystem 實作
 * 3. System call interface (read/write operations)
 * 4. Process management 和 task_struct 存取
 * 5. Kernel-userspace data transfer
 * 6. Process scheduling statistics
 *
 * 功能：接收 user space 傳來的 PID，查詢該 process 的執行統計資訊，
 * 包括執行時間和 context switch 次數
 */

#include <linux/init_task.h>  // 提供 init_task（所有 process 的根節點）
#include <linux/kernel.h>     // Linux Kernel 基本函式和巨集
#include <linux/module.h>     // kernel module 相關函式和巨集
#include <linux/proc_fs.h>    // /proc filesystem 支援
#include <linux/uaccess.h>    // user space 和 kernel space 資料傳輸
#include <linux/version.h>    // Linux 核心版本資訊

#include "../include/proc_module.h"  // 自定義的常數定義

/*
 * Linux 核心版本相容性處理
 * Linux 核心版本 5.6.0 之後，proc_ops 取代了 file_operations 結構
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0)
#define HAVE_PROC_OPS
#endif

// kernel module 內部 global variables
static struct proc_dir_entry *our_proc_file;  // /proc 檔案項目指標
static char procfs_buffer[PROCFS_MAX_SIZE];   // 與 user space 通訊的緩衝區
static ssize_t procfs_buffer_size = 0;        // 緩衝區中有效資料的大小

/*
 * procfile_read() - 處理對 /proc/thread_info 的讀取操作
 *
 * 當 user space application 讀取 /proc/thread_info 時，kernel 會呼叫此函式
 * 展示 kernel-to-userspace data transfer 和 /proc filesystem 的讀取機制
 *
 * @param filePointer: 檔案指標結構，包含檔案相關資訊
 * @param buffer: user space 提供的緩衝區（user space 位址）
 * @param buffer_length: 請求讀取的資料長度
 * @param offset: 檔案偏移量指標
 * @return: 實際讀取的 byte 數，0 表示 EOF
 */
static ssize_t procfile_read(struct file *filePointer, char __user *buffer, size_t buffer_length, loff_t *offset)
{
    ssize_t len = procfs_buffer_size;  // 可用資料長度
    ssize_t ret = len;

    /*
     * 檢查是否到達檔案結尾或資料傳輸失敗
     * copy_to_user() 用於安全地從 kernel space 複製資料到 user space
     * 這是必要的安全檢查，因為直接存取 user space memory 可能導致安全問題
     */
    if (*offset >= len || copy_to_user(buffer, procfs_buffer, len)) {
        ret = 0;  // EOF 或傳輸失敗
    } else {
        // 輸出偵錯資訊到 kernel log
        pr_info("procfile read %s\n", filePointer->f_path.dentry->d_name.name);
        *offset += len;  // 更新檔案偏移量
    }

    return ret;
}

/*
 * procfile_write() - 處理對 /proc/thread_info 的寫入操作
 *
 * 當 user space application 寫入資料到 /proc/thread_info 時，kernel 會呼叫此函式
 * 這是此模組的核心功能，展示以下重要概念：
 * 1. Userspace-to-kernel data transfer
 * 2. Process table traversal
 * 3. Task scheduling statistics extraction
 * 4. Process Control Block (PCB) 資訊存取
 *
 * 處理流程：
 * 1. 從 user space 接收 PID
 * 2. 在 process table 中查找對應的 task_struct
 * 3. 提取該 process 的執行統計資訊
 * 4. 格式化回應資料供後續讀取
 *
 * @param file: 檔案結構指標
 * @param buff: user space 傳入的資料緩衝區
 * @param len: 寫入資料的長度
 * @param off: 檔案偏移量指標
 * @return: 實際處理的 byte 數，負數表示錯誤
 */
static ssize_t procfile_write(struct file *file, const char __user *buff, size_t len, loff_t *off)
{
    struct task_struct *task, *p;  // task_struct 指標，用於 process 資訊存取
    struct list_head *pos;         // 鏈結串列遍歷指標
    pid_t pid;                     // 從 user space 接收的 process ID

    // 設定緩衝區大小，確保不超過最大限制
    procfs_buffer_size = len;
    if (procfs_buffer_size > PROCFS_MAX_SIZE)
        procfs_buffer_size = PROCFS_MAX_SIZE;

    /*
     * 從 user space 安全地複製資料到 kernel space
     * copy_from_user() 執行必要的權限檢查和記憶體存取驗證
     */
    if (copy_from_user(procfs_buffer, buff, procfs_buffer_size))
        return -EFAULT;  // 記憶體存取錯誤

    // 確保字串以 null 結尾，防止 buffer overflow
    procfs_buffer[procfs_buffer_size & (PROCFS_MAX_SIZE - 1)] = '\0';
    *off += procfs_buffer_size;  // 更新檔案偏移量

    // 輸出偵錯資訊到 kernel log
    pr_info("procfile write %s\n", procfs_buffer);

    // 將接收到的字串轉換為整數 PID
    if (kstrtoint(procfs_buffer, 10, &pid) != 0) {
        pr_info("invalid pid: %s\n", procfs_buffer);
        return 0;
    }

    /*
     * 遍歷系統中的所有 process 來查找指定的 PID
     * init_task 是所有 process 的根節點（PID 0，系統初始化 process）
     * task->tasks 是連接所有 task_struct 的雙向鏈結串列
     */
    task = &init_task;
    list_for_each (pos, &task->tasks) {
        // 從鏈結串列節點取得 task_struct 指標
        p = list_entry(pos, struct task_struct, tasks);

        if (p->pid == pid) {
            /*
             * 找到目標 process，提取統計資訊：
             * - p->pid: Process ID
             * - p->utime: User mode 執行時間（nanoseconds）
             * - p->nvcsw: Voluntary context switches（主動 context switch）
             * - p->nivcsw: Involuntary context switches（被動 context switch）
             */
            sprintf(procfs_buffer, "ThreadID:%d Time:%lld(ms) context switch times:%ld\n", p->pid,
                    p->utime / 1000 / 1000,  // 轉換為毫秒
                    p->nvcsw + p->nivcsw);   // 總 context switch 次數
            procfs_buffer_size = strlen(procfs_buffer);
            break;
        }
    }

    return procfs_buffer_size;
}

/*
 * 檔案操作結構體定義
 * 將自定義的讀寫函式與 /proc filesystem 連接
 *
 * Linux 核心版本相容性處理：
 * - 5.6.0 之後使用 proc_ops 結構
 * - 之前版本使用 file_operations 結構
 */
#ifdef HAVE_PROC_OPS
static const struct proc_ops proc_file_fops = {
    .proc_read = procfile_read,    // 註冊讀取操作處理函式
    .proc_write = procfile_write,  // 註冊寫入操作處理函式
};
#else
static const struct file_operations proc_file_fops = {
    .read = procfile_read,    // 註冊讀取操作處理函式
    .write = procfile_write,  // 註冊寫入操作處理函式
};
#endif

/*
 * procfs_init() - Kernel Module 初始化函式
 *
 * 當模組載入時（使用 insmod 命令），kernel 會呼叫此函式
 * 負責建立 /proc/thread_info 檔案並註冊相關操作
 *
 * __init 標記表示此函式只在初始化時使用，
 * 初始化完成後其程式碼可被釋放以節省記憶體
 *
 * @return: 0 表示成功，負數表示錯誤碼
 */
static int __init procfs_init(void)
{
    /*
     * 在 /proc filesystem 中建立檔案
     * 參數說明：
     * - PROCFS_NAME: 檔案名稱 "thread_info"
     * - 0666: 檔案權限（owner/group/other 都有讀寫權限）
     * - NULL: 父目錄（NULL 表示直接在 /proc 下）
     * - &proc_file_fops: 檔案操作函式結構指標
     */
    our_proc_file = proc_create(PROCFS_NAME, 0666, NULL, &proc_file_fops);
    if (our_proc_file == NULL) {
        proc_remove(our_proc_file);  // 清理資源
        pr_alert("Error:Could not initialize /proc/%s\n", PROCFS_NAME);
        return -ENOMEM;  // 記憶體不足錯誤
    }

    pr_info("/proc/%s created\n", PROCFS_NAME);  // 記錄成功訊息到 kernel log
    return 0;                                    // 初始化成功
}

/*
 * procfs_exit() - Kernel Module 清理函式
 *
 * 當模組卸載時（使用 rmmod 命令），kernel 會呼叫此函式
 * 負責清理資源，移除 /proc/thread_info 檔案
 *
 * __exit 標記表示此函式只在模組卸載時使用
 */
static void __exit procfs_exit(void)
{
    proc_remove(our_proc_file);                  // 從 /proc filesystem 移除檔案
    pr_info("/proc/%s removed\n", PROCFS_NAME);  // 記錄清理訊息到 kernel log
}

/*
 * Kernel Module 註冊 macro
 * 告知 kernel 哪些函式負責模組的初始化和清理工作
 */
module_init(procfs_init);  // 註冊初始化函式
module_exit(procfs_exit);  // 註冊清理函式

/*
 * 模組授權宣告
 * GPL 授權允許此模組與 GPL 授權的核心一起使用
 * 這是 Linux Kernel Module 的必要宣告
 */
MODULE_LICENSE("GPL");