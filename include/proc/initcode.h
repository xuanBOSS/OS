// proc/initcode.h - 这个文件现在由 user/Makefile 自动生成
// 不需要手写，编译时会自动创建

// 如果需要手动创建临时版本用于测试：
unsigned char initcode[] = {
  0x93, 0x08, 0x00, 0x00, 0x73, 0x00, 0x00, 0x00  // 简单的 ecall
};
unsigned int initcode_len = 8;
