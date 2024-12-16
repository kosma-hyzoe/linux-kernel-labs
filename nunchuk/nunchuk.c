// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/delay.h>

/* i2c_adapter
 *
 *
 *

/* Add your code here */
#define INIT_BYTES_1	{0xf0, 0x55}
#define INIT_BYTES_2	{0xfb, 0x00}
#define INIT_DELAY	10000


#define DELAY		10000
#define DELAY_MAX	20000

struct nunchuk_dev {
	struct i2c_client *i2c_client;
};

static int _nunchuk_read_regs(struct i2c_client *client, char *rx_buf)
{
	int ret_val;
	int zpressed, cpressed;
	char tx_buf[] = {0x00};

	usleep_range(DELAY, DELAY_MAX);

	ret_val = i2c_master_send(client, tx_buf, sizeof(tx_buf));
	if (ret_val != 1) {
		pr_alert("i2c_master_send: error %d\n", ret_val);
		return ret_val;
	}
	usleep_range(DELAY, DELAY_MAX);

	ret_val = i2c_master_recv(client, rx_buf, 6);
	if (ret_val != 1) {
		pr_alert(" i2c_master_recv: error %d\n", ret_val);
		return ret_val;
	}

	zpressed =
	cpressed =
	return 0;
}

static int _nunchuk_write(struct i2c_cilent *client, char *rx_buf)
{
	return 0;
}


static int nunchuk_probe(struct i2c_client *client)
{
	int ret_val;
	const char tx_buf1[2] = INIT_BYTES_1;
	const char tx_buf2[2] = INIT_BYTES_2;

	ret_val = i2c_master_send(client, tx_buf1, sizeof(tx_buf1));
	if (ret_val != 2) {
		pr_alert("%s: error %d while sending bytes", __func__, ret_val);
		return ret_val;
	}

	udelay(INIT_DELAY);

	ret_val = i2c_master_send(client, tx_buf2, sizeof(tx_buf2));
	if (ret_val != 2) {
		pr_alert("%s: error %d while sending bytes", __func__, ret_val);
		return ret_val;
	}

	 char rx_buf[6];
	_nunchuk_read_regs(client, rx_buf);

	return 0;
}

static void nunchuk_remove(void)
{

}

/* TODO: actually of_device_id??? */
static struct of_device_id nunchuck_match_table[] = {
	{ .compatible = "nintendo,nunchuk" },
	{ }
};

static const struct i2c_device_id nunchuk_device_id[] = {
	{ "nintendo,nunchuk" },
	{ }
};
/* this generates a modalias string when the device is connected */
MODULE_DEVICE_TABLE(i2c, nunchuk_device_id);



struct i2c_driver nunchuk_driver = {
	.probe = nunchuk_probe,
	.id_table = nunchuk_device_id,
	.driver = {

	}
};

module_i2c_driver(nunchuk_driver);
MODULE_AUTHOR("Marcin Kosma Hyżorek");
MODULE_LICENSE("GPL");
