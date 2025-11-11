# user/Makefile - 最简版本
include ../common.mk

USER_CFLAGS = $(CFLAGS) -fno-builtin -nostdlib -static -I../include
USER_LDFLAGS = -nostdlib -static

.PHONY: all clean build test-program 

build: ../kernel/proc/initcode.h

all: ../kernel/proc/initcode.h ../kernel/proc/test.h ../kernel/proc/security_test.h

test-program: ../kernel/proc/test.h

security-test: ../kernel/proc/security_test.h

usys.S: usys.pl
	perl usys.pl > usys.S

%.o: %.c
	$(CC) $(USER_CFLAGS) -c $< -o $@

%.o: %.S
	$(CC) $(USER_CFLAGS) -c $< -o $@

initcode.elf: initcode.o user.ld
	$(LD) $(USER_LDFLAGS) -T user.ld -o $@ initcode.o

test.elf: test.o ulib.o printf.o umalloc.o usys.o user.ld
	$(LD) $(USER_LDFLAGS) -T user.ld -o $@ test.o ulib.o printf.o umalloc.o usys.o

security_test.elf: security_test.o ulib.o printf.o umalloc.o usys.o user.ld
	$(LD) $(USER_LDFLAGS) -T user.ld -o $@ security_test.o ulib.o printf.o umalloc.o usys.o

%.bin: %.elf
	$(OBJCOPY) -S -O binary $< $@

../kernel/proc/initcode.h: initcode.bin
	@mkdir -p ../kernel/proc
	xxd -i $< > $@
	echo "unsigned int initcode_len = sizeof(initcode_bin);" >> $@

../kernel/proc/test.h: test.bin
	@mkdir -p ../kernel/proc
	xxd -i $< > $@
	echo "unsigned int test_len = sizeof(test_bin);" >> $@

../kernel/proc/security_test.h: security_test.bin
	@mkdir -p ../kernel/proc
	xxd -i $< > $@
	echo "unsigned int security_test_bin_len = sizeof(security_test_bin);" >> $@

clean:
	rm -f *.o *.elf *.bin usys.S ../kernel/proc/initcode.h ../kernel/proc/test.h  ../kernel/proc/security_test.h *.d
