/*
 * Copyright 2011 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2011 Linaro Ltd.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>

#include "common.h"
#include "hardware.h"
#include "mach-mx53sigbox.h"

static void setup_iomux_usbhub(void)
{
    /* request USB HUB power enable pin, GPIO4_11 */
        gpio_direction_output(IMX_GPIO_NR(4, 11), 1);
    /* N_RESET -> first put the hub in reset state, we need clock and configuration first */
        gpio_direction_output(IMX_GPIO_NR(6, 18), 0);
    /* CFG_SEL_0 -> set the HUB to standlone mode, no i2c control, default config, self powered */
        gpio_direction_output(IMX_GPIO_NR(4, 12), 0);
    /* CFG_SEL_1 -> set the HUB to standlone mode, no i2c control */
        gpio_direction_output(IMX_GPIO_NR(6, 17), 0);
    /* Clock -> CCM SSI EXT2 24Mhz */
        msleep(100);
    /* N_RESET -> let's release the reset ROCK ON!*/
        gpio_direction_output(IMX_GPIO_NR(6, 18), 1);
}


void __init imx53_sigbox_init(void)
{
    pr_info("\n\n***Sigbox custom init reached.***\n");
    gpio_request(IMX_GPIO_NR(5, 20),"wifi-pwd");
    gpio_direction_output(IMX_GPIO_NR(5, 20), 1); // enable wifipwd
    gpio_request(IMX_GPIO_NR(6, 15),"wifi-vdd");
    gpio_direction_output(IMX_GPIO_NR(6, 15), 1); // enable wifivdd
    gpio_request(IMX_GPIO_NR(3, 31),"wifi-wow");
    gpio_direction_input(IMX_GPIO_NR(3, 31));     // enable wlan-wow
    gpio_request(IMX_GPIO_NR(3, 28),"bt-wake");
    gpio_direction_output(IMX_GPIO_NR(3, 28), 1); // enable btwake
    gpio_request(IMX_GPIO_NR(3, 29),"bt-disable");
    gpio_direction_output(IMX_GPIO_NR(3, 29), 0); // enable bt
    gpio_request(IMX_GPIO_NR(3, 26),"bt-pwd");
    gpio_direction_output(IMX_GPIO_NR(3, 26), 1); // enable btpwd
    gpio_request(IMX_GPIO_NR(3, 27),"bt-gpio");
    gpio_direction_input(IMX_GPIO_NR(3, 27));     // enable btgpio
    gpio_request(IMX_GPIO_NR(3, 30),"bt-host-wakeup");
    gpio_direction_input(IMX_GPIO_NR(3, 30));     // enable bt-host-wakeup

/*    pr_info("Calling USB HUB config...");
    setup_iomux_usbhub();
    pr_info("USB HUB config done.\n");
    pr_info("Waiting a bit...\n");
*/
    msleep(200);
    pr_info("***Sigbox custom init left.***\n\n\n");
}
