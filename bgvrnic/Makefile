
#EXTRA_CFLAGS += -DDEBUG
EXTRA_CFLAGS += -D __LINUX__ -I$(ROQ)/include

obj-m += bgvrnic.o

bgvrnic-objs :=bgvrnic_main.o bgvrnic_eth.o bgvrnic_verbs.o bgvrnic_cm.o

include $(ROQ)/Makefile.inc
