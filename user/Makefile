# user/Makefile
include ../common.mk

# 用户程序特殊编译选项
USER_CFLAGS = $(CFLAGS) -fno-builtin -fno-stack-protector -nostdlib -static
USER_LDFLAGS = -nostdlib -static

# 用户程序目标文件
USER_OBJS = initcode.o

# 生成的文件
INITCODE_ELF = initcode.elf
INITCODE_BIN = initcode
INITCODE_H = ../kernel/proc/initcode.h
INITCODE_ASM = initcode.asm

.PHONY: build clean

# 编译用户程序源文件
%.o: %.c
	@$(CC) $(USER_CFLAGS) -c $< -o $@

# 链接生成 ELF 文件
$(INITCODE_ELF): $(USER_OBJS) user.ld
	@$(LD) $(USER_LDFLAGS) -T user.ld -o $(INITCODE_ELF) $(USER_OBJS)

# 转换为二进制文件
$(INITCODE_BIN): $(INITCODE_ELF)
	@$(OBJCOPY) -S -O binary $(INITCODE_ELF) $(INITCODE_BIN)

# 生成反汇编文件（可选，用于调试）
$(INITCODE_ASM): $(INITCODE_ELF)
	@$(OBJDUMP) -S $(INITCODE_ELF) > $(INITCODE_ASM)

# 生成 initcode.h 供内核包含
$(INITCODE_H): $(INITCODE_BIN) $(INITCODE_ASM)
	@mkdir -p $(dir $(INITCODE_H))
	@xxd -i $(INITCODE_BIN) > $(INITCODE_H)
	@echo "unsigned int initcode_len = sizeof(initcode);" >> $(INITCODE_H)

# 构建目标：生成 initcode.h
build: $(INITCODE_H)

# 清理目标
clean:
	@rm -f $(USER_OBJS) $(INITCODE_ELF) $(INITCODE_BIN) $(INITCODE_H) $(INITCODE_ASM) *.d
	