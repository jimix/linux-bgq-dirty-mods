/*                                                                  */
/* Licensed Materials - Property of IBM                             */
/*                                                                  */
/* Blue Gene/Q                                                      */
/*                                                                  */
/* (c) Copyright IBM Corp. 2011, 2012 All Rights Reserved           */
/*                                                                  */
/* US Government Users Restricted Rights - Use, duplication or      */
/* disclosure restricted by GSA ADP Schedule Contract with IBM      */
/* Corporation.                                                     */
/*                                                                  */
/* This software is available to you under either the GNU General   */
/* Public License (GPL) version 2 or the Eclipse Public License     */
/* (EPL) at your discretion.                                        */
/*                                                                  */

//! \file  mudm_ras.c
//! \brief Implementation of RAS for MUDM.

#include <spi/include/kernel/debug.h>
#include <asm/bluegene_ras.h>

#include "mudm_ras.h"
#include "mudm_trace.h"
#include "mudm_macro.h"
#include "common.h"

#include <platforms/bgq/bgq.h>

int bluegene_writeRAS(unsigned int msg_id, unsigned char is_binary,
                      unsigned short len, void *msg)
{
	if (is_binary)
		return bgq_ras_write(msg_id, msg, len);
	return bgq_ras_puts(msg_id, msg);
}

int injectRAWRAS(uint32_t message_id, size_t raslength, const uint64_t* rasdata){
 return bluegene_writeRAS((unsigned int) message_id, (unsigned char) 1, (unsigned short) raslength, (void*)rasdata);
}

