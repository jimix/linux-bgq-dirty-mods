# When you built Linux you installed modules somewhere using:
#  INSTALL_MOD_PATH=
# This should be the absolute path to that.
BGQ_MODULES := /bgsys/bgq/eic/scratch/jimix/work/linux/build/roq/modules

# version of Linux you built, you can find this by looking at the
# directory that was created in $(BGQ_MODULES)/lib/modules
BGQ_VERSION := 3.4.22-BGQ-rc2-00019-g2e23f88

KDIR := $(BGQ_MODULES)/lib/modules/$(BGQ_VERSION)/build
ROQ := $(shell pwd)

SUBDIRS := bgmudm bgvrnic
#SUBDIRS += roq_db

%-all: %
	$(MAKE) -C $^ KDIR=$(KDIR) ROQ=$(ROQ) all

%-install: %
	$(MAKE) -C $^ KDIR=$(KDIR) ROQ=$(ROQ) install

%-clean: %
	$(MAKE) -C $^ KDIR=$(KDIR) ROQ=$(ROQ) clean

all: $(SUBDIRS:%=%-all)

install: $(SUBDIRS:%=%-install)

clean: $(SUBDIRS:%=%-clean)

