# OS-MultiThread-KernelModule
## 專案概述
這個專案實作了一個結合 Linux kernel module 與 multi-thread 程式的矩陣乘法系統

系統利用多核心處理器的優勢，透過多執行緒進行負載分配，並使用 kernel module 記錄每個執行緒的執行時間與 context switch 次數

## 系統架構
### 主要元件
1. **MT_matrix** - 多執行緒矩陣乘法程式
   - 負責創建多個 worker thread
   - 將矩陣乘法工作分配給各 worker thread
   - 收集並顯示執行統計資訊

2. **My_proc** - Linux kernel module
   - 創建 `/proc/thread_info` 檔案
   - 記錄每個 worker thread 的執行時間 (utime)
   - 記錄 context switch 次數 (nvcsw + nivcsw)

## 目錄結構
```
.
├── src/                  # 原始碼目錄
│   ├── MT_matrix.c      # 多執行緒矩陣乘法主程式
│   └── My_proc.c        # Linux kernel module
├── include/             # 標頭檔目錄
│   ├── matrix.h         # 矩陣相關結構與函式宣告
│   └── proc_module.h    # kernel module 相關定義
├── Makefile             # 編譯腳本
├── spec.md              # 詳細規格說明
└── README.md            # 本文件
```

## 系統需求
- Linux 作業系統
- GCC 編譯器
- Linux 核心開發標頭檔
- pthread 函式庫
- 建議配置：4 CPU 核心、4GB RAM

## 安裝必要套件
```bash
# Ubuntu/Debian
sudo apt-get update
sudo apt-get install build-essential linux-headers-$(uname -r)

# RHEL/CentOS/Fedora
sudo yum groupinstall "Development Tools"
sudo yum install kernel-devel kernel-headers
```

## 編譯方式
### 編譯所有元件
```bash
make all
```

範例輸出:
```
make all  
gcc -o MT_matrix src/MT_matrix.c -lpthread
cp src/My_proc.c My_proc.c
cp include/proc_module.h proc_module.h
sed -i 's|../include/proc_module.h|proc_module.h|' My_proc.c
make -C /lib/modules/6.14.0-27-generic/build M=/home/phyrexxxxx/OS-MultiThread-KernelModule modules
make[1]: Entering directory '/usr/src/linux-headers-6.14.0-27-generic'
make[2]: Entering directory '/home/phyrexxxxx/OS-MultiThread-KernelModule'
warning: the compiler differs from the one used to build the kernel
  The kernel was built by: x86_64-linux-gnu-gcc-13 (Ubuntu 13.3.0-6ubuntu2~24.04) 13.3.0
  You are using:           gcc-13 (Ubuntu 13.3.0-6ubuntu2~24.04) 13.3.0
  CC [M]  My_proc.o
  MODPOST Module.symvers
  CC [M]  My_proc.mod.o
  CC [M]  .module-common.o
  LD [M]  My_proc.ko
  BTF [M] My_proc.ko
Skipping BTF generation for My_proc.ko due to unavailability of vmlinux
make[2]: Leaving directory '/home/phyrexxxxx/OS-MultiThread-KernelModule'
make[1]: Leaving directory '/usr/src/linux-headers-6.14.0-27-generic'
rm -f My_proc.c proc_module.h
```

### 單獨編譯
```bash
# 編譯多執行緒程式
gcc -I./include -pthread -o MT_matrix src/MT_matrix.c

# 編譯核心模組
make kernel_module
```

### 清理編譯檔案
```bash
make clean
```

## 執行方式
### 1. 載入核心模組
```bash
sudo make load
# 或使用以下指令
sudo insmod My_proc.ko
```
範例輸出:
```
sudo insmod My_proc.ko
```


### 2. 執行矩陣乘法程式
```bash
./MT_matrix [執行緒數量] [矩陣1檔案] [矩陣2檔案]

# 範例
./MT_matrix 4 matrix1.txt matrix2.txt
```

### 3. 卸載核心模組
```bash
sudo make unload
# 或使用以下指令
sudo rmmod My_proc
```
範例輸出:
```
sudo rmmod My_proc || true >/dev/null
```

### 快速測試
```bash
# 自動載入模組、執行測試、卸載模組
make check
```

## 輸入檔案格式
矩陣檔案格式如下：
- 第一行：矩陣的列數與行數
- 其餘行：矩陣元素（元素值範圍 1-1000）

範例 (3x4 矩陣)：
```
3 4
593 329 377 596
13 47 266 276
997 415 783 971
```

## 輸出說明
### 1. 終端機輸出
程式執行時會顯示：
- 主程序 PID
- 各 worker thread 資訊：
  - ThreadID：執行緒 ID
  - Time：執行時間（毫秒）
  - Context switch times：上下文切換次數

範例輸出：
```
PID:12345
    ThreadID:12346 Time:15(ms) context switch times:3
    ThreadID:12347 Time:12(ms) context switch times:2
    ThreadID:12348 Time:14(ms) context switch times:4
    ThreadID:12349 Time:13(ms) context switch times:2

Elapsed Time: 1 (s)
```

### 2. 檔案輸出
- `result.txt`：矩陣乘法結果，格式與輸入檔案相同

## 工作分配策略
本實作採用**按列分配 (Row-wise Distribution)** 策略：
- 每個 worker thread 負責計算結果矩陣的連續數列
- 當列數無法平均分配時，前面的 worker thread 會多分配一列
- 例如：10 列分給 3 個 worker thread 時，分配為 4、3、3 列

## Race Condition 處理
程式使用 `pthread_mutex` 來保護 critical section：
- 在寫入 `/proc/thread_info` 時使用 mutex
- 確保 kernel module 能正確記錄每個 worker thread 的資訊
- 避免多個 worker thread 同時寫入造成的 race condition

## 效能考量
### 執行緒數量選擇
- **執行緒數 < CPU 核心數**：可能無法充分利用所有核心
- **執行緒數 = CPU 核心數**：通常能達到最佳效能
- **執行緒數 > CPU 核心數**：可能因 context switch 增加而降低效能

### 建議測試配置
測試不同執行緒數量以找出最佳配置：
- 1, 2, 3, 4, 8, 16, 24, 32 個執行緒

## 故障排除
## 1. 無法載入核心模組
```
sudo make load
```
輸出:
```
sudo insmod My_proc.ko
insmod: ERROR: could not insert module My_proc.ko: Key was rejected by service
make: *** [Makefile:29: load] Error 1
```
參考解法:
- [【SO】insmod: ERROR: could not insert module HelloWorld.ko: Operation not permitted】](https://stackoverflow.com/questions/58546126/insmod-error-could-not-insert-module-helloworld-ko-operation-not-permitted)

依序執行以下指令:
```
sudo apt install mokutil
sudo mokutil --disable-validation
```
範例輸出:
```
$ sudo apt install mokutil
[sudo] password for phyrexxxxx: 
Reading package lists... Done
Building dependency tree... Done
Reading state information... Done
mokutil is already the newest version (0.6.0-2build3).
mokutil set to manually installed.
0 upgraded, 0 newly installed, 0 to remove and 251 not upgraded.

$ sudo mokutil --disable-validation
password length: 8~16
input password: 
input password again:
```

系統會要求你建立一組密碼。密碼至少要有 8 個字元。重新啟動後，UEFI 會詢問你是否要變更安全性設定，選擇「Yes」

接著會要求你輸入先前建立的密碼。有些 UEFI 韌體不要求輸入整組密碼，而是要求輸入其中某些字元，例如第 1、3 個字元等

### 2. 找不到 /proc/thread_info
```bash
# 確認模組已載入
lsmod | grep My_proc

# 檢查 proc 檔案
ls -la /proc/thread_info
```

### 3. 編譯錯誤
```bash
# 安裝必要的開發工具
sudo apt-get install build-essential linux-headers-$(uname -r)

# 檢查 pthread 函式庫
ldconfig -p | grep pthread
```

## 注意事項
1. 執行 kernel module 操作需要 root 權限
2. 確保輸入矩陣檔案格式正確
3. 第一個矩陣的 col 數必須等於第二個矩陣的 row 數
4. 建議在虛擬機器中測試 kernel module
5. 記得在測試後卸載 kernel module

## 參考資料
- [Matrix Multiplication](https://en.wikipedia.org/wiki/Matrix_multiplication)
- [pthreads(7) — Linux manual page](https://man7.org/linux/man-pages/man7/pthreads.7.html)
- [Linux Kernel Module Programming Guide](https://sysprog21.github.io/lkmpg/)
- [struct task_struct](https://elixir.bootlin.com/linux/v5.15.42/source/include/linux/sched.h)
