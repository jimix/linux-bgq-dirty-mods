// This program is free software; you can redistribute  it and/or modify it
// under  the terms of  the GNU General  Public License as published by the
// Free Software Foundation;  either version 2 of the  License, or (at your
// option) any later version.
//

#ifndef __BLUEGENE_H__
#define __BLUEGENE_H__

#ifdef __KERNEL__
#ifndef __ASSEMBLY__

 
void __section(.init.text) __cold notrace bgq_init_fw(void);
void bgq_udbg_putc(char c);
void __section(.init.text) __cold notrace bgq_init_IRQ(void);

int bluegene_writeRAS(unsigned int msgId, 
		      unsigned char isBinary,
		      unsigned short len,
		      void* details);

int bluegene_printr(u64 id,
		    const char* format,
		    ...);

int bluegene_read_inbox(void *arg);                       
                 
int bluegene_register_stdin_handler(int (*handler)(char*, unsigned));

int bluegene_writeToMailboxConsole(char *msg,
				   unsigned msglen);

int bluegene_sendBlockStatus(u16 status, u16 argc, u64 argv[]);

int bluegene_takeCPU(unsigned cpu, 
		     unsigned threadMask, 
		     void (*entry)(void*),
		     void* data);

int bluegene_install_interrupt_vector(void*, u64);

int bluegene_getPersonality(void* buff, unsigned buffSize);

void bluegene_halt(void);

extern const char *bgq_config_addr;
extern u32 bgq_io_reset_block_id;

#endif // __ASSEMBLY__ 
#endif // __KERNEL__ 
#endif // __BLUEGENE_H__

