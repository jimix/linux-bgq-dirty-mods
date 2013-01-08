PWD := $(shell pwd)

include $(KDIR)/.config

EXTRA_CFLAGS += -D__LINUX__ -I$(ROQ)/include

obj-m += bgmudm.o

bgmudm-objs := bgmudm_main.o mudm_atomic.o mudm_bcast.o mudm.o \
	mudm_cleanup.o mudm_connect.o mudm_descriptor.o mudm_init.o \
	mudm_ip.o mudm_lock.o mudm_msg.o mudm_personality.o mudm_pkt.o \
	mudm_poll_bcast.o mudm_ras.o mudm_trace.o mudm_utils.o rdma_object.o

include $(ROQ)/Makefile.inc
