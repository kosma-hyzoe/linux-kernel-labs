// SPDX-License-Identifier: GPL-2.0

#include "linux/dma-direction.h"
#include "linux/spinlock_types.h"
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <uapi/linux/serial_reg.h>
#include <linux/pm_runtime.h>
#include <linux/pm_runtime.h>
#include <linux/iomap.h>
#include <linux/miscdevice.h>
#include <linux/interrupt.h>
#include <linux/atomic.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>

#define SERIAL_RESET_COUNTER    0
#define SERIAL_GET_COUNTER      1
#define SERIAL_BUFSIZE          16

/* Add your code here */
static ssize_t serial_write_pio(struct file *f, const char __user *buf,
                         size_t sz, loff_t *off);
static ssize_t serial_write_dma(struct file *f, const char __user *buf,
                         size_t sz, loff_t *off);
static ssize_t serial_read(struct file *f, char __user *buf,
                        size_t sz, loff_t *off);
static long serial_ioctl(struct file *file, unsigned int cmd,
                               unsigned long arg);

struct file_operations serial_fops_pio = {
        .write = serial_write_pio,
        .read = serial_read,
        .unlocked_ioctl = serial_ioctl,
        /* so that the file is marked as used when written to */
        .owner = THIS_MODULE
};

struct file_operations serial_fops_dma = {
        .write = serial_write_pio,
        .read = serial_read,
        .unlocked_ioctl = serial_ioctl,
        .owner = THIS_MODULE
};

struct serial_dev {
        /* TODO: more on iomem */
        void __iomem *regs;
        struct miscdevice miscdev;
        atomic_t counter;
        char rx_buf[SERIAL_BUFSIZE];
        char tx_buf[SERIAL_BUFSIZE];
        unsigned int buf_rd;
        unsigned int buf_wr;
        wait_queue_head_t wq;
        struct resource *res;
        struct device *dev;
        spinlock_t lock;

        dma_addr_t fifo_dma_addr;
        struct dma_chan *txchan;

};


static int serial_init_dma(struct serial_dev *serial)
{
        int ret;
        struct dma_slave_config txconf = {};
        serial->fifo_dma_addr = dma_map_resource(serial->dev,
                                                 serial->res->start
                                                 + UART_TX * 4,
                                                 4, DMA_TO_DEVICE, 0);
        if (dma_mapping_error(serial->dev, serial->fifo_dma_addr))
                return -ENOMEM;
        txconf.direction = DMA_MEM_TO_DEV;
        txconf.dst_addr_width = DMA_SLAVE_BUSWIDTH_1_BYTE;
        txconf.dst_addr = serial->fifo_dma_addr;
        ret = dmaengine_slave_config(serial->txchan, &txconf);
        if (ret)
                return ret;
        return 0;
        // size_t sz = dma_opt_mapping_size(&pdev->dev);
        // dma_alloc_coherent(pdev->dev, sz,
}


static int serial_clean_dma(struct serial_dev *serial)
{
        int ret;

        ret = dmaengine_terminate_sync(serial->txchan);
        if (ret)
                return ret;
        dma_unmap_resource(serial->dev, serial->fifo_dma_addr, 4, DMA_TO_DEVICE,
                           0);
        dma_release_channel(serial->txchan);
        return 0;
}

static long serial_ioctl(struct file *file, unsigned int cmd,
                               unsigned long arg)
{
        struct miscdevice *miscdev_ptr;
        struct serial_dev *serial;
        unsigned int __user *argp = (unsigned int __user *)arg;

        /* ...it was set automatically! TODO: how? */
        miscdev_ptr = file->private_data;
        serial = container_of(miscdev_ptr, struct serial_dev, miscdev);

        switch (cmd) {
                case SERIAL_GET_COUNTER:
                        if (put_user(atomic_read(&serial->counter), argp))
                                return -EFAULT;
                        break;
                case SERIAL_RESET_COUNTER:
                        atomic_set(&serial->counter, 0);
                        break;
                default:
                        return -ENOTTY;
        }
        return 0;

}

static u32 reg_read(struct serial_dev *serial, unsigned int reg)
{
        return ioread32(serial->regs + (reg * 4));
}
static void reg_write(struct serial_dev *serial, u32 val, unsigned int reg)
{
        iowrite32(val, serial->regs + (reg * 4));
}

static void serial_write_char(struct serial_dev *serial, u32 c)
{
        unsigned long flags;

retry:
        while ((reg_read(serial, UART_LSR) & UART_LSR_THRE) == 0)
                cpu_relax();

        spin_lock_irqsave(&serial->lock, flags);
        if ((reg_read(serial, UART_LSR) & UART_LSR_THRE) == 0) {
                spin_unlock_irqrestore(&serial->lock, flags);
                goto retry;
        }

        reg_write(serial, c, UART_TX);
        spin_unlock_irqrestore(&serial->lock, flags);
}

static ssize_t serial_write_pio(struct file *file, const char __user *buf,
                         size_t sz, loff_t *off)
{
        int i;
        struct miscdevice *miscdev_ptr = file->private_data;
        struct serial_dev *serial = container_of(miscdev_ptr, struct
                                                 serial_dev, miscdev);


        for (i = 0; i < sz; i++) {
                unsigned char c;
                if (get_user(c, buf + i))
                        return -EFAULT;

                serial_write_char(serial, c);
                atomic_inc(&serial->counter);

                if (c == '\n')
                        serial_write_char(serial, '\r');
        }
        *off += sz;
        return sz;
}

static ssize_t serial_read(struct file *file, char __user *buf,
                        size_t sz, loff_t *off)
{
        int ret;
        unsigned char c;
        struct miscdevice *miscdev_ptr;
        struct serial_dev *serial;

        miscdev_ptr = file->private_data;
        serial = container_of(miscdev_ptr, struct serial_dev, miscdev);

retry:
        ret = wait_event_interruptible(serial->wq,
                                       serial->buf_wr != serial->buf_rd);
        if (ret)
                return ret;

        spin_lock(&serial->lock);
        if (serial->buf_wr == serial->buf_rd) {
                spin_unlock(&serial->lock);
                goto retry;
        }
        c = serial->rx_buf[serial->buf_rd++];
        if (serial->buf_rd == SERIAL_BUFSIZE)
                serial->buf_rd = 0;

        spin_unlock(&serial->lock);

        ret = put_user(c, buf);
        if (ret)
                return ret;

        *off += 1;
        return 1;
}

static irqreturn_t serial_irq_handler(int irq, void *arg)
{
        unsigned int c;
        struct serial_dev *serial = arg;
        if (!serial)
            return IRQ_NONE;
        /* prevent preemption from a */
        spin_lock(&serial->lock);

        c = reg_read(serial, UART_TX);
        serial->rx_buf[serial->buf_wr++] = c;
        if (serial->buf_wr == SERIAL_BUFSIZE)
                serial->buf_wr = 0;

        spin_unlock(&serial->lock);
        wake_up(&serial->wq);

        return IRQ_HANDLED;
}

static int serial_probe(struct platform_device *pdev)
{
        struct serial_dev *serial;
        struct resource *res;

        int ret, irq;
        unsigned int baud_divisor, uartclk;

        irq = platform_get_irq(pdev, 0);
        serial = devm_kzalloc(&pdev->dev, sizeof(*serial), GFP_KERNEL);
        if (!serial)
                return -ENOMEM;

        init_waitqueue_head(&serial->wq);
        spin_lock_init(&serial->lock);

        serial->regs = devm_platform_ioremap_resource(pdev, 0);
        if (IS_ERR(serial->regs))
                return PTR_ERR(serial->regs);


        /* retrieves phys address from DT */
        res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
        if (!res)
                return -EINVAL;
        serial->res = res;
        serial->dev = &pdev->dev;

        pm_runtime_enable(&pdev->dev);
        pm_runtime_get_sync(&pdev->dev);

        ret = of_property_read_u32(pdev->dev.of_node, "clock-frequency",
			   &uartclk);
        if (ret) {
                dev_err(&pdev->dev,
                        "clock-frequency property not found in Device Tree\n");
                goto disable_rpm;
        }

        baud_divisor = uartclk / 16 / 115200;
        reg_write(serial, 0x07, UART_OMAP_MDR1);
        reg_write(serial, 0x00, UART_LCR);
        reg_write(serial, UART_LCR_DLAB, UART_LCR);
        reg_write(serial, baud_divisor & 0xff, UART_DLL);
        reg_write(serial, (baud_divisor >> 8) & 0xff, UART_DLM);
        reg_write(serial, UART_LCR_WLEN8, UART_LCR);
        reg_write(serial, 0x00, UART_OMAP_MDR1);

        /* enable interrupts */
        reg_write(serial, UART_IER_RDI, UART_IER);

        /* Clear UART FIFOs */
        reg_write(serial, UART_FCR_CLEAR_RCVR | UART_FCR_CLEAR_XMIT, UART_FCR);

        ret = serial_init_dma(serial);
        if (ret)
                serial_clean_dma(serial);

        misc_register(&serial->miscdev);
        serial->miscdev.minor = MISC_DYNAMIC_MINOR;
        if (ret)
                serial->miscdev.fops = &serial_fops_pio;
        else
                serial->miscdev.fops = &serial_fops_dma;
        serial->miscdev.parent = &pdev->dev;
        serial->miscdev.name = devm_kasprintf(&pdev->dev, GFP_KERNEL,
                                              "serial-%x", res->start);

        ret = devm_request_irq(serial->miscdev.parent, irq, &serial_irq_handler,
                         0, pdev->name, serial);
        if (ret) {
                /* TODO: what??? this is not needed??? */
                // devm_free_irq(serial->miscdev.parent, irq, &serial);
                goto disable_rpm;
        }

        /* so that we can get miscdev and other structs in other parts */
        platform_set_drvdata(pdev, serial);

	pr_info("Called %s\n", __func__);

        return 0;

disable_rpm:
        pm_runtime_disable(&pdev->dev);
        return ret;

}

static int serial_remove(struct platform_device *pdev)
{
        struct serial_dev *serial;

        serial = platform_get_drvdata(pdev);

        pm_runtime_disable(&pdev->dev);
        serial_clean_dma(serial);
        misc_deregister(&serial->miscdev);
	pr_info("Called %s\n", __func__);
        return 0;
}

const struct of_device_id serial_match_table[] = {
        { .compatible = "bootlin,serial" },
        { }
};

MODULE_DEVICE_TABLE(of, serial_match_table);

static struct platform_driver serial_driver = {
        .driver = {
                .name = "serial",
                .owner = THIS_MODULE,
                .of_match_table = serial_match_table
        },
        .probe = serial_probe,
        .remove = serial_remove,
};
module_platform_driver(serial_driver);

MODULE_LICENSE("GPL");
