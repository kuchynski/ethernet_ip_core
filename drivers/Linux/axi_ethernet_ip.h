//
// ethernet_ip
// kuchynskiandrei@gmail.com
// 2024
//

#ifndef axi_ethernet_ip_H
#define axi_ethernet_ip_H

#include <linux/etherdevice.h>

#define NETDEV_NAME "eth_ip"
#define MAX_FRAME_SIZE	(12 + 2 + 1500 + 4 + 4)

struct axi_info;

int ip_core_init(struct device *dev, struct axi_info **pip_core_data);
int ip_core_send_frame(struct axi_info *ip_core_data, unsigned char *data, const size_t len);
size_t ip_core_receive_frame(struct axi_info *ip_core_data, unsigned char *buf);
void ip_core_exit(struct device *dev, struct axi_info *ip_core_data);

#endif