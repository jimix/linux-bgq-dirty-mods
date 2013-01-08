
KDIR   := $(mods)/lib/modules/$(vers)/build
PWD    := $(shell pwd)

include $(KDIR)/.config

all: 
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) modules

install:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) modules_install

clean:
	$(RM) *.o *.ko *.mod.c .*.cmd
	$(RM) Module.symvers modules.order
	$(RM) -r .tmp_versions
