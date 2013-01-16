
#EXTRA_CFLAGS += -DDEBUG
EXTRA_CFLAGS += -D__LINUX__ -I$(ROQ)/include

KBUILD_EXTRA_SYMBOLS += $(ROQ)/bgmudm/Module.symvers

obj-m += bgvrnic.o

bgvrnic-objs := bgvrnic_main.o bgvrnic_eth.o bgvrnic_verbs.o bgvrnic_cm.o

include $(ROQ)/Makefile.inc
