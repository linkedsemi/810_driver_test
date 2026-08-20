/*
 * Copyright (c) 2025 LinkedSemi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i3c.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(host_i3c_master, LOG_LEVEL_INF);

#define TARGET_ADDR 0x38
#define INIT_LEN    9
#define READ_LEN    50

static const struct device *i3c_dev = DEVICE_DT_GET(DT_NODELABEL(i3c5));

static struct i3c_device_desc *wait_for_target(void)
{
    struct i3c_device_desc *target = NULL;
    int ret;

    for (int attempt = 0; attempt < 1; attempt++) {
        target = i3c_dev_list_i3c_addr_find(i3c_dev, TARGET_ADDR);
        if (target != NULL) {
            return target;
        }
        LOG_INF("Target 0x%02X not found, retry DAA (attempt %d)",
                TARGET_ADDR, attempt + 1);
        ret = i3c_do_daa(i3c_dev);
        if (ret != 0) {
            LOG_INF("i3c_do_daa failed: %d", ret);
        }
        k_sleep(K_MSEC(200));
    }

    return NULL;
}

static int host_send_init_cmd(struct i3c_device_desc *target)
{
    uint8_t init_buf[] = {0x02, 0x06, 0xb8, 0x01, 0x00, 0x4c, 0xa5, 0x00, 0x00,
    0x02, 0x06, 0xb8, 0x01, 0x00, 0x4c, 0xa5, 0x00, 0x00,0x02, 0x06, 0xb8, 0x01, 0x00, 0x4c, 0xa5, 0x00, 0x00
        ,0x02, 0x06, 0xb8, 0x01, 0x00, 0x4c, 0xa5, 0x00, 0x00,
    0x02, 0x06, 0xb8, 0x01, 0x00, 0x4c, 0xa5, 0x00, 0x11};
    struct i3c_msg msg = {
        .buf = init_buf,
        .len = sizeof(init_buf),
        .flags = I3C_MSG_WRITE | I3C_MSG_STOP,
        .hdr_mode = 0,
        .hdr_cmd_code = 0,
        .num_xfer = 0,
    };

    int ret = i3c_transfer(target, &msg, 1);
    if (ret != 0) {
        LOG_ERR("Init command transfer failed: %d", ret);
        return ret;
    }

    LOG_INF("Init command sent, %zu/%d bytes transferred", msg.num_xfer, sizeof(init_buf));
    return 0;
}

static int host_write_read(struct i3c_device_desc *target,
                           const uint8_t *write_buf, size_t num_write,
                           uint8_t *read_buf, size_t num_read,
                           size_t *actual)
{
    struct i3c_msg msg[2] = {
        {
            .buf = (uint8_t *)write_buf,
            .len = num_write,
            .flags = I3C_MSG_WRITE,
            .hdr_mode = 0,
            .hdr_cmd_code = 0,
            .num_xfer = 0,
        },
        {
            .buf = read_buf,
            .len = num_read,
            .flags = I3C_MSG_RESTART | I3C_MSG_READ | I3C_MSG_STOP,
            .hdr_mode = 0,
            .hdr_cmd_code = 0,
            .num_xfer = 0,
        },
    };
    int ret;
     ret = i3c_transfer(target, msg, 2);
    if (ret == 0) {
        *actual = msg[1].num_xfer;
    }

    // ret = i3c_transfer(target, &msg[1], 1);
    // if (ret == 0) {
    //     *actual = msg[1].num_xfer;
    // }
    return ret;
}

static int host_send_i2c_wave(const struct device *dev)
{
    uint8_t dummy = 0x5A;
    struct i2c_msg msg = {
        .buf = &dummy,
        .len = 1,
        .flags = I2C_MSG_WRITE | I2C_MSG_STOP,
    };
    int ret;

    ret = i2c_transfer(dev, &msg, 1, 0x20);
    if (ret == -ENOSYS) {
        LOG_INF("I2C clock not configured, skip I2C waveform test");
    } else if (ret != 0) {
        LOG_INF("I2C waveform sent (addr 0x20), transfer returned %d "
                "(NACK expected if no I2C slave)", ret);
    } else {
        LOG_INF("I2C waveform sent (addr 0x20), ACKed by slave");
    }

    return 0;
}

int main(void)
{
    LOG_INF("[HOST MASTER] I3C host (CPU2/I3C5) started");

    uint8_t read_buf[READ_LEN];
    uint8_t cmd = 0x55;
    struct i3c_device_desc *target = NULL;
    size_t actual;
    int ret;

    /* Wait 1 second before starting communication. */
    k_sleep(K_MSEC(1000));

    target = wait_for_target();
    if (target == NULL) {
        LOG_ERR("Failed to find I3C target at 0x%02X", TARGET_ADDR);
        return -1;
    }
    LOG_INF("Target 0x%02X found", TARGET_ADDR);

    /* Send initial 9-byte command: 02 06 08 01 00 4c a5 00 00 */
    ret = host_send_init_cmd(target);
    if (ret != 0) {
        return ret;
    }

    /* Wait 0.5 second before entering the write+read loop. */
    k_sleep(K_MSEC(500));

    // while (1) {
        memset(read_buf, 0, sizeof(read_buf));
        actual = 0;

        ret = host_write_read(target, &cmd, 1, read_buf, 100, &actual);
        if (ret != 0) {
            LOG_ERR("Write+read failed: %d", ret);
        } else {
            LOG_INF("Write+read OK: wrote 0x%02X, read %zu bytes", cmd, actual);
            LOG_HEXDUMP_INF(read_buf, actual, "Read data");
        }

        k_sleep(K_MSEC(500));
    // }

    /* Send an I2C-style waveform (no slave required). */
    host_send_i2c_wave(i3c_dev);

    return 0;
}
