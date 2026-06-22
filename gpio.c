#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/slab.h>

#include "gpio.h"

static int *irq_numbers = NULL;
static int nof_gpios_requested = 0;
static ulong irq_edge = IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING;

int gpio_init(irqreturn_t (*irq_handler)(int, void *), int *gpios, int nof_gpios) {
    int ret = 0;

    irq_numbers = kmalloc(nof_gpios * sizeof(int), GFP_KERNEL);
    if (!irq_numbers) {
        pr_err("gpioevt: failed to allocate irq_numbers\n");
        ret = -ENOMEM;
        goto end;
    }
    // initialize irq tracking
    for (int i = 0; i < nof_gpios; i++) {
        irq_numbers[i] = -ENXIO;
    }

    // request gpios and register irqs
    for (int i = 0; i < nof_gpios; i++) {
        ret = gpio_request(gpios[i], "gpioevt");
        if (ret < 0) {
            pr_err("gpioevt: failed to request GPIO %d\n", gpios[i]);
            goto err;
        }
        nof_gpios_requested++;

        struct gpio_desc *desc = gpio_to_desc(gpios[i]);
        if (!desc) {
            pr_err("gpioevt: invalid GPIO %d\n", gpios[i]);
            ret = -EINVAL;
            goto err;
        }
        int dir = gpiod_get_direction(desc);
        if (dir < 0) {
            pr_err("gpioevt: failed to get direction for GPIO %d\n", gpios[i]);
            ret = dir;
            goto err;
        }
        if (dir != GPIOD_IN) {
            pr_err("gpioevt: GPIO %d is not configured as input\n", gpios[i]);
            ret = -EPERM;
            goto err;
        }

        int irqno = gpio_to_irq(gpios[i]);
        if (irqno < 0) {
            pr_err("gpioevt: failed to get IRQ for GPIO %d\n", gpios[i]);
            ret = irqno;
            goto err;
        }

        ret = request_irq(irqno, irq_handler, irq_edge, "gpioevt", (void *)(uintptr_t)gpios[i]);
        if (ret < 0) {
            pr_err("gpioevt: failed to request IRQ %d for GPIO %d\n", irqno, gpios[i]);
            goto err;
        }
        irq_numbers[i] = irqno;
    }

    goto end;

err:
    gpio_deinit(gpios, nof_gpios);
end:
    return ret;
}

void gpio_deinit(int *gpios, int nof_gpios) {
    for (int i = 0; i < nof_gpios_requested; i++) {
        if (irq_numbers[i] != -ENXIO) {
            free_irq(irq_numbers[i], (void *)(uintptr_t)gpios[i]);
        }
        gpio_free(gpios[i]);
    }
    if (irq_numbers != NULL) {
        kfree(irq_numbers);
        irq_numbers = NULL;
    }
    nof_gpios_requested = 0;
}
