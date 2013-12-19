/*
 * Copyright 2013 Navicron.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/clk.h>
#include <linux/clk/mxs.h>
#include <linux/clkdev.h>
#include <linux/clocksource.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/irqchip/mxs.h>
#include <linux/reboot.h>
#include <linux/micrel_phy.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/phy.h>
#include <linux/pinctrl/consumer.h>
#include <linux/sys_soc.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/time.h>
#include <asm/system_misc.h>

#include "pm.h"

/* (Temporary) HW adaption for Navicron DWAMS */


#ifdef CONFIG_WILINK_PLATFORM_DATA
 #include <linux/wl12xx.h>
 static struct wl12xx_platform_data wl12xx __initdata;
#endif

#define MXS_GPIO_NR(bank, nr)  ((bank) * 32 + (nr))

/* WLAN */
#define WLAN_EN            MXS_GPIO_NR(1, 23) //MXS_PIN_TO_GPIO(PINID_LCD_ENABLE)
#define WLAN_IRQ       MXS_GPIO_NR(1, 21) //MXS_PIN_TO_GPIO(PINID_LCD_CS)
#define WLAN_CLK_EN        MXS_GPIO_NR(1, 22) //MXS_PIN_TO_GPIO(PINID_LCD_DOTCK)
#define WLAN_MUX_EN        MXS_GPIO_NR(2, 27) //MXS_PIN_TO_GPIO(PINID_GPMI_CE1N)
/* BT */
#define RESET_BT       MXS_GPIO_NR(1, 20) //MXS_PIN_TO_GPIO(PINID_LCD_WR)
#define VBUS_EN            MXS_GPIO_NR(1, 15) //MXS_PIN_TO_GPIO(PINID_LCD_D15)
/* AUDIO */
#define HS_DETECT      MXS_GPIO_NR(1, 18) //MXS_PIN_TO_GPIO(PINID_LCD_RESET)
#define MIC_GPIO_EN        MXS_GPIO_NR(1, 12) //MXS_PIN_TO_GPIO(PINID_LCD_D12)



#if CONFIG_WL_TI
void imx23_dwams_wlan_on_off(bool enable)
{
   printk( KERN_INFO "---imx23_dwams_wlan_on_off:%d\n", enable );
   if(enable)
       gpio_set_value(WLAN_EN, 1);
   else
       gpio_set_value(WLAN_EN, 0);
}
static void imx23_dwams_wlan_init(void)
{
   printk( KERN_INFO "---imx23_dwams_wlan_init\n" );
   /* WLAN IRQ */
   gpio_request(WLAN_IRQ, "wlan_irq");
   gpio_direction_input(WLAN_IRQ);

   /* WLAN enable */
   gpio_request(WLAN_EN, "wlan_en");
   gpio_direction_output(WLAN_EN, 0);

   /* WLAN MUX enable */
   gpio_request(WLAN_MUX_EN, "wlan_mux_en");
   gpio_direction_output(WLAN_MUX_EN, 1);

   /* WLAN clk enable */
   gpio_request(WLAN_CLK_EN, "wlan_clk_en");
   gpio_direction_output(WLAN_CLK_EN, 1);

   /* power up chip */
   mdelay(10);
   imx23_dwams_wlan_on_off(1);
}
#else
void imx23_dwams_wlan_on_off(bool enable) {}
void imx23_dwams_wlan_init(void) {}
#endif

#ifdef CONFIG_WILINK_PLATFORM_DATA
static void imx23_dwams_wlan_plat_data_set(void)
{
   int ret;
   
   printk( KERN_INFO "---imx23_dwams_wlan_plat_data_set\n" );
   wl12xx.board_ref_clock = WL12XX_REFCLOCK_26;
   wl12xx.irq = gpio_to_irq(WLAN_IRQ);
// wl12xx.board_tcxo_clock = WL12XX_TCXOCLOCK_26;
// wl12xx.platform_quirks = WL12XX_PLATFORM_QUIRK_EDGE_IRQ;
// wl12xx.set_power = imx23_dwams_wlan_on_off;

   ret = wl12xx_set_platform_data(&wl12xx);
   if (ret) {
       pr_err("error setting wl12xx data: %d\n", ret);
       return;
   }
}
#else
static void __init imx23_dwams_wlan_plat_data_set(void) {}
#endif

static void imx23_dwams_bt_init(void)
{
   printk( KERN_INFO "---imx23_dwams_bt_init\n" );

   /* BT out of reset */
   gpio_request(RESET_BT, "bt_reset");
   gpio_direction_output(RESET_BT, 1);
   gpio_export(RESET_BT, true);

// gpio_request(VBUS_EN, "Vbus_en");
// gpio_direction_output(VBUS_EN, 1);
// gpio_export(VBUS_EN, true);

}

static void imx23_dwams_audio_init(void)
{
   printk( KERN_INFO "---imx23_dwams_audio_init\n" );

   gpio_request(HS_DETECT, "hs_detect");
   gpio_direction_input(HS_DETECT);
   gpio_export(HS_DETECT, true);

   gpio_request(MIC_GPIO_EN, "saif_mic_en");
   gpio_direction_output(MIC_GPIO_EN, 1);
   gpio_export(MIC_GPIO_EN, true);

}

void __init imx23_dwams_init(void)
{
   printk( KERN_INFO "---imx23_dwams_init\n" );
   imx23_dwams_wlan_init();
   imx23_dwams_wlan_plat_data_set();
   imx23_dwams_bt_init();
   imx23_dwams_audio_init();
}
