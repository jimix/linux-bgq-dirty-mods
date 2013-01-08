/*
  author: Andrew Tauferner <ataufer@us.ibm.com>
 
  Copyright 2011, 2012 International Business Machines

  This program is free software; you can redistribute  it and/or modify it
  under  the terms of  the GNU General  Public License as published by the
  Free Software Foundation;  either version 2 of the  License, or (at your
  option) any later version.
*/

#ifndef __BLUEGENE_RAS_H__
#define __BLUEGENE_RAS_H__

#include <linux/types.h>

#define BG_RAS_FILE "/dev/bgras"


/* Max payload 2K right now due to the size of the RAS message in the DB.
 * It would be possible to do a 15K max payload (16K mailbox - ~1K header)
 */
#define BG_RAS_MAX_PAYLOAD 2048

typedef struct {
	uint32_t msgId;			/* See possible IDs below. */
	uint8_t isBinary;		/* Non-zero if this RAS event is binary type. */
	uint16_t len;			/* Length of RAS data.  Measured in 64b words for binary.  Measured in bytes for ASCII. */
	char msg[BG_RAS_MAX_PAYLOAD];	/* RAS data which follows the header. */
} bg_ras;


#ifdef BGRAS
#define ID_STR(name,value) { #name, #value },

/* RAS event IDs for the Linux environment.  This includes the kernel, ramdisk,  
 * etc.  Add enumerated values here for new messages, followed by embeded XML meta 
 * data. */
char *bgras_ids[][2] = {
#else
#define ID_STR(name,value) 
extern char *bgras_ids[][2];
#endif

#define	BGRAS_ID_NONE 0xa0000

ID_STR(BGRAS_ID_PCIE_UNSUPPORTED_ADAPTER, 0xa0001)
#define	BGRAS_ID_PCIE_UNSUPPORTED_ADAPTER 0xa0001
/*
<rasevent id="000a0001" component="LINUX" category="PCI" severity="WARN"
 message="[PCIe] An unsupported PCIe adapter was detected:  $(DETAILS)"
 description="Unsupported PCIe adapters may cause unpredictable behavior.  Supported adapter part numbers may be seen in /bgsys/linux/ionfloor/etc/sysconfig/bgqadapters."
 service_action="Please see the event details for a description of the unsupported adapter."
/>
 */

ID_STR(BGRAS_ID_PCIE_MISSING_ADAPTER_VPD, 0xa0002)
#define	BGRAS_ID_PCIE_MISSING_ADAPTER_VPD 0xa0002
/*
<rasevent id="000a0002" component="LINUX" category="PCI" severity="FATAL"
 message="[PCIe] No PCIe adapter VPD detected."
 description="The node configuration indicates PCIe enablement but the adapter VPD cannot be located."
 service_action="Please correct the node configuration or verify that a supported PCI adapter is installed.  If a PCI adapter is installed it may require replacement."
 control_action="COMPUTE_IN_ERROR"
/>
 */

ID_STR(BGRAS_ID_BGSYS_MOUNT_FAILURE, 0xa0003)
#define	BGRAS_ID_BGSYS_MOUNT_FAILURE 0xa0003
/*
<rasevent id="000a0003" component="LINUX" category="Software_Error" severity="FATAL"
 message="[BOOT] All attempts to mount /bgsys failed:  $(DETAILS)"
 description="All attempts to mount the export directory from the server specified in the database have failed."
 service_action="Please review the mount errors reported in the node's log.  Ensure that the appropriate server IP address and export directory have been specified.  Also ensure that the specified server is active, is exporting the specified directory and is configured to allow the subnet being used by the nodes to mount the export."
 control_action="SOFTWARE_IN_ERROR"
/>
*/

ID_STR(BGRAS_ID_GPFS_START_FAILURE, 0xa0004)
#define	BGRAS_ID_GPFS_START_FAILURE 0xa0004
/*
<rasevent id="000a0004" component="LINUX" category="Software_Error" severity="FATAL"
 message="[GPFS] GPFS failed to start:  $(DETAILS)"
 description="The node was unable to start GPFS because it is not a member of bgio cluster."
 service_action="Use 'mmaddnode &amp;lt;failing node hostname&amp;gt;' on the service node to add this node to the GPFS bgio cluster."
 control_action="SOFTWARE_IN_ERROR"
/>
*/

ID_STR(BGRAS_ID_NO_NETWORK_INTERFACE_DEFINED, 0xa0005)
#define	BGRAS_ID_NO_NETWORK_INTERFACE_DEFINED 0xa0005

/*
<rasevent id="000a0005" component="LINUX" category="Software_Error" severity="FATAL"
 message="[BOOT] No network interface was defined for the node:  $(DETAILS)" 
 description="The control system must indicate at least one network interface (ib0, eth0 or eth1) to configure in order for the node to successfully boot."
 service_action="The system administrator needs to properly configure networking for the failing node."
 control_action="SOFTWARE_IN_ERROR"
/>
*/

ID_STR(BGRAS_ID_SCRIPT_FAILURE, 0xa0006)
#define BGRAS_ID_SCRIPT_FAILURE	0xa0006
/*
<rasevent id="000a0006" component="LINUX" category="Software_Error" severity="WARN"
 message="[LINUX] An init script has encountered an error or failed to properly execute:  $(DETAILS)" 
 description="The init process was not able to fully execute one of the init script. "
 service_action="Consult the node logs for further information regarding this failure and correct the indicated problem."
/>
*/

ID_STR(BGRAS_ID_BGQ_DISTRO_MISSING, 0xa0007)
#define	BGRAS_ID_BGQ_DISTRO_MISSING 0xa0007
/*
<rasevent id="000a0007" component="LINUX" category="Software_Error" severity="FATAL"
 message="[BOOT] The specified BG/Q Linux Distribution path is missing or invalid:  $(DETAILS)" 
 description="In order for boot of the node to complete successfully the path to a valid BG/Q Linux Distribution must be specified. The default path is /bgsys/linux/ionfloor."
 service_action="Ensure that the path specified in the error message exists and is accessible.  If using a custom built ramdisk you may need to rebuild your ramdisk using a valid --runos &amp;lt;dir&amp;gt; specified."
 control_action="SOFTWARE_IN_ERROR"
/>
*/

ID_STR(BGRAS_ID_GPFS_INIT_FAILURE, 0xa0008)
#define BGRAS_ID_GPFS_INIT_FAILURE 0xa0008
/*
<rasevent id="000a0008" component="LINUX" category="Software_Error" severity="FATAL"
  message="[GPFS] GPFS on the specified cluster node failed to initialize:  $(DETAILS}"
  description="GPFS failed to initialize on the specified node due to an error."
  service_action="Consult the I/O Node and GPFS logs for the specified node for further information and/or send this information to support for further analysis."
 control_action="SOFTWARE_IN_ERROR"
/>
*/

ID_STR(BGRAS_ID_PCIE_LINK_DEGRADED, 0xa0009)
#define	BGRAS_ID_PCIE_LINK_DEGRADED 0xa0009
/*
 <rasevent id="000a0009" component="LINUX" category="PCI" severity="WARN"
  message="[PCIe] The PCIe adapter is running in a suboptimal configuration.  $(DETAILS)"
  description="The PCIe link speed and/or width is not set at its maximum capability.  The maximum bus speed is 5.0 GT/s.  The maximum width is x8."
  service_action="Please verify your PCIe adapter configuration.  The event details describe the current PCIe link speed and width."
 />
 */ 

ID_STR(BGRAS_ID_NETWORK_CONFIG_FAILURE, 0xa000a)
#define	BGRAS_ID_NETWORK_CONFIG_FAILURE	0xa000a
/*
 <rasevent id="000a000a" component="LINUX" category="Software_Error" severity="FATAL"
 message="[BOOT] Network configuration failed for the indicated node:  $(DETAILS)"
 description="One of ib0, eth0 or eth1 must be configured in order for the node to successfully boot."
 service_action="Please check the logs for the I/O node for additional information on the network failure. Also ensure that the system administrator has properly configured networking for the failing node. "
control_action="SOFTWARE_IN_ERROR"
/>
*/

ID_STR(BGRAS_ID_INT_VECTOR_FAILURE, 0xa000b)
#define	BGRAS_ID_INT_VECTOR_FAILURE 0xa000b
/*
 <rasevent id="000a000b" component="LINUX" category="Software_Error" severity="FATAL"
 message="[BOOT] The installation of interrupt vectors has failed."
 description="Proper machine check handling is not possible on this node."
 service_action="The event details further describe the failure."
 control_action="SOFTWARE_IN_ERROR"
/>
*/

ID_STR(BGRAS_ID_GPFS_HOSTNAME_FAILURE, 0xa000c)
#define BGRAS_ID_GPFS_HOSTNAME_FAILURE 0xa000c
/*
 <rasevent id="000a000c" component="LINUX" category="Software_Error" severity="FATAL"
  message="[GPFS] GPFS was unable to resolve a hostname for the indicated node:  $(DETAILS)"
  description="A hostname must be configured for nodes to successfully start GPFS but one could not be resolved for the indicated node."
  service_action="The system administrator needs to ensure that a hostname has been configured for the failing node.  If one has been configured, consult the node's logs for possible causes of the name resolution failure. "
/>
*/

ID_STR(BGRAS_ID_KERNEL_PANIC, 0xa000d)
#define	BGRAS_ID_KERNEL_PANIC 0xa000d
/*
  <rasevent id="000a000d" component="LINUX" category="Software_Error" severity="FATAL"
   message="[LINUX] A kernel panic has occurred:  $(DETAILS) "
   description="The kernel has encountered an error condition and is no longer functioning."
   service_action="Send this RAS event and relevant logs to IBM service if this failure persists."
   control_action="SOFTWARE_IN_ERROR,END_JOB,FREE_COMPUTE_BLOCK"
 />
*/

ID_STR(BGRAS_ID_ETHERNET_LINK_TIMEOUT, 0xa000e)
#define BGRAS_ID_ETHERNET_LINK_TIMEOUT 0xa000e
/*
  <rasevent id="000a000e" component="LINUX" category="Ethernet" severity="FATAL"
   message="[ETHERNET] An Ethernet link was not established:  $(DETAILS)"
   description="The Ethernet port was not able to establish a link within the required time period."
   service_action="Please verify all Ethernet cable connections to the adapter and switch port.  Also, verify that the node's I/O configuration matches the installed adapter."
   control_action="COMPUTE_IN_ERROR,END_JOB,FREE_COMPUTE_BLOCK"
  />
*/ 

ID_STR(BGRAS_ID_IB_LINK_TIMEOUT, 0xa000f)
#define	BGRAS_ID_IB_LINK_TIMEOUT 0xa000f
/*
  <rasevent id="000a000f" component="LINUX" category="Infiniband" severity="FATAL"
   message="[INFINIBAND] An Infiniband link was not established:  $(DETAILS)"
   description="The Infiniband port was not able to establish a link within the required time period."
   service_action="Please verify all Infiniband cable connections to the adapter and switch port.  Also, verify that the node's I/O configuration matches the installed adapter."
   control_action="COMPUTE_IN_ERROR,END_JOB,FREE_COMPUTE_BLOCK"
  />
*/

ID_STR(BGRAS_ID_NODE_HEALTH_MONITOR_WARNING, 0xa0010)
#define BGRAS_ID_NODE_HEALTH_MONITOR_WARNING 0xa0010
/*
  <rasevent id="000a0010" component="LINUX" category="Software_Error" severity="WARN"
   message="[BGHEALTHMON] The Blue Gene Node Health Monitor has detected a potential resource problem:  $(DETAILS)"
   description="A critical resource being monitored on a Blue Gene node has exceeded the configured threshold." 
   service_action="Review the exhausted resource indicated in the RAS message and see the referenced log file for additional details. Corrective action may be required."
  />
*/

ID_STR(BGRAS_ID_ETHERNET_LINK_LOST, 0xa0011)
#define BGRAS_ID_ETHERNET_LINK_LOST 0xa0011
/*
  <rasevent id="000a0011" component="LINUX" category="Ethernet" severity="FATAL"
   message="[ETHERNET] The Ethernet link was lost:  $(DETAILS)"
   description="The Ethernet port lost its link to the switch after a link had been established."
   service_action="Please verify all Ethernet cable connections to the adapter and switch port.  Also, verify that the node's I/O configuration matches the installed adapter."
   control_action="END_JOB,FREE_COMPUTE_BLOCK"
  />
*/

ID_STR(BGRAS_ID_IB_LINK_LOST, 0xa0012)
#define BGRAS_ID_IB_LINK_LOST 0xa0012
/*
  <rasevent id="000a0012" component="LINUX" category="Infiniband" severity="FATAL"
   message="[INFINIBAND] The Infiniband link was lost:  $(DETAILS)"
   description="The Infiniband port lost its link to the switch after a link had been established."
   service_action="Please verify all Infiniband cable connections to the adapter and switch port.  Also, verify that the node's I/O configuration matches the installed adapter."
   control_action="END_JOB,FREE_COMPUTE_BLOCK"
  />
*/

ID_STR(BGRAS_ID_ROOT_FS_UNRESPONSIVE, 0xa0013)
#define BGRAS_ID_ROOT_FS_UNRESPONSIVE 0xa0013
/*
  <rasevent id="000a0013" component="LINUX" category="Software_Error" severity="WARN"
   message="[LINUX] The node's root filesystem is unresponsive:  $(DETAILS)"
   description="Attempts to read from the mounted root filesystem no longer complete in a timely manner which may result in node instability."
   service_action="Please verify all network cable connections to the adapter and switch port.  Ensure that any gateways between the node and fileserver are properly functioning.  Also, verify that the fileserver that is serving /bgsys is functioning properly."
  />
*/

#define	BGRAS_ID_MAX 0xaffff 
#ifdef BGRAS
};
#endif

#endif // __BLUEGENE_RAS_H__
