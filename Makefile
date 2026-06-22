obj-m := gpioevt.o
gpioevt-objs := main.o circular_buffer.o gpio.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
