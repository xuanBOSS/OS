# user/Makefile - 最简版本
include ../common.mk

USER_CFLAGS = $(CFLAGS) -fno-builtin -nostdlib -static -I../include
USER_LDFLAGS = -nostdlib -static

.PHONY: all clean build test-program 

build: ../kernel/proc/initcode.h

all: ../kernel/proc/initcode.h ../kernel/proc/simple_test.h

usys.S: usys.pl
	perl usys.pl > usys.S

%.o: %.c
	$(CC) $(USER_CFLAGS) -c $< -o $@

%.o: %.S
	$(CC) $(USER_CFLAGS) -c $< -o $@

initcode.elf: initcode.o ulib.o printf.o umalloc.o usys.o user.ld
	$(LD) $(USER_LDFLAGS) -T user.ld -o $@ initcode.o ulib.o printf.o umalloc.o usys.o

simple_test.elf: simple_test.o ulib.o printf.o umalloc.o usys.o user.ld
	$(LD) $(USER_LDFLAGS) -T user.ld -o $@ simple_test.o ulib.o printf.o umalloc.o usys.o

%.bin: %.elf
	$(OBJCOPY) -S -O binary $< $@

../kernel/proc/initcode.h: initcode.bin
	@mkdir -p ../kernel/proc
	xxd -i $< > $@
	echo "unsigned int initcode_len = sizeof(initcode_bin);" >> $@

../kernel/proc/simple_test.h: simple_test.bin
	@mkdir -p ../kernel/proc
	xxd -i $< > $@
	echo "unsigned int simple_test_len = sizeof(simple_test_bin);" >> $@

clean:
	rm -f *.o *.elf *.bin usys.S ../kernel/proc/initcode.h ../kernel/proc/simple_test.h *.d
