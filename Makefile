KPATH = /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KPATH) M=$(shell pwd) modules

clean:
	rm -rf .*.cmd *.o *.ko *.mod.c .tmp_versions modules.order Module.symvers

obj-m := ber_emulator.o

.PHONY: modules clean
