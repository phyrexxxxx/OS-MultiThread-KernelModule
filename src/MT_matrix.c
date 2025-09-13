/*
 * MT_matrix.c
 *
 * Multi-threaded Matrix Multiplication
 * 展示 POSIX threads (pthread)、inter-process communication (IPC)
 * 以及 Linux kernel module 互動的綜合應用
 *
 * 主要 OS 概念：
 * - Multithreading
 * - Process creation with fork()
 * - Inter-process communication via pipes
 * - Synchronization with mutex
 * - Kernel-userspace communication via /proc filesystem
 */

#include <fcntl.h>    // 檔案控制，提供 open()、O_RDWR 等
#include <pthread.h>  // POSIX threads API
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>   // 檔案狀態資訊
#include <sys/types.h>  // 系統資料型別定義
#include <sys/wait.h>   // process waiting 函式
#include <time.h>
#include <unistd.h>  // UNIX 標準函式，提供 fork(), pipe() 等

#include "../include/matrix.h"  // 自定義的矩陣相關資料結構和函式

// Global Variables
static int n_thread;                           // 指定的 thread 數量
static Matrix *m1, *m2, *m;                    // 矩陣指標：m1, m2 為輸入矩陣，m 為結果矩陣
static char *proc_file = "/proc/thread_info";  // kernel module 提供的 proc 介面路徑
static char *out_file = "result.txt";          // 輸出檔案名稱
static pthread_mutex_t mutex;                  // mutex 用於 thread synchronization

// 時間測量變數，用於效能分析
struct timespec startTime, endTime;  // 高精度時間結構
int totalTime;                       // 總執行時間（秒）

/*
 * load_matric() - 從檔案載入矩陣資料
 *
 * 此函式執行檔案 I/O 操作，從文字檔案讀取矩陣資料並建立 Matrix 結構
 * 展示 dynamic memory allocation 的使用
 *
 * @param filename: 包含矩陣資料的檔案名稱
 * @return: 成功時回傳 Matrix 結構指標，失敗時回傳 NULL
 *
 * 檔案格式：
 * 第一行：[row 數量] [column 數量]
 * 後續行：矩陣元素（以空格分隔）
 */
Matrix *load_matric(char *filename)
{
    FILE *fp = fopen(filename, "r");  // 以讀取模式開啟檔案
    if (fp == NULL) {
        printf("File not found: %s\n", filename);
        return NULL;
    }

    // 動態配置 Matrix 結構的記憶體空間
    Matrix *m = malloc(sizeof(Matrix));
    if (m == NULL) {
        printf("Malloc failed.\n");
        return NULL;
    }
    int r, c;  // 迴圈索引變數

    // 讀取矩陣維度資訊
    fscanf(fp, "%d", &(m->row));  // 讀取列數
    fscanf(fp, "%d", &(m->col));  // 讀取行數

    // 為二維陣列配置記憶體
    m->data = malloc(sizeof(int *) * m->row);  // 配置指標陣列
    for (r = 0; r < m->row; r++) {
        m->data[r] = malloc(sizeof(int) * m->col);  // 為每一列配置記憶體
        if (m->data[r] == NULL) {
            printf("Malloc failed.\n");
            return NULL;
        }
    }

    // 讀取矩陣元素資料
    for (r = 0; r < m->row; r++) {
        for (c = 0; c < m->col; c++) {
            fscanf(fp, "%d", &(m->data[r][c]));
        }
    }
    fclose(fp);  // 關閉檔案，釋放 file descriptor
    return m;
}

/*
 * thread() - Thread 執行函式
 *
 * 這是每個 pthread 執行的主要函式，展示多個重要的 OS 概念：
 * 1. Process creation (fork())
 * 2. IPC via pipes
 * 3. Thread synchronization via mutex
 * 4. Kernel-userspace communication via /proc filesystem
 *
 * 設計模式：使用 fork() 建立 child process 進行計算，
 * parent process 負責資料收集和與 kernel module 的通訊
 *
 * @param param: 指向 Task 結構的指標，包含此 thread 的工作範圍
 * @return: void* (符合 pthread 函式簽名)
 */
void *thread(void *param)
{
    Task t = *(Task *) param;  // 取得此 thread 負責的工作範圍

    // 建立 pipe 用於 parent-child process 間的通訊
    int fd[2];
    if (pipe(fd) < 0) {
        printf("Create pipe error.\n");
        return NULL;
    }

    pid_t pid = fork();  // 建立 child process
    char buffer[BUF_SIZE];

    if (pid < 0) {
        // fork() 失敗
        printf("Fork error.\n");
        return NULL;
    } else if (pid == 0) {
        // Child Process: 負責矩陣運算
        close(fd[0]);  // 關閉讀取端，child 只負責寫入
        int r, c, i, sum;

        // 執行分配給此 thread 的矩陣相乘運算
        for (r = t.start_row; r < t.end_row; r++) {
            for (c = 0; c < m2->col; c++) {
                sum = 0;
                // 計算 m[r][c] = sum(m1[r][i] * m2[i][c])
                for (i = 0; i < m1->col; i++) {
                    sum += m1->data[r][i] * m2->data[i][c];
                }
                // 透過 pipe 將結果傳送給 parent process
                sprintf(buffer, "%d", sum);
                write(fd[1], buffer, BUF_SIZE);  // 寫入 pipe
            }
        }
    } else {
        // Parent Process: 負責資料收集和與 kernel module 通訊
        close(fd[1]);  // 關閉寫入端，parent 只負責讀取
        int r, c, i, sum;

        // 從 child process 接收計算結果
        for (r = t.start_row; r < t.end_row; r++) {
            for (c = 0; c < m2->col; c++) {
                read(fd[0], buffer, BUF_SIZE);  // 從 pipe 讀取結果
                m->data[r][c] = atoi(buffer);   // 轉換並儲存到結果矩陣
            }
        }

        // 與 kernel module 通訊，取得 child process 的執行資訊
        int fd = open(proc_file, O_RDWR);  // 開啟 /proc/thread_info
        if (fd == -1) {
            printf("Cannot open %s.\n", proc_file);
            return NULL;
        }

        // 使用 mutex 確保對 /proc 介面的存取是 thread-safe
        pthread_mutex_lock(&mutex);
        usleep(1000);  // 短暫延遲，模擬真實的處理時間

        /* 存取 /proc/thread_info (critical section) */
        char buffer[BUF_SIZE] = {0};
        sprintf(buffer, "%d\n", pid);  // 準備要寫入的 PID (也就是 child process 的 PID)

        // 對於 parent process 來說，fork() 的返回值是 child process 的 PID
        // 向 kernel module 寫入 child process 的 PID
        write(fd, buffer, strlen(buffer));

        // 從 kernel module 讀取該 process 的統計資訊
        read(fd, buffer, BUF_SIZE);
        printf("\t%s", buffer);  // 顯示 thread 統計資訊

        pthread_mutex_unlock(&mutex);  // 釋放 mutex

        wait(NULL);  // 等待 child process 結束，避免產生 zombie process
    }
}

/*
 * write_result() - 將矩陣運算結果寫入檔案
 *
 * 執行檔案輸出操作，將完成的矩陣相乘結果儲存到文字檔案
 * 展示檔案 I/O 操作和 data persistence 概念
 */
void write_result()
{
    FILE *fp = fopen(out_file, "w");  // 以寫入模式建立/開啟檔案
    if (fp == NULL) {
        printf("Cannot create file %s.\n", out_file);
        return;
    }

    // 寫入矩陣維度資訊（第一行）
    fprintf(fp, "%d %d\n", m->row, m->col);

    int r, c;
    // 寫入矩陣元素資料
    for (r = 0; r < m->row; r++) {
        for (c = 0; c < m->col; c++) {
            fprintf(fp, "%d ", m->data[r][c]);
        }
        fprintf(fp, "\n");  // 每列結束後換行
    }
    fclose(fp);  // 關閉檔案，確保資料寫入磁碟
}

/*
 * multiply() - 主要矩陣相乘控制函式
 *
 * 這是程式的核心函式，展示以下重要的 OS 概念：
 * 1. Thread creation and management (pthread_create, pthread_join)
 * 2. Task decomposition and load balancing
 * 3. Parallel computing coordination
 * 4. Performance measurement
 *
 * 演算法：將矩陣 m1 和 m2 相乘，結果存入 m
 * 使用多個 threads 平行處理，每個 thread 負責計算結果矩陣的一部分列
 */
void multiply()
{
    int r, c, d, n;

    // 檢查矩陣相乘的必要條件：m1 的 col 數必須等於 m2 的 row 數
    if (m1->col != m2->row) {
        printf("Cannot do matrix multiplication.\n");
        return;
    }

    // 為結果矩陣配置記憶體空間
    m = malloc(sizeof(Matrix));
    if (m == NULL) {
        printf("Malloc failed.\n");
        return;
    }
    m->row = m1->row;  // 結果矩陣的列數等於 m1 的列數
    m->col = m2->col;  // 結果矩陣的行數等於 m2 的行數

    // 為結果矩陣的二維陣列配置記憶體
    m->data = malloc(sizeof(int *) * m->row);
    for (r = 0; r < m->row; r++) {
        m->data[r] = malloc(sizeof(int) * m->col);
        if (m->data[r] == NULL) {
            printf("Malloc failed.\n");
            return;
        }
    }

    // 準備 multithreading 相關的資料結構
    pthread_t *tids;  // thread ID 陣列
    Task *t;          // 任務分配陣列
    tids = malloc(sizeof(pthread_t) * n_thread);
    t = malloc(sizeof(Task) * n_thread);
    if (tids == NULL || t == NULL) {
        printf("Malloc failed.\n");
        return;
    }

    printf("PID:%d\n", getpid());  // 顯示主程式的 process ID

    int start_row = 0;  // 用於分配工作範圍的起始列

    // 開始計時，用於效能測量
    clock_gettime(CLOCK_MONOTONIC, &startTime);

    // 建立指定數量的 threads 並分配工作
    for (n = 0; n < n_thread; n++) {
        t[n].start_row = start_row;

        // 建立 pthread，執行 thread() 函式
        if (pthread_create(&(tids[n]), NULL, thread, &t[n]) == -1) {
            printf("Create thread failed.\n");
            return;
        }

        // 實現 load balancing：平均分配工作量
        // 如果總列數不能被 thread 數整除，前幾個 threads 多處理一列
        if (n < (m->row % n_thread))
            t[n].end_row = t[n].start_row + m->row / n_thread + 1;
        else
            t[n].end_row = t[n].start_row + m->row / n_thread;

        start_row = t[n].end_row;  // 更新下一個 thread 的起始列
    }

    // 等待所有 threads 完成工作（thread synchronization）
    for (n = 0; n < n_thread; n++) {
        pthread_join(tids[n], NULL);  // 阻塞等待直到指定 thread 結束
    }

    // 停止計時並計算總執行時間
    clock_gettime(CLOCK_MONOTONIC, &endTime);
    totalTime = endTime.tv_sec - startTime.tv_sec;

    write_result();  // 將結果寫入檔案
}

/*
 * main() - 程式進入點
 *
 * 負責程式初始化、參數處理、以及整體流程控制
 * 展示命令列參數處理和程式執行時間測量
 *
 * @param argc: 命令列參數數量
 * @param argv: 命令列參數字串陣列
 * @return: 程式結束狀態碼
 */
int main(int argc, char *argv[])
{
    // 檢查命令列參數數量
    if (argc != 4) {
        printf(
            "Usage: ./MT_matrix [number of worker threads] [file name of input matrix1] [file name of input "
            "matrix2]\n");
        return 0;
    }

    // 解析命令列參數
    n_thread = atoi(argv[1]);   // 指定的工作 thread 數量
    m1 = load_matric(argv[2]);  // 載入第一個矩陣
    m2 = load_matric(argv[3]);  // 載入第二個矩陣

    // 檢查矩陣載入是否成功
    if (m1 == NULL || m2 == NULL) {
        return 0;
    }

    multiply();  // 執行多執行緒矩陣相乘

    // 顯示總執行時間，用於效能分析
    printf("\nElapsed Time: %d (s)\n", totalTime);
}