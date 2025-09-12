/*
 * matrix.h
 * 
 * 多執行緒矩陣運算的標頭檔
 * 定義了矩陣資料結構和相關函式的宣告
 * 此程式用於展示多執行緒（multithreading）環境下的同步（synchronization）機制
 */

#ifndef MATRIX_H
#define MATRIX_H

#include <pthread.h>    // 提供 pthread API 支援多執行緒程式設計
#include <stdio.h>      // 標準輸入輸出函式庫
#include <stdlib.h>     // 標準函式庫，包含記憶體配置函式

#define BUF_SIZE 1000   // 緩衝區大小常數，用於 pipe communication

/*
 * Matrix 結構體
 * 表示一個二維矩陣的資料結構
 */
typedef struct Matrix {
    int row;        // 矩陣的列（row）數量
    int col;        // 矩陣的行（column）數量
    int** data;     // 二維陣列指標，儲存矩陣元素資料
} Matrix;

/*
 * Task 結構體
 * 用於定義每個 thread 負責處理的工作範圍
 * 實現 task decomposition（任務分解）和 load balancing（負載平衡）
 */
typedef struct Task {
    int start_row;  // 該 thread 負責運算的起始列索引
    int end_row;    // 該 thread 負責運算的結束列索引（不包含此列）
} Task;

// 函式宣告

/*
 * 從檔案載入矩陣資料
 * @param filename: 矩陣檔案的檔名
 * @return: 指向 Matrix 結構體的指標，失敗時回傳 NULL
 */
Matrix* load_matric(char* filename);

/*
 * 將矩陣運算結果寫入輸出檔案
 * 執行 I/O operation 將計算完成的矩陣資料輸出
 */
void write_result(void);

/*
 * 主要的矩陣相乘函式
 * 負責建立多個 threads 並分配工作給各個 thread
 * 實現 parallel computing（平行運算）
 */
void multiply(void);

/*
 * Thread 執行函式（thread routine）
 * 每個 thread 執行此函式來處理分配給它的矩陣運算工作
 * 使用 fork() 建立 child process 進行實際運算
 * 透過 pipe 進行 inter-process communication (IPC)
 * @param param: 指向 Task 結構體的指標，包含該 thread 的工作範圍
 * @return: void* 類型的回傳值（符合 pthread 函式簽名要求）
 */
void* thread(void* param);

#endif