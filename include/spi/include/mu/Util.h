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

#ifndef	_MUSPI_UTIL_H_ /* Prevent multiple inclusion */
#define	_MUSPI_UTIL_H_


/**
 * \file Util.h
 *
 * \brief C Header File containing Message Unit SPI Utility Definitions
 *
 */


#include "kernel_impl.h" 
#include <hwi/include/common/compiler_support.h>


__BEGIN_DECLS

/**
 * \addtogroup env_vars_ Environment Variables
 *
 * \internal
 * Environment variable documentation uses the custom doxygen tags "\envs",
 * "\env", and "\default". These tags are defined as the following ALIASES in
 * the Doxyfile:
 *
 * \code
 *   ALIASES  = default="\n \par Default:\n"
 *   ALIASES += envs{3}="\addtogroup env_vars_\2 \3 \n \ingroup env_vars_\1"
 *   ALIASES += env{2}="\addtogroup env_vars_\1 \n \anchor \2 \section \2 "
 * \endcode
 * 
 * \endinternal
 *
 * \envs{,muspi,MU SPI}
 *
 * The MU SPI functions do not internally query any environment variables,
 * however, as the MU SPIs operate on a node scope it is recommended that
 * the following environment variables be used by any software library that
 * uses the MU SPIs. Failure to coordinate the MU SPI configuration between
 * independently developed software libraries can lead to unexpected results.
 *
 * \env{muspi,MUSPI_NUMBATIDS}
 * Number of base address table IDs per process reserved for use by an MU SPI
 * application.
 * \default 0
 * 
 * \env{muspi,MUSPI_NUMCLASSROUTES}
 * Number of collective classroutes reserved for use by an MU SPI application.
 * \default 0
 * 
 * \env{muspi,MUSPI_NUMINJFIFOS}
 * Number of injection fifos per process reserved for use by an MU SPI
 * application.
 * \default 0
 *
 * \env{muspi,MUSPI_NUMRECFIFOS}
 * Number of reception fifos per process reserved for use by an MU SPI
 * application.
 * \default 0
 * 
 * \env{muspi,MUSPI_INJFIFOSIZE}
 * The size, in bytes, of each injection FIFO.  These
 * FIFOs store 64-byte descriptors, each describing a memory buffer to be
 * sent on the torus.  Making this larger can reduce overhead when there are
 * many outstanding messages.  Making this smaller can increase that overhead.
 * \default 65536
 *
 * \env{muspi,MUSPI_RECFIFOSIZE}
 * The size, in bytes, of each reception FIFO.  Incoming
 * torus packets are stored in this fifo until software can process
 * them.  Making this larger can reduce torus network congestion.  Making this
 * smaller leaves more memory available to the application.
 * \default 1048576
 */

/**
 * \brief __INLINE__ definition
 * 
 * Option 1:
 * Make all functions be "static inline":
 * - They are inlined if the compiler can do it
 * - If the compiler does not inline it, a single copy of the function is
 *   placed in the translation unit (eg. xxx.c)for use within that unit.
 *   The function is not externalized for use by another unit...we want this
 *   so we don't end up with multiple units exporting the same function,
 *   which would result in linker errors.
 *
 * Option 2:
 * A GNU C model: Use "extern inline" in a common header (this one) and provide
 * a definition in a .c file somewhere, perhaps using macros to ensure that the
 * same code is used in each case. For instance, in the header file:
 *
 * \verbatim
   #ifndef INLINE
   # define INLINE extern inline
   #endif
   INLINE int max(int a, int b) {
     return a > b ? a : b;
   }
   \endverbatim
 *
 * ...and in exactly one source file (in runtime/SPI), that is included in a
 * library...
 *
 * \verbatim
   #define INLINE
   #include "header.h"
   \endverbatim
 * 
 * This allows inlining, where possible, but when not possible, only one 
 * instance of the function is in storage (in the library).
 */
#ifndef __INLINE__
#define __INLINE__ extern inline
#endif


/**
 * \brief DCBT instruction
 */
#define muspi_dcbt(vaddr,inc) { do { asm volatile ("dcbt %0,%1" :: "b" (vaddr), "r" (inc) : "memory"); } while(0); }


/**
 * \brief DCBT L2 instruction
 */
#define muspi_dcbt_l2(vaddr,inc) { do { asm volatile ("dcbt 0x2,%0,%1" :: "b" (vaddr), "r" (inc) : "memory"); } while(0); }


/**
 * \brief DCBZ instruction
 */
#define muspi_dcbz(vaddr,inc) { do { asm volatile ("dcbz %0,%1" :: "b" (vaddr), "r" (inc) : "memory"); } while(0); }


/**
 * \brief Vector Load QPX Inline
 */
#define VECTOR_LOAD_NU(si,sb,fp) \
  do { \
    asm volatile("qvlfdx %0,%1,%2" : "=f" (fp) : "b" (si), "r" (sb)); \
  } while(0)


/**
 * \brief Vector Store QPX Inline
 */
#define VECTOR_STORE_NU(si,sb,fp) \
  do { \
    asm volatile("qvstfdx %0,%1,%2" :: "f" (fp), "b" (si), "r" (sb) :"memory"); \
  } while(0)



__END_DECLS


#endif /* _MUSPI_UTIL_H_  */
