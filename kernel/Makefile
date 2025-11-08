# kernel/Makefile
include ../common.mk

# 获取所有子目录（排除当前目录和隐藏目录）
MODULES = $(shell ls -d */ 2>/dev/null | grep -v "^\." | sort)

# 收集所有 .o 文件
OBJS = $(shell find . -name "*.o" 2>/dev/null)

# 内核输出文件
KERNEL_ELF = ../kernel-qemu
KERNEL_LD = kernel.ld

# 用户程序头文件路径
INITCODE_H = proc/initcode.h

.PHONY: build clean $(MODULES) check-initcode

# 检查用户程序是否已编译
check-initcode:
	@if [ ! -f $(INITCODE_H) ]; then \
		echo "ERROR: $(INITCODE_H) not found!"; \
		echo "User program must be compiled first."; \
		exit 1; \
	fi

# 构建所有子模块
$(MODULES): 
	@$(MAKE) -C $@ build

# 链接内核
$(KERNEL_ELF): check-initcode $(MODULES) $(KERNEL_LD)
	@$(LD) $(LDFLAGS) -T $(KERNEL_LD) -o $(KERNEL_ELF) $(OBJS)

# 构建目标
build: $(KERNEL_ELF)

# 清理目标
clean:
	@for d in $(MODULES); do \
		$(MAKE) -C $$d clean 2>/dev/null || true; \
	done
	@rm -f $(KERNEL_ELF) $(OBJS)
	@find . -name "*.d" -delete
