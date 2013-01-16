# When you built Linux you installed modules somewhere using:
#  INSTALL_MOD_PATH=
# This should be the absolute path to that.
BGQ_MODULES := /gpfs/DDNgpfs1/jimix/work/bgq/linux/build/roq/modules

# version of Linux you built, you can find this by looking at the
# directory that was created in $(BGQ_MODULES)/lib/modules
BGQ_VERSION := 3.4.22-BGQ-rc2-00019-g2e23f88-dirty

KDIR := $(BGQ_MODULES)/lib/modules/$(BGQ_VERSION)/build
ROQ := $(shell pwd)

SUBDIRS := bgmudm bgvrnic
#SUBDIRS += roq_db

%-all: %
	$(MAKE) -C $< KDIR=$(KDIR) ROQ=$(ROQ) all

%-install: %
	$(MAKE) -C $< KDIR=$(KDIR) ROQ=$(ROQ) BGQ_MODULES=$(BGQ_MODULES) install

%-clean: %
	$(MAKE) -C $< KDIR=$(KDIR) ROQ=$(ROQ) clean

all: $(SUBDIRS:%=%-all)

# We must build bgmudm before bgvrnic so that we know we have
# correctly satisfied all symbol interdependnecies
bgvrnic-all: bgmudm-all

install: $(SUBDIRS:%=%-install)

clean: $(SUBDIRS:%=%-clean)

