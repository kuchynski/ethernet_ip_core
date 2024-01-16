# linux kernel location
#KDIR:=~/projects/linux/kernel/linux
KDIR:=/usr/src/linux-headers-5.10.103-v7l+

# toolchain location
#CROSS_TOP_DIR:=/home/andrei/opt/toolchains/arm-xilinx-linux-gnueabi
#export CROSS_COMPILE:=$(CROSS_TOP_DIR)/bin/arm-xilinx-linux-gnueabi-
CC:=gcc

obj-m := ethernet_ip.o
ethernet_ip-objs := network_driver.o axi_ethernet_ip.o
ccflags-y += 

default:
	$(MAKE) -C $(KDIR) ARCH=arm M=$(shell pwd) modules

#clean:
	@rm -f *.o *.order *.mod *.mod.c *.symvers .*.*.cmd

