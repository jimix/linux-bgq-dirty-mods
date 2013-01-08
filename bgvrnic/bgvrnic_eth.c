/*                                                                  */
/* Blue Gene/Q Ethernet Over Torus Driver                           */
/*                                                                  */
/* author: Andrew Tauferner <ataufer@us.ibm.com>                    */
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
/* This software is available to you under the GNU General Public   */
/* License (GPL) version 2.                                         */
/*                                                                  */

#include <linux/netdevice.h>
#include <linux/if_ether.h>
#include <linux/if_arp.h>
#include <linux/ip.h>
#include <linux/etherdevice.h>
#include <linux/inetdevice.h>
#include <linux/spinlock.h>

#include <net/arp.h>

#include "bgvrnic.h"


static const u32 default_msg = NETIF_MSG_DRV | NETIF_MSG_PROBE | NETIF_MSG_LINK
    | NETIF_MSG_IFUP | NETIF_MSG_IFDOWN;


struct bgvrnic_ether_dev {
	struct net_device* net_dev;
	struct bgvrnic_device* vrnic_dev;
	spinlock_t lock;
	u32 msg_enable;
};


/* Ethernet ARP packet */
struct earp_pkt {
        struct arphdr hdr;
        u8 ar_sha[ETH_ALEN];
        u8 ar_sip[4];
        u8 ar_tha[ETH_ALEN];
        u8 ar_tip[4];
};


static int bgvrnic_open(struct net_device* net_dev)
{
	int rc = 0;
ENTER
	netif_start_queue(net_dev);

EXIT
	return rc;
}


static int bgvrnic_stop(struct net_device* net_dev)
{
	int rc = 0;
ENTER
	
EXIT
	return rc;
}

static inline void bgvrnic_dump_bytes(char* buffer, 
					int   buffer_len,
					u8* data, 
					int data_len,
					int bytes_per_line,
					int prefix)
{
	int rc;
	u32 bytes_dumped = 0;

	if (prefix) {
		rc = snprintf(buffer, buffer_len, "%04x ", bytes_dumped);
		if (rc > 0) {
			buffer+= rc;
			buffer_len -= rc;
		}
	}
	
	while (bytes_dumped < data_len && (buffer_len - 3) > 0) {
		int rc = snprintf(buffer, buffer_len, "%02x ", *data);

		if (rc <= 0)
			break;

		data++;
		bytes_dumped++;
		buffer_len -= rc;
		buffer += rc;

		if ((bytes_dumped % bytes_per_line) == 0) {
			*buffer = '\n';
			buffer++;
			buffer_len--;
			if (prefix && (bytes_dumped < data_len)) {
				rc = snprintf(buffer, buffer_len, "%04x ", bytes_dumped);
				if (rc > 0) {
					buffer+= rc;
					buffer_len -= rc;
				} else
					break;
			}
		}
	}	
	if (*buffer != '\n')
		*(++buffer) = '\n';	

	return;
}


#ifdef BGVRNIC_DEBUG
char _buffer[8192];
static void bgvrnic_dump_skb(struct sk_buff* skb)
{
	u8* mac;

	if (!skb || !skb->len)
		return;

	switch (skb->protocol) {
		case ETH_P_IP: {
			u8* octet;
			struct iphdr* ip = (struct iphdr*) ip_hdr(skb);
			
			printk(KERN_EMERG "protocol %04x (IP)\n", skb->protocol);
			printk(KERN_EMERG "length %d\n", skb->len);
			octet = (u8*) &ip->saddr;
			printk(KERN_EMERG "Src IP %d.%d.%d.%d\n", octet[0], octet[1], octet[2], octet[3]);
			octet = (u8*) &ip->daddr;
			printk(KERN_EMERG "Dst IP %d.%d.%d.%d\n", octet[0], octet[1], octet[2], octet[3]);
		
			break;
		}

		case ETH_P_ARP: {
			struct earp_pkt* pkt = (struct earp_pkt*) arp_hdr(skb);
			u8* octet;

			printk(KERN_EMERG "protocol %04x (ARP)\n", skb->protocol);
			printk(KERN_EMERG "HW address format %04x\n", pkt->hdr.ar_hrd);
			printk(KERN_EMERG "Protocol address format %04x\n", pkt->hdr.ar_pro);
			printk(KERN_EMERG "HW address length %d\n", pkt->hdr.ar_hln);
			printk(KERN_EMERG "Protocol address length %d\n", pkt->hdr.ar_pln);
			switch (pkt->hdr.ar_op) {
				case ARPOP_REQUEST:
					printk(KERN_EMERG "Op code ARPOP_REQUEST\n");
					break;

				case ARPOP_REPLY:
					printk(KERN_EMERG "Op code ARPOP_REPLY\n");
					break;
	
				default:
					printk(KERN_EMERG "Op code %04x\n", pkt->hdr.ar_op);
			}
			mac = pkt->ar_sha;
			printk(KERN_EMERG "Src MAC %02x:%02x:%02x:%02x:%02x:%02x\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
			octet = pkt->ar_sip;
			printk(KERN_EMERG "Src IP %d.%d.%d.%d\n", octet[0], octet[1], octet[2], octet[3]);
			mac = pkt->ar_tha;
			printk(KERN_EMERG "Dst MAC %02x:%02x:%02x:%02x:%02x:%02x\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
			octet = pkt->ar_tip;
			printk(KERN_EMERG "Dst IP %d.%d.%d.%d\n", octet[0], octet[1], octet[2], octet[3]);
			
			break;
		}

		case ETH_P_IPV6:
			printk(KERN_EMERG "protocol %04x (IPv6)\n", skb->protocol); 
			break;

		case ETH_P_PAUSE:
			printk(KERN_EMERG "protocol %04x (pause frame)\n", skb->protocol);
			break;

		default: {
			int data_len = skb->len;
			u8* data = skb->data;

			printk(KERN_EMERG "protocol %04x (unknown)\n", skb->protocol);
			bgvrnic_dump_bytes(_buffer, sizeof(_buffer), data, data_len, 32, 1);
			printk(KERN_EMERG "%s", _buffer);
		} 
	}

	return;
}
#endif	

static int bgvrnic_create(struct sk_buff* skb,
	  		  struct net_device* net_dev,
			  unsigned short type,
			  const void* daddr,
			  const void* saddr,
			  unsigned len)
{
	struct ethhdr* eth = (struct ethhdr*) skb_push(skb, ETH_HLEN);

	eth->h_proto = htons(type);
	memcpy(eth->h_source, net_dev->dev_addr, net_dev->addr_len);
	if (type == ETH_P_IP) {
		u8* octet = (u8*) &ip_hdr(skb)->daddr;

		/* Create a MAC address from the IP */
		eth->h_dest[0] = octet[1] & 0x40 ? 0x66 : 0x42;
		eth->h_dest[1] = octet[1] & 0xf;
		eth->h_dest[2] = octet[2] >> 4;
		eth->h_dest[3] = octet[2] & 0xf;
		eth->h_dest[4] = octet[3] >> 4;
		eth->h_dest[5] = octet[3] & 0xf;	
	} else if (daddr)  
		memcpy(eth->h_dest, daddr, net_dev->addr_len);
	else
		memcpy(eth->h_dest, net_dev->dev_addr, net_dev->addr_len);

	return net_dev->hard_header_len;
}


#define MU_NODE_ADDR(A,B,C,D,E) (u32) ((((A) & 0x3f) << 24) | (((B) & 0x3f) << 18) | (((C) & 0x3f) << 12) | (((D) & 0x3f) << 6) | ((E) & 0x3f))
	
static int bgvrnic_hard_start_xmit(struct sk_buff* skb,
			      	   struct net_device* net_dev)
{
	u32 torus_addr;
	int send_rc;
	int rc = NETDEV_TX_OK;
	u32 frame_len = skb->len - skb->data_len;
	int nr_frags = skb_shinfo(skb)->nr_frags;
	struct bgvrnic_ether_dev* priv = netdev_priv(net_dev);
	dma_addr_t data_addr = dma_map_single(priv->vrnic_dev->ib_dev.dma_device, skb->data, frame_len, DMA_TO_DEVICE);

ENTER
	/* We don't do fragmented IP frames yet. */
       	BUG_ON(nr_frags);
	BUG_ON(frame_len > 512);

	/* Extract the destination IP address from the frame. */
	if (skb->protocol == htons(ETH_P_IP)) {
		u8* octets = (u8*) &ip_hdr(skb)->daddr;	

		torus_addr = MU_NODE_ADDR(octets[1] & 0x1f, octets[2] >> 4, octets[2] & 0x1f, octets[3] >> 4, octets[3] & 0x1f);
		send_rc = mudm_send_msg(priv->vrnic_dev->mu_context, (void*) skb, torus_addr, MUDM_IP_IMMED, 0,
					(void*) data_addr, frame_len);
		if (send_rc == -EBUSY) {
			rc = NETDEV_TX_BUSY;
		} else if (send_rc) {
			printk(KERN_EMERG "%s:%d - mudm_send_msg() failed, rc=%d\n", __func__, __LINE__, send_rc);
			goto out;
		} else
			net_dev->stats.tx_bytes += skb->len;
	} else if (skb->protocol == htons(ETH_P_IPV6)) {
		printk(KERN_INFO "%s : IPv6 unsupported.\n", net_dev->name);
		goto out; 
	} else {
		printk(KERN_EMERG "Unsupported protocol %04x\n", skb->protocol);
	}

	net_dev->trans_start = jiffies;
	net_dev->stats.tx_packets++;
out:
	dma_unmap_single(priv->vrnic_dev->ib_dev.dma_device, data_addr, frame_len, DMA_TO_DEVICE);
	dev_kfree_skb_any(skb);

EXIT
	return rc;
}


static void bgvrnic_tx_timeout(struct net_device* net_dev)
{
ENTER

EXIT
	return;
}


static int bgvrnic_change_mtu(struct net_device* net_dev,
			      int newMTU)
{
	int rc = 0;
	int max_frame_len = newMTU + ETH_ALEN;
ENTER
	if (max_frame_len > 512)
		rc = -EINVAL;
	else
		net_dev->mtu = newMTU;

EXIT
	return rc;
}


struct net_device_stats* bgvrnic_get_stats(struct net_device* net_dev)
{
ENTER

EXIT
	return &net_dev->stats;
}


static int bgvrnic_set_mac_addr(struct net_device* net_dev,
				void* mac_addr)
{
	int rc = 0;
	struct sockaddr* sock_addr = (struct sockaddr*) mac_addr;

ENTER
	if (is_valid_ether_addr(sock_addr->sa_data)) 
		memcpy(net_dev->dev_addr, sock_addr->sa_data, net_dev->addr_len);
	else
		rc = -EADDRNOTAVAIL;

EXIT
	return rc;
}


static int bgvrnic_validate_addr(struct net_device* net_dev)
{
ENTER

EXIT
	return 0;
}


static const struct net_device_ops bgvrnic_netdev = {
        .ndo_open               = bgvrnic_open,
        .ndo_stop               = bgvrnic_stop,
        .ndo_start_xmit         = bgvrnic_hard_start_xmit,
        .ndo_tx_timeout         = bgvrnic_tx_timeout,
        .ndo_change_mtu         = bgvrnic_change_mtu,
        .ndo_set_mac_address    = bgvrnic_set_mac_addr,
        .ndo_validate_addr      = bgvrnic_validate_addr,
	.ndo_get_stats		= bgvrnic_get_stats,
};

static const struct header_ops bgvrnic_header_ops = {
	.create			= bgvrnic_create,
};

struct net_device* __devinit bgvrnic_alloc_netdev(struct bgvrnic_device* dev,
						  const char* name,
						  const char* mac)
{
	struct net_device* net_dev = alloc_etherdev(sizeof(struct bgvrnic_ether_dev));
	struct bgvrnic_ether_dev* ether_dev;
ENTER

	if (net_dev) {
		int rc;

		net_dev->netdev_ops = &bgvrnic_netdev;
		net_dev->header_ops = &bgvrnic_header_ops;
		ether_dev = (struct bgvrnic_ether_dev*) netdev_priv(net_dev);
		ether_dev->net_dev = net_dev;
		ether_dev->vrnic_dev = dev;
		ether_dev->msg_enable = netif_msg_init(eth_debug, default_msg);
		net_dev->features |= NETIF_F_VLAN_CHALLENGED;
		net_dev->flags |= IFF_NOARP;
		net_dev->flags &= ~IFF_BROADCAST;
		spin_lock_init(&ether_dev->lock);
		memcpy(net_dev->dev_addr, mac, sizeof(net_dev->dev_addr));
		if (name)
			strlcpy(net_dev->name, name, sizeof(net_dev->name));
		rc = register_netdev(net_dev);
		if (rc) {
			printk(KERN_EMERG "Unable to register network device, rc=%d\n", rc);
			free_netdev(net_dev);	
			net_dev = NULL;
		}
	}
EXIT

	return net_dev;
}


void  bgvrnic_free_netdev(struct net_device* net_dev)
{
ENTER
	if (net_dev) {
		netif_stop_queue(net_dev);
		unregister_netdev(net_dev);
		free_netdev(net_dev);
	}
EXIT
	return;	
}


int bgvrnic_rx(struct net_device* net_dev,
	       	u8* data,
		u32 data_len)
{
	int rc;
	struct sk_buff* skb = dev_alloc_skb(data_len + 2);

	if (likely(skb)) {
		skb_reserve(skb, 2);
		skb->ip_summed = CHECKSUM_UNNECESSARY;
		skb->dev = net_dev;
		skb_put(skb, data_len);	
		memcpy(skb->data, data, data_len);
		skb->protocol = eth_type_trans(skb, net_dev);
		rc = netif_rx(skb); 
		if (rc != NET_RX_SUCCESS) {
			net_dev->stats.rx_dropped++;
			net_dev->stats.rx_errors++;
		} else {
			net_dev->stats.rx_packets++;
			net_dev->stats.rx_bytes += data_len;
		}
	} else {
		if (printk_ratelimit())
			printk(KERN_EMERG "SKB allocation failure!\n");
		net_dev->stats.rx_dropped++;
		net_dev->stats.rx_errors++;
		rc = -ENOMEM;
	}	
	
	return rc;
}
