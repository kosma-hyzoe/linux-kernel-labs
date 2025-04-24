// SPDX-License-Identifier: GPL-2.0
#include "linux/container_of.h"
#include <linux/init.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/delay.h>

/* Add your code here */
#define INIT_BYTES_1	{0xf0, 0x55}
#define INIT_BYTES_2	{0xfb, 0x00}
#define INIT_DELAY	1000


#define DELAY		10000
#define DELAY_MAX	20000

struct nunchuk_dev {
	struct i2c_client *i2c_client;
};

static int _nunchuk_read_regs(struct i2c_client *client, char *rx_buf)
{
	int ret;
	char tx_buf[] = {0x00};

	usleep_range(DELAY, DELAY_MAX);

	ret = i2c_master_send(client, tx_buf, sizeof(tx_buf));
	if (ret != 1) {
		pr_alert("i2c_master_send: error %d\n", ret);
		return ret;
	}
	usleep_range(DELAY, DELAY_MAX);

	ret = i2c_master_recv(client, rx_buf, 6);
	if (ret != 6) {
		pr_alert(" i2c_master_recv: error %d\n", ret);
		return ret;
	}
	return 0;
}

void nunchuk_poll(struct input_dev *input)
{
	int zpressed= 0, cpressed = 0;
	struct nunchuk_dev *nunchuk = input_get_drvdata(input);
	struct i2c_client *client = nunchuk->i2c_client;
	u8 recv[6];


	if (_nunchuk_read_regs(client, recv) < 0)
		return;

	zpressed = (recv[5] & BIT(0)) ? 0 : 1;
	cpressed = (recv[5] & BIT(1)) ? 0 : 1;

	input_report_key(input, BTN_Z, zpressed);
	input_report_key(input, BTN_C, cpressed);
	input_sync(input);
}

static int nunchuk_probe(struct i2c_client *client)
{
	int ret;
	const char tx_buf1[2] = INIT_BYTES_1;
	const char tx_buf2[2] = INIT_BYTES_2;
	struct input_dev *input;
	struct nunchuk_dev *nunchuk;
	u8 recv[6];

	nunchuk = devm_kzalloc(&client->dev, sizeof(*nunchuk), GFP_KERNEL);
	if (!nunchuk)
		return -ENOMEM;

	// pointer magic :p
	nunchuk->i2c_client = client;

	input = devm_input_allocate_device(&client->dev);
	if (!input)
		return -ENOMEM;
	input_set_drvdata(input, nunchuk);

	input->name = "Wii Nunchuk";
	input->id.bustype = BUS_I2C;
	set_bit(EV_KEY, input->evbit);
	set_bit(BTN_C, input->keybit);
	set_bit(BTN_Z, input->keybit);
	input_setup_polling(input, nunchuk_poll);
	input_set_poll_interval(input, 50);
	// NOTE: call last
	ret = input_register_device(input);
	if (ret) {
		dev_err(&client->dev, "failed to register input device (%d)\n",
			ret);
		return ret;
	}

	// initialize the device
	ret = i2c_master_send(client, tx_buf1, sizeof(tx_buf1));
	if (ret < 0) {
		pr_alert("%s: error %d while sending bytes", __func__, ret);
		return ret;
	}

	udelay(INIT_DELAY);

	ret = i2c_master_send(client, tx_buf2, sizeof(tx_buf2));
	if (ret != 2) {
		pr_alert("%s: error %d while sending bytes", __func__, ret);
		return ret;
	}

	/* dummy read to init the device */
	ret = _nunchuk_read_regs(client, recv);
	if (ret < 0)
		return ret;

	ret = _nunchuk_read_regs(client, recv);
	if (ret < 0)
		return ret;

	return 0;
}

static void nunchuk_remove(struct i2c_client *client)
{
	return;
}

static const struct of_device_id nunchuk_dt_match[] = {
	{ .compatible = "nintendo,nunchuk" },
	{ },
};
// creates an alias for loading the module if specified in DT
MODULE_DEVICE_TABLE(of, nunchuk_dt_match);

static struct i2c_driver nunchuk_driver = {
	.driver = {
		.name = "nunchuk",
		.of_match_table = nunchuk_dt_match,
	},
	.probe = nunchuk_probe,
	.remove = nunchuk_remove
};


module_i2c_driver(nunchuk_driver);
MODULE_AUTHOR("Marcin Kosma Hyżorek");
MODULE_LICENSE("GPL");
