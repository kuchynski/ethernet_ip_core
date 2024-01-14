//
// ethernet_ip
// kuchynskiandrei@gmail.com
// 2024
//

#include "axi_ethernet_ip.h"

struct network_driver_info
{
	struct axi_info *ip_core_data;
	struct rtnl_link_stats64 stats;
	struct task_struct *rx_task;
	bool exit;
};

static int rx_thread(void *arg)
{
	struct net_device *netdev = (struct net_device*)arg;
	struct network_driver_info *adapter = netdev_priv(netdev);
	struct sk_buff *skb = NULL;

	while (adapter->exit == false) {
		if (skb == NULL) {
	//aa RX 1. alloc skb
			skb = __netdev_alloc_skb_ip_align(netdev, MAX_FRAME_SIZE, GFP_KERNEL);
			if (skb) {
				skb->dev = netdev;
				skb->ip_summed = CHECKSUM_UNNECESSARY;
			} else {
				msleep(1000);
			}
		} else {
	//aa RX 2. get frame
			const size_t size = ip_core_receive_frame(adapter->ip_core_data, skb->data);

			if (size) {
	//aa RX 3. set all required by the network stack
				skb->protocol = eth_type_trans(skb, netdev);
				skb->tail = skb->data + size;
				skb->len = size;
				adapter->stats.rx_packets++;
				adapter->stats.rx_bytes += size;

	//aa RX 4. sent frame to the network stack
				local_bh_disable();
				netif_receive_skb(skb);
				skb = NULL;
				local_bh_enable();
			}
		}
	}

	if (skb)
		dev_kfree_skb(skb);

	return 0;
}

static netdev_tx_t netdev_tx(struct sk_buff *skb, struct net_device *netdev)
{
	struct network_driver_info *adapter = netdev_priv(netdev);

	skb_tx_timestamp(skb);
	//aa TX 1. send skb
	if (ip_core_send_frame(adapter->ip_core_data, skb->data, skb->len) == 0) {
		adapter->stats.tx_packets++;
		adapter->stats.tx_bytes += skb->len;
		dev_kfree_skb(skb);

		return NETDEV_TX_OK;
	}

	return NETDEV_TX_BUSY;
}

int netdev_open(struct net_device *netdev)
{
	struct network_driver_info *adapter = netdev_priv(netdev);
	//aa init 2. intialize IP core
	const int ret = ip_core_init(&netdev->dev, &adapter->ip_core_data);
	
	if (ret) {
		dev_err(&netdev->dev, "failed to init ethernet ip core: %d\n", ret);
	} else {
	//aa init 3. start RX thread
		memset(adapter, 0x0, sizeof(struct network_driver_info));
		adapter->exit = false;
		adapter->rx_task = kthread_run(rx_thread, netdev, "ethip_rx");

	//aa init 4. inform the network stack we are ready
		netif_carrier_on(netdev);
		netif_start_queue(netdev);
	}

	return ret;
}

int netdev_close(struct net_device *netdev)
{
	struct network_driver_info *adapter = netdev_priv(netdev);

	netif_stop_queue(netdev);
	netif_carrier_off(netdev);

	adapter->exit = true;
	kthread_stop(adapter->rx_task); 
	ip_core_exit(adapter->ip_core_data);

	return 0;
}

static void netdev_get_stats64(struct net_device *netdev, struct rtnl_link_stats64 *stats)
{
	struct network_driver_info *adapter = netdev_priv(netdev);

	memcpy(stats, &adapter->stats, sizeof(struct rtnl_link_stats64));
}

struct net_device *ethip_netdev;
static const struct net_device_ops netdev_ops = {
	.ndo_open			= netdev_open,
	.ndo_stop			= netdev_close,
	.ndo_start_xmit		= netdev_tx,
	.ndo_get_stats64	= netdev_get_stats64,
};

static int netdev_init(void)
{
	//aa init 1. create the network device
	struct net_device *netdev = alloc_netdev_mqs(sizeof(struct network_driver_info), NETDEV_NAME, NET_NAME_UNKNOWN, ether_setup, 1, 1);
	int ret = -ENOMEM;

	if (netdev) 
		pr_err("failed to allocate net device\n");
	else {
		netdev->netdev_ops = &netdev_ops;
		netdev->priv_flags |= IFF_UNICAST_FLT;

		ret = register_netdev(netdev);
		if (ret) {
			pr_err("failed to register net device\n");
			free_netdev(netdev);
		}
		ethip_netdev = netdev;
	}

	return ret;
}

static void netdev_exit(void)
{
	unregister_netdev(ethip_netdev);
	free_netdev(ethip_netdev);
}

module_init(netdev_init);
module_exit(netdev_exit);

MODULE_AUTHOR("kuchynskiandrei@gmail.com");
MODULE_LICENSE("GPL v2");
