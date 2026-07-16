obj-m += orin_thermal.o

KDIR = /lib/modules/$(shell uname -r)/build
PWD = $(shell pwd)
ccflags-y += -I$(src)/../../include

all:
	make -C $(KDIR) M=$(PWD) modules

clean:
	make -C $(KDIR) M=$(PWD) clean