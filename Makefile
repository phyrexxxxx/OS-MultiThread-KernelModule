# ================================================================
# 多執行緒與 Kernel Module 專案的 Makefile
# 此專案包含一個 userspace 的多執行緒矩陣程式和一個 kernel module
# ================================================================

# 定義主要的目標名稱
TARGET_MODULE := My_proc          # kernel module 的名稱
MATRIX_PROG := MT_matrix          # userspace 多執行緒矩陣程式的名稱

# ================================================================
# Kernel Module 編譯設定
# ================================================================

# 告訴 kernel build system 要編譯的 module object file
obj-m := $(TARGET_MODULE).o

# 指定 kernel module 的 source files（將 .c 編譯成 .o）
$(TARGET_MODULE)-objs := src/My_proc.o

# ================================================================
# Userspace 程式編譯設定
# ================================================================

CC := gcc                         # 使用 gcc 編譯器
CFLAGS := -I./include -pthread    # 編譯選項：包含 include 目錄，啟用 pthread 支援

# ================================================================
# Kernel 相關路徑設定
# ================================================================

# 取得目前執行中的 kernel 的 build 目錄路徑
KDIR := /lib/modules/$(shell uname -r)/build

# 取得目前工作目錄的絕對路徑
PWD := $(shell pwd)

# ================================================================
# 主要編譯目標
# ================================================================

# 預設目標：同時編譯 userspace 程式和 kernel module
all: $(MATRIX_PROG) kernel_module

# 編譯 userspace 的多執行緒矩陣程式
$(MATRIX_PROG): src/MT_matrix.c
	$(CC) $(CFLAGS) -o $@ $<

# 編譯 kernel module
kernel_module:
	# 將 source file 複製到根目錄（kernel build system 的要求）
	cp src/My_proc.c My_proc.c
	
	# 使用 kernel build system 編譯 module
	# -C $(KDIR)：切換到 kernel build 目錄
	# M=$(PWD)：告訴 build system 我們的 module source 在目前目錄
	# modules：編譯 modules 目標
	$(MAKE) -C $(KDIR) M=$(PWD) modules
	
	# 清理暫時複製的檔案
	rm -f My_proc.c

# ================================================================
# 清理目標
# ================================================================

clean:
	# 清理 userspace 程式
	rm -f $(MATRIX_PROG)
	
	# 清理 kernel module 編譯產生的檔案
	# .o：object files
	# .ko：kernel object files (compiled modules)
	# .mod.*：module metadata files
	# .symvers：symbol version files
	# .order：module build order files
	rm -f *.o *.ko *.mod.* *.symvers *.order
	rm -f src/*.o
	
	# 使用 kernel build system 清理
	$(MAKE) -C $(KDIR) M=$(PWD) clean

# ================================================================
# Module 載入/卸載目標
# ================================================================

# 載入 kernel module 到系統中
load:
	sudo insmod $(TARGET_MODULE).ko

# 從系統中卸載 kernel module
# || true >/dev/null：如果 module 不存在也不會報錯
unload:
	sudo rmmod $(TARGET_MODULE) || true >/dev/null

# ================================================================
# 測試目標
# ================================================================

# 完整測試流程：編譯 → 卸載舊 module → 載入新 module → 執行測試 → 卸載 module
check: all
	$(MAKE) unload                              # 先卸載可能存在的 module
	$(MAKE) load                                # 載入新編譯的 module
	./$(MATRIX_PROG) 4 test1.txt test2.txt    # 執行測試（4個執行緒，讀取兩個測試檔案）
	$(MAKE) unload                              # 測試完成後卸載 module