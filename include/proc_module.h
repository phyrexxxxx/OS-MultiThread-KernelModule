/*
 * proc_module.h
 *
 * Linux kernel module 的標頭檔
 * 定義與 proc filesystem 相關的常數和設定
 * 用於建立 kernel space 和 user space 之間的通訊介面
 */

#ifndef PROC_MODULE_H
#define PROC_MODULE_H

/*
 * PROCFS_MAX_SIZE
 * 定義 proc filesystem 緩衝區的最大大小（單位：bytes）
 * 限制透過 /proc 介面傳遞的資料量，防止 buffer overflow
 * 在 kernel programming 中，固定緩衝區大小是重要的安全考量
 */
#define PROCFS_MAX_SIZE 1024

/*
 * PROCFS_NAME
 * 定義在 /proc 目錄下建立的虛擬檔案名稱
 * 完整路徑為 /proc/thread_info
 * 此檔案作為 kernel module 與 user space application 的溝通管道
 * 支援 read 和 write 操作來交換 thread 資訊
 */
#define PROCFS_NAME "thread_info"

#endif