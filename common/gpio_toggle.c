/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * GPIO Auto Toggle - 公共GPIO自动翻转模块
 *
 * 提供两个独立的GPIO翻转线程，分别控制PA05和PA07引脚。
 * 使用Zephyr的SYS_INIT机制自动初始化，无需手动调用。
 *
 * 使用方法：
 *   1. 在CMakeLists.txt中添加此源文件
 *   2. 在prj.conf中启用CONFIG_GPIO=y
 *   3. 在app.overlay中启用gpio0节点
 *   模块会自动在系统启动时创建翻转线程
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(gpio_toggle, LOG_LEVEL_INF);

/* GPIO设备节点 */
static const struct device *gpio_dev = DEVICE_DT_GET(DT_NODELABEL(gpio0));

/* GPIO引脚配置 */
#define GPIO_PA05_PIN  5
#define GPIO_PA07_PIN  7

/* 线程栈大小 */
#define GPIO_THREAD_STACK_SIZE 512

/*
 * 线程优先级：故意设置不同优先级以增加测试干扰
 * - PA05: 高优先级（8） - 更接近测试线程，增加抢占概率
 * - PA07: 低优先级（12）- 模拟低优先级后台任务
 *
 * 两个GPIO线程以不同优先级运行，可以在示波器上观察：
 * - 高优先级线程翻转更准时，抖动小
 * - 低优先级线程可能被测试线程延迟，抖动大
 * 这有助于验证测试代码在不同负载下的时序稳定性
 */
#define GPIO_PA05_THREAD_PRIORITY 8   /* 较高优先级 */
#define GPIO_PA07_THREAD_PRIORITY 12  /* 较低优先级 */

/* 线程栈 */
K_THREAD_STACK_DEFINE(gpio05_stack, GPIO_THREAD_STACK_SIZE);
K_THREAD_STACK_DEFINE(gpio07_stack, GPIO_THREAD_STACK_SIZE);

/* 线程控制块 */
static struct k_thread gpio05_thread_data;
static struct k_thread gpio07_thread_data;

/* 引脚状态 */
static uint32_t pin05_state = 0;
static uint32_t pin07_state = 0;

/**
 * @brief PA05翻转线程函数
 */
static void gpio05_toggle_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	int ret;

	/* 配置PA05为输出，初始低电平 */
	ret = gpio_pin_configure(gpio_dev, GPIO_PA05_PIN, GPIO_OUTPUT_LOW);
	if (ret < 0) {
		LOG_ERR("Failed to configure PA05: %d", ret);
		return;
	}

	LOG_INF("[GPIO] PA05 toggle thread started (500ms period)");

	while (1) {
		pin05_state = !pin05_state;
		gpio_pin_set(gpio_dev, GPIO_PA05_PIN, pin05_state);
		k_msleep(500);
	}
}

/**
 * @brief PA07翻转线程函数
 */
static void gpio07_toggle_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	int ret;

	/* 配置PA07为输出，初始低电平 */
	ret = gpio_pin_configure(gpio_dev, GPIO_PA07_PIN, GPIO_OUTPUT_LOW);
	if (ret < 0) {
		LOG_ERR("Failed to configure PA07: %d", ret);
		return;
	}

	LOG_INF("[GPIO] PA07 toggle thread started (500ms period)");

	while (1) {
		pin07_state = !pin07_state;
		gpio_pin_set(gpio_dev, GPIO_PA07_PIN, pin07_state);
		k_msleep(500);
	}
}

/**
 * @brief GPIO toggle模块初始化函数
 *
 * 通过SYS_INIT自动在系统启动时调用
 */
static int gpio_toggle_init(void)
{
	/* 检查GPIO设备是否就绪 */
	if (!device_is_ready(gpio_dev)) {
		LOG_ERR("GPIO device not ready");
		return -ENODEV;
	}

	/* 创建PA05翻转线程（高优先级） */
	k_thread_create(&gpio05_thread_data, gpio05_stack,
			K_THREAD_STACK_SIZEOF(gpio05_stack),
			gpio05_toggle_thread, NULL, NULL, NULL,
			GPIO_PA05_THREAD_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&gpio05_thread_data, "gpio_pa05");

	/* 创建PA07翻转线程（低优先级） */
	k_thread_create(&gpio07_thread_data, gpio07_stack,
			K_THREAD_STACK_SIZEOF(gpio07_stack),
			gpio07_toggle_thread, NULL, NULL, NULL,
			GPIO_PA07_THREAD_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&gpio07_thread_data, "gpio_pa07");

	LOG_INF("[GPIO] Auto toggle threads initialized");
	LOG_INF("  - PA05: priority=%d (high), period=500ms", GPIO_PA05_THREAD_PRIORITY);
	LOG_INF("  - PA07: priority=%d (low),  period=500ms", GPIO_PA07_THREAD_PRIORITY);

	return 0;
}

/* 使用SYS_INIT在系统启动时自动初始化，优先级设为APPLICATION级 */
SYS_INIT(gpio_toggle_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
