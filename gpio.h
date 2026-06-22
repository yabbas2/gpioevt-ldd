#ifndef GPIO_H
#define GPIO_H

#include <linux/kernel.h>
#include <linux/irq.h>
#include <linux/interrupt.h>

int gpio_init(irqreturn_t (*irq_handler)(int, void*), int *gpios, int nof_gpios);
void gpio_deinit(int *gpios, int nof_gpios);

#endif
