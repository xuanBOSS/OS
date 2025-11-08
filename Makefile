# 根目录 Makefile
include common.mk

KERN = kernel
USER = user
KERNEL_ELF = kernel-qemu
CPUNUM = 3
FS_IMG = none

.PHONY: clean build qemu qemu-gdb user-build kernel-build help

# 先构建用户程序
user-build:
	@$(MAKE) -C $(USER) build

# 再构建内核（依赖用户程序）
kernel-build: user-build
	@$(MAKE) -C $(KERN) build

# 总体构建目标
build: kernel-build

# QEMU 运行配置
QEMU     =  qemu-system-riscv64
QEMUOPTS =  -machine virt -bios none -kernel $(KERNEL_ELF) 
QEMUOPTS += -m 128M -smp $(CPUNUM) -nographic

# 调试
GDBPORT = $(shell expr `id -u` % 5000 + 25000)
QEMUGDB = $(shell if $(QEMU) -help | grep -q '^-gdb'; \
	then echo "-gdb tcp::$(GDBPORT)"; \
	else echo "-s -p $(GDBPORT)"; fi)

# qemu运行
qemu: build
	$(QEMU) $(QEMUOPTS)

# GDB调试
.gdbinit: .gdbinit.tmpl-riscv
	sed "s/:1234/:$(GDBPORT)/" < $^ > $@

qemu-gdb: build .gdbinit
	$(QEMU) $(QEMUOPTS) -S $(QEMUGDB)

# 清理
clean:
	@$(MAKE) -C $(USER) clean
	@$(MAKE) -C $(KERN) clean
	@rm -f $(KERNEL_ELF) .gdbinit

# 帮助信息
help:
	@echo "RISC-V OS Build System"
	@echo ""
	@echo "Available targets:"
	@echo "  make build       - Compile everything (user + kernel)"
	@echo "  make user-build  - Compile user programs only"
	@echo "  make kernel-build- Compile kernel only (requires user build first)"
	@echo "  make qemu        - Run kernel in QEMU"
	@echo "  make qemu-gdb    - Run kernel in QEMU with GDB debugging"
	@echo "  make clean       - Remove all compiled files"
	@echo "  make help        - Show this help message"
	@echo ""
	@echo "Example workflow:"
	@echo "  make build       # First time compilation"
	@echo "  make qemu        # Run the kernel"
