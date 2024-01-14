//
// ethernet_ip
// kuchynskiandrei@gmail.com
// 2024
//

#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include "axi_ethernet_ip.h"

#define QUEUE_ONE_ENTRY_SHIFT 11
#define QUEUE_ONE_ENTRY_SIZE_BYTES (1 << QUEUE_ONE_ENTRY_SHIFT)
#define QUEUE_CAPACITY_ENTRIES 256

#define AXI_MEMORY_ADDRESS 0x80000000
#define AXI_MEMORY_SIZE_BYTES 64
#define IRQ_NO 186

#define SHIFT_QUEUE_ADDRESS		(0 << 2) // write only
#define SHIFT_QUEUE_CAPACITY	(1 << 2) // write only
#define SHIFT_RX_INDEX			(2 << 2) // write OUT index, read IN index
#define SHIFT_TX_INDEX			(3 << 2) // write IN index, read OUT index
#define SHIFT_IRQ_MASK			(4 << 2) // write only

struct axi_info {
	unsigned char *io_virtual;
	unsigned long io_physical;
	int irq_no;

	unsigned char *buf_virt_tx;
	unsigned char *buf_virt_rx;

	struct completion compl;
	u32 rxqueue_out_index, rxqueue_in_index;
	u32 txqueue_out_index, txqueue_in_index;
};

irqreturn_t ip_core_interrupt(int irq, void* pData)
{
	struct axi_info *ip_core_data = (struct axi_info*)pData;

	iowrite32(0, ip_core_data->io_virtual + SHIFT_IRQ_MASK);
	complete(&ip_core_data->compl);

	return IRQ_HANDLED;
}

size_t ip_core_receive_frame(struct axi_info *ip_core_data, unsigned char *buf)
{
	//aa RX 2.1 check if some frame was awaliable last time
	if (ip_core_data->rxqueue_out_index == ip_core_data->rxqueue_in_index) {
		ip_core_data->rxqueue_in_index = ioread32(ip_core_data->io_virtual + SHIFT_RX_INDEX);

	//aa RX 2.2 if not, check if the frame has been received since then
		if (ip_core_data->rxqueue_out_index == ip_core_data->rxqueue_in_index) {
	//aa RX 2.3 if still not, wait for the frame
			iowrite32(1, ip_core_data->io_virtual + SHIFT_IRQ_MASK);
			wait_for_completion_timeout(&ip_core_data->compl, HZ);
			ip_core_data->rxqueue_in_index = ioread32(ip_core_data->io_virtual + SHIFT_RX_INDEX);
		}
	}

	if (ip_core_data->rxqueue_out_index != ip_core_data->rxqueue_in_index) {
	//aa RX 2.4 copy the frame from the RX ring
		u8 *ptr = ip_core_data->buf_virt_rx + (ip_core_data->rxqueue_out_index << QUEUE_ONE_ENTRY_SHIFT);
		const size_t offset_size = QUEUE_ONE_ENTRY_SIZE_BYTES - 2 * sizeof(u64);
		const size_t size = *(size_t*)(ptr + offset_size);
		const size_t offset_data = (offset_size - size) & ~0xF;

		memcpy(buf, ptr + offset_data, size);
	//aa RX 2.5 free the entry in the RX ring
		ip_core_data->rxqueue_out_index = (ip_core_data->rxqueue_out_index + 1) % QUEUE_CAPACITY_ENTRIES;
		iowrite32(ip_core_data->rxqueue_out_index, ip_core_data->io_virtual + SHIFT_RX_INDEX);

		return size;
	}

	return 0;
}

int ip_core_send_frame(struct axi_info *ip_core_data, unsigned char *data, const size_t len)
{
	const u32 next_in_index = (ip_core_data->txqueue_in_index + 1) % QUEUE_CAPACITY_ENTRIES;

	//aa TX 1.1 check free entries into the TX ring
	if (ip_core_data->txqueue_out_index == next_in_index)
		ip_core_data->txqueue_out_index = ioread32(ip_core_data->io_virtual + SHIFT_TX_INDEX);

	if (ip_core_data->txqueue_out_index != next_in_index) {
	//aa TX 1.2 copy the frame to the TX ring
		u8 *ptr = ip_core_data->buf_virt_tx + (ip_core_data->txqueue_in_index << QUEUE_ONE_ENTRY_SHIFT);
		memcpy(ptr, data, len);

	//aa TX 1.3 inform about it IP core
		ip_core_data->txqueue_in_index = next_in_index;
		iowrite32((len << 16) | ip_core_data->txqueue_in_index << 16, ip_core_data->io_virtual + SHIFT_TX_INDEX);

		return 0;
	}

	return -EBUSY;
}

int ip_core_init(const struct device *dev, struct axi_info **pip_core_data)
{
	int ret = -ENOMEM;
	struct axi_info *ip_core_data = (struct axi_info*)vmalloc(sizeof(struct axi_info));

	if (ip_core_data) {
		memset(ip_core_data, 0, sizeof(struct axi_info)); 

	//aa init 2.1 map IP core address space
		ip_core_data->io_physical = AXI_MEMORY_ADDRESS;
		if(NULL == request_mem_region(ip_core_data->io_physical, AXI_MEMORY_SIZE_BYTES, NETDEV_NAME)) {
			dev_err(dev, "failed to request memory region 0x%lx\n", ip_core_data->io_physical);
		} else {
			ip_core_data->io_virtual = ioremap(ip_core_data->io_physical, AXI_MEMORY_SIZE_BYTES);
			if(ip_core_data->io_virtual == NULL) {
				dev_err(dev, "failed to ioremap memory region 0x%lx\n", ip_core_data->io_physical);
			} else {
	//aa init 2.2 allocate memory for the frame queues
				const size_t buf_size = QUEUE_CAPACITY_ENTRIES * QUEUE_ONE_ENTRY_SIZE_BYTES;

				ip_core_data->buf_virt_tx = kmalloc(buf_size * 2, GFP_KERNEL);
				if (ip_core_data->buf_virt_tx == NULL) {
					dev_err(dev, "failed to allocate memory\n");
				} else {
					dma_addr_t buf_physical = dma_map_single(0, ip_core_data->buf_virt_tx, QUEUE_ONE_ENTRY_SIZE_BYTES, DMA_TO_DEVICE);
					ip_core_data->buf_virt_rx = ip_core_data->buf_virt_tx + buf_size;

	//aa init 2.3 request IRQ line
					init_completion(&ip_core_data->compl);
					ip_core_data->irq_no = IRQ_NO;
					ret = request_irq(ip_core_data->irq_no, ip_core_interrupt, 0, NETDEV_NAME, ip_core_data);
					if(ret) {
						dev_err(dev, "failed to request interrupt #%d\n", ip_core_data->irq_no);
						ip_core_data->irq_no = 0;
					} else {
						*pip_core_data = ip_core_data;
						disable_irq_nosync(ip_core_data->irq_no);

	//aa init 2.4 initialize IP core itself
						iowrite32(0, ip_core_data->io_virtual + SHIFT_IRQ_MASK);
						iowrite32(QUEUE_CAPACITY_ENTRIES, ip_core_data->io_virtual + SHIFT_QUEUE_CAPACITY);
						iowrite32(buf_physical, ip_core_data->io_virtual + SHIFT_QUEUE_ADDRESS);
					}
				}
			}
		}
	}

	if (ret)
		ip_core_exit(ip_core_data);

	return ret;
}

void ip_core_exit(struct axi_info *ip_core_data)
{
	if (ip_core_data) {
		if (ip_core_data->irq_no > 0)
			free_irq(ip_core_data->irq_no, ip_core_data);
		if (ip_core_data->io_virtual) {
			iowrite32(0, ip_core_data->io_virtual + SHIFT_IRQ_MASK);
			iowrite32(0, ip_core_data->io_virtual + SHIFT_QUEUE_ADDRESS);
			iowrite32(0, ip_core_data->io_virtual + SHIFT_QUEUE_CAPACITY);
			release_mem_region(ip_core_data->io_physical, AXI_MEMORY_SIZE_BYTES);
			kfree(ip_core_data->io_virtual);
		}
		if (ip_core_data->buf_virt_tx)
			kfree(ip_core_data->buf_virt_tx);
		vfree(ip_core_data);
	}
}
