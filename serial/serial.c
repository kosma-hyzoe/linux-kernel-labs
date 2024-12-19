// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <uapi/linux/serial_reg.h>
#include <linux/pm_runtime.h>
#include <linux/pm_runtime.h>
#include <linux/iomap.h>
#include <linux/miscdevice.h>


/* Add your code here */
static ssize_t serial_write(struct file *f, const char __user *buf,
                         size_t sz, loff_t *off);
static ssize_t serial_read(struct file *f, char __user *buf,
                        size_t sz, loff_t *off);

struct file_operations serial_fops = {
        .write = serial_write,
        .read = serial_read
};

struct serial_dev {
        /* TODO: more on iomem */
        void __iomem *regs;
        struct miscdevice miscdev;
};

struct serial_fops;


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
        while ((reg_read(serial, UART_LSR) & UART_LSR_THRE) == 0)
                cpu_relax();
        reg_write(serial, c, UART_TX);
}

static ssize_t serial_write(struct file *f, const char __user *buf,
                         size_t sz, loff_t *off)
{
        return -EINVAL;
}

static ssize_t serial_read(struct file *f, char __user *buf,
                        size_t sz, loff_t *off)
{
        return 0;
}

static int serial_probe(struct platform_device *pdev)
{
        struct serial_dev *serial;
        struct resource *res;

        int ret;
        unsigned int baud_divisor, uartclk;

        serial = devm_kzalloc(&pdev->dev, sizeof(*serial), GFP_KERNEL);
        if (!serial)
                return -ENOMEM;

        serial->regs = devm_platform_ioremap_resource(pdev, 0);
        if (IS_ERR(serial->regs))
                return PTR_ERR(serial->regs);

        /* TODO: should it be later??? */
        res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
        if (!res)
                return -EINVAL;

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

        /* Clear UART FIFOs */
        reg_write(serial, UART_FCR_CLEAR_RCVR | UART_FCR_CLEAR_XMIT, UART_FCR);

        serial->miscdev.minor = MISC_DYNAMIC_MINOR;
        serial->miscdev.fops = &serial_fops;
        serial->miscdev.parent = &pdev->dev;
        serial->miscdev.name = devm_kasprintf(&pdev->dev, GFP_KERNEL,
                                              "serial-%x", res->start);
        misc_register(&serial->miscdev);
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
        misc_deregister(pdev-
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
