KPATH = /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KPATH) M=$(shell pwd) modules

clean:
	rm -rf .*.cmd *.o *.ko *.mod.c .tmp_versions modules.order Module.symvers

obj-m := sbe.o
sbe-objs := ber_emulator.o bitmap.o

.PHONY: modules clean
