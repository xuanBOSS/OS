# 根目录 Makefile
include common.mk

KERN = kernel
USER = user
MKFS = mkfs
KERNEL_ELF = kernel-qemu
CPUNUM = 1
FS_IMG = fs.img

# 用户程序列表
USER_PROGS = $(wildcard user/_*)

.PHONY: clean build qemu qemu-gdb user-build kernel-build mkfs-build fs-build help

# 先构建用户程序
user-build:
	@$(MAKE) -C $(USER) build

# 构建 mkfs 工具
mkfs-build:
	@echo "=== Building mkfs tool ==="
	@$(MAKE) -C $(MKFS) build

# 生成文件系统镜像
fs-build: mkfs-build user-build
	@echo "=== Creating filesystem image ==="
	@if [ -f $(FS_IMG) ]; then \
		echo "Removing old $(FS_IMG)"; \
		rm -f $(FS_IMG); \
	fi
	@./$(MKFS)/mkfs $(FS_IMG) $(USER_PROGS)
	@echo "Filesystem image created: $(FS_IMG)"

# 生成 fs.img（独立目标）
fs.img: fs-build

# 再构建内核（依赖用户程序）
kernel-build: user-build
	@$(MAKE) -C $(KERN) build

# 总体构建目标
build: kernel-build fs-build

# QEMU 运行配置
QEMU     =  qemu-system-riscv64
QEMUOPTS =  -machine virt -bios none -kernel $(KERNEL_ELF) 
QEMUOPTS += -m 128M -smp $(CPUNUM) -nographic
QEMUOPTS += -drive file=$(FS_IMG),if=none,format=raw,id=x0
QEMUOPTS += -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0

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
	@$(MAKE) -C $(MKFS) clean
	@rm -f $(KERNEL_ELF) $(FS_IMG) .gdbinit
	@echo "Cleaned all build artifacts"

# 帮助信息
help:
	@echo "RISC-V OS Build System"
	@echo ""
	@echo "Available targets:"
	@echo "  make build       - Compile everything (user + kernel + fs.img)"
	@echo "  make user-build  - Compile user programs only"
	@echo "  make kernel-build- Compile kernel only (requires user build first)"
	@echo "  make mkfs-build  - Compile mkfs tool only"
	@echo "  make fs-build    - Generate filesystem image (fs.img)"
	@echo "  make fs.img      - Same as fs-build"
	@echo "  make qemu        - Run kernel in QEMU"
	@echo "  make qemu-gdb    - Run kernel in QEMU with GDB debugging"
	@echo "  make clean       - Remove all compiled files"
	@echo "  make help        - Show this help message"
	@echo ""
	@echo "Example workflow:"
	@echo "  make clean       # Clean everything"
	@echo "  make build       # Build everything"
	@echo "  make qemu        # Run the kernel"
