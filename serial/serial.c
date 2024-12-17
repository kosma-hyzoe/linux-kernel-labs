// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <uapi/linux/serial_reg.h>
#include <linux/pm_runtime.h>
#include <linux/pm_runtime.h>
#include <linux/iomap.h>


/* Add your code here */

struct serial_dev {
        /* TODO: more on iomem */
        void __iomem *regs;
};

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

static int serial_probe(struct platform_device *pdev)
{
        struct serial_dev *serial;
        int ret;
        unsigned int baud_divisor, uartclk;

        serial = devm_kzalloc(&pdev->dev, sizeof(*serial), GFP_KERNEL);
        if (!serial)
                return -ENOMEM;

        serial->regs = devm_platform_ioremap_resource(pdev, 0);
        if (IS_ERR(serial->regs))
                return PTR_ERR(serial->regs);

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
	pr_info("Called %s\n", __func__);
        serial_write_char(serial, 'f');

        return 0;

disable_rpm:
        pm_runtime_disable(&pdev->dev);
        return ret;

}

static int serial_remove(struct platform_device *pdev)
{
        pm_runtime_disable(&pdev->dev);
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
