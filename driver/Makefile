obj-m += bme280.o

CSTD_FLAG := -std=gnu11

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

install:
	make -C /lib/modules/$(shell uname -r)/build M="$(CURDIR)" modules_install

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

