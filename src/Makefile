obj-m += xmir_patcher.o

ifeq ($(ARCH),arm64)
MY_CFLAGS += -mpc-relative-literal-loads
ccflags-y += ${MY_CFLAGS}
CC += ${MY_CFLAGS}
endif

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules EXTRA_CFLAGS="${MY_CFLAGS}"

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
