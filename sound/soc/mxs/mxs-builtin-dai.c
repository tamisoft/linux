/*
 * mxs-builtin-dai.c -- i.MX233 built-in codec ALSA Soc Audio driver
 * 
 * Author: Michal Ulianko <michal.ulianko@gmail.com>
 * 
 * Based on sound/soc/mxs/mxs-adc.c for kernel 2.6.35
 * by Vladislav Buzov <vbuzov@embeddedalley.com>
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include <linux/workqueue.h>

#include "../codecs/mxs-builtin-codec.h"
#include "mxs-builtin-pcm.h"

#define ADC_VOLUME_MIN  0x37


#ifndef BF
#define BF(value, field) (((value) << BP_##field) & BM_##field)
#endif

/* TODO Delete this and use BM_RTC_PERSISTENT0_RELEASE_GND from header file
 * if it works. */
#define BP_RTC_PERSISTENT0_SPARE_ANALOG	18
#define BM_RTC_PERSISTENT0_SPARE_ANALOG	0xFFFC0000
#define BM_RTC_PERSISTENT0_RELEASE_GND BF(0x2, RTC_PERSISTENT0_SPARE_ANALOG)

//#define ENABLE_HS_DETECT

/* TODO Use codec IO function soc snd write etc, instead of __writel __readl */

struct mxs_adc_priv {
#ifdef ENABLE_HS_DETECT
	struct delayed_work hs_det_work;
	int hp_short_irq;
#endif
	struct delayed_work adc_ramp_work;
	struct delayed_work dac_ramp_work;
	struct workqueue_struct *adc_ramp_queue;
	struct workqueue_struct *dac_ramp_queue;
	int dma_adc_err_irq;
	int dma_dac_err_irq;
	void __iomem *audioin_base;
	void __iomem *audioout_base;
	void __iomem *rtc_base;
	int play_state;
	int cap_state;
};

enum {
		MXS_ADC_PLAY_STATE_RUNNIG,
		MXS_ADC_CAP_STATE_RUN,
		MXS_ADC_PLAY_STATE_STOPPED,
		MXS_ADC_CAP_STATE_STOPPED
};	

static bool adc_ramp_done = 1;
static bool dac_ramp_done = 1;

#ifdef ENABLE_HS_DETECT
static inline void mxs_adc_schedule_work(struct delayed_work *work)
{
	schedule_delayed_work(work, HZ / 10);
}

static void mxs_adc_hs_det_work(struct work_struct *work)
{
	struct mxs_adc_priv *mxs_adc;

	mxs_adc = container_of(work, struct mxs_adc_priv, hs_det_work.work);

	/* disable irq */
	disable_irq(mxs_adc->hp_short_irq);

	while (true) {
		__raw_writel(BM_AUDIOOUT_PWRDN_HEADPHONE,
		      mxs_adc->audioout_base + HW_AUDIOOUT_PWRDN_CLR);
		msleep(10);
		if ((__raw_readl(mxs_adc->audioout_base + HW_AUDIOOUT_ANACTRL)
			& BM_AUDIOOUT_ANACTRL_SHORT_LR_STS) != 0) {
			/* rearm the short protection */
			__raw_writel(BM_AUDIOOUT_ANACTRL_SHORTMODE_LR,
				mxs_adc->audioout_base + HW_AUDIOOUT_ANACTRL_CLR);
			__raw_writel(BM_AUDIOOUT_ANACTRL_SHORT_LR_STS,
				mxs_adc->audioout_base + HW_AUDIOOUT_ANACTRL_CLR);
			__raw_writel(BF_AUDIOOUT_ANACTRL_SHORTMODE_LR(0x1),
				mxs_adc->audioout_base + HW_AUDIOOUT_ANACTRL_SET);

			__raw_writel(BM_AUDIOOUT_PWRDN_HEADPHONE,
				mxs_adc->audioout_base + HW_AUDIOOUT_PWRDN_SET);
			printk(KERN_WARNING "WARNING : Headphone LR short!\r\n");
		} else {
			printk(KERN_WARNING "INFO : Headphone LR no longer short!\r\n");
			break;
		}
		msleep(1000);
	}

	/* power up the HEADPHONE and un-mute the HPVOL */
	__raw_writel(BM_AUDIOOUT_HPVOL_MUTE,
	      mxs_adc->audioout_base + HW_AUDIOOUT_HPVOL_CLR);
	__raw_writel(BM_AUDIOOUT_PWRDN_HEADPHONE,
		      mxs_adc->audioout_base + HW_AUDIOOUT_PWRDN_CLR);

	/* enable irq for next short detect*/
	enable_irq(mxs_adc->hp_short_irq);
}
#endif

static inline void mxs_adc_queue_ramp_work(struct workqueue_struct *queue, struct delayed_work *work)
{
	queue_delayed_work(queue, work, msecs_to_jiffies(2));
	adc_ramp_done = 0;
}

static void mxs_adc_ramp_work(struct work_struct *work)
{
	struct mxs_adc_priv *mxs_adc;
	u32 reg = 0;
	u32 reg1 = 0;
	u32 reg2 = 0;
	u32 l, r;
	u32 ll, rr;
	int i;

	mxs_adc = container_of(work, struct mxs_adc_priv, adc_ramp_work.work);

	reg = __raw_readl(mxs_adc->audioin_base + \
		HW_AUDIOIN_ADCVOLUME);

	reg1 = reg & ~BM_AUDIOIN_ADCVOLUME_VOLUME_LEFT;
	reg1 = reg1 & ~BM_AUDIOIN_ADCVOLUME_VOLUME_RIGHT;
	/* minimize adc volume */
	reg2 = reg1 |
	    BF_AUDIOIN_ADCVOLUME_VOLUME_LEFT(ADC_VOLUME_MIN) |
	    BF_AUDIOIN_ADCVOLUME_VOLUME_RIGHT(ADC_VOLUME_MIN);
	__raw_writel(reg2,
		mxs_adc->audioin_base + HW_AUDIOIN_ADCVOLUME);
	mdelay(1);

	l = (reg & BM_AUDIOIN_ADCVOLUME_VOLUME_LEFT) >>
		BP_AUDIOIN_ADCVOLUME_VOLUME_LEFT;
	r = (reg & BM_AUDIOIN_ADCVOLUME_VOLUME_RIGHT) >>
		BP_AUDIOIN_ADCVOLUME_VOLUME_RIGHT;

	/* fade in adc vol */
	for (i = ADC_VOLUME_MIN; (i < l) || (i < r);) {
		i += 0x8;
		ll = i < l ? i : l;
		rr = i < r ? i : r;
		reg2 = reg1 |
		    BF_AUDIOIN_ADCVOLUME_VOLUME_LEFT(ll) |
		    BF_AUDIOIN_ADCVOLUME_VOLUME_RIGHT(rr);
		__raw_writel(reg2,
		    mxs_adc->audioin_base + HW_AUDIOIN_ADCVOLUME);
		mdelay(1);
	}
	adc_ramp_done = 1;
}

static inline void mxs_dac_queue_ramp_work(struct workqueue_struct *queue, struct delayed_work *work)
{
	queue_delayed_work(queue, work, msecs_to_jiffies(2));
	dac_ramp_done = 0;
}

static void mxs_dac_ramp_work(struct work_struct *work)
{
	struct mxs_adc_priv *mxs_adc;
	u32 reg = 0;
	u32 reg1 = 0;
	u32 l, r;
	u32 ll, rr;
	int i;

	mxs_adc = container_of(work, struct mxs_adc_priv, dac_ramp_work.work);

	/* unmute hp and speaker */
	__raw_writel(BM_AUDIOOUT_HPVOL_MUTE,
		mxs_adc->audioout_base + HW_AUDIOOUT_HPVOL_CLR);

	reg = __raw_readl(mxs_adc->audioout_base + \
			HW_AUDIOOUT_HPVOL);

	reg1 = reg & ~BM_AUDIOOUT_HPVOL_VOL_LEFT;
	reg1 = reg1 & ~BM_AUDIOOUT_HPVOL_VOL_RIGHT;

	l = (reg & BM_AUDIOOUT_HPVOL_VOL_LEFT) >>
		BP_AUDIOOUT_HPVOL_VOL_LEFT;
	r = (reg & BM_AUDIOOUT_HPVOL_VOL_RIGHT) >>
		BP_AUDIOOUT_HPVOL_VOL_RIGHT;
	/* fade in hp vol */

	for (i = 0x7f; i > 0 ;) {
		i -= 0x8;
		ll = i > (int)l ? i : l;
		rr = i > (int)r ? i : r;
		reg = reg1 | BF_AUDIOOUT_HPVOL_VOL_LEFT(ll)
			| BF_AUDIOOUT_HPVOL_VOL_RIGHT(rr);
		__raw_writel(reg,
			mxs_adc->audioout_base + HW_AUDIOOUT_HPVOL);
		mdelay(1);
	}

	dac_ramp_done = 1;
}

/* IRQs */
#ifdef ENABLE_HS_DETECT
static irqreturn_t mxs_short_irq(int irq, void *dev_id)
{
	struct mxs_adc_priv *mxs_adc = (struct mxs_adc_priv *)dev_id;
	
	__raw_writel(BM_AUDIOOUT_ANACTRL_SHORTMODE_LR,
		mxs_adc->audioout_base + HW_AUDIOOUT_ANACTRL_CLR);
	__raw_writel(BM_AUDIOOUT_ANACTRL_SHORT_LR_STS,
		mxs_adc->audioout_base + HW_AUDIOOUT_ANACTRL_CLR);
	__raw_writel(BF_AUDIOOUT_ANACTRL_SHORTMODE_LR(0x1),
		mxs_adc->audioout_base + HW_AUDIOOUT_ANACTRL_SET);

	__raw_writel(BM_AUDIOOUT_HPVOL_MUTE,
	      mxs_adc->audioout_base + HW_AUDIOOUT_HPVOL_SET);
	__raw_writel(BM_AUDIOOUT_PWRDN_HEADPHONE,
		      mxs_adc->audioout_base + HW_AUDIOOUT_PWRDN_SET);
	__raw_writel(BM_AUDIOOUT_ANACTRL_HP_CLASSAB,
		mxs_adc->audioout_base + HW_AUDIOOUT_ANACTRL_SET);

	mxs_adc_schedule_work(&mxs_adc->hs_det_work);
	return IRQ_HANDLED;
}
#endif

static irqreturn_t mxs_dac_err_irq(int irq, void *dev_id)
{
	struct mxs_adc_priv *mxs_adc = (struct mxs_adc_priv *)dev_id;
	u32 ctrl_reg;

	ctrl_reg = __raw_readl(mxs_adc->audioout_base + HW_AUDIOOUT_CTRL);

	if (ctrl_reg & BM_AUDIOOUT_CTRL_FIFO_UNDERFLOW_IRQ) {
		//printk(KERN_INFO "DAC underflow detected\n" );

		__raw_writel(BM_AUDIOOUT_CTRL_FIFO_UNDERFLOW_IRQ,
				mxs_adc->audioout_base + HW_AUDIOOUT_CTRL_CLR);

	} else if (ctrl_reg & BM_AUDIOOUT_CTRL_FIFO_OVERFLOW_IRQ) {
		//printk(KERN_INFO "DAC overflow detected\n" );

		__raw_writel(BM_AUDIOOUT_CTRL_FIFO_OVERFLOW_IRQ,
				mxs_adc->audioout_base + HW_AUDIOOUT_CTRL_CLR);
	} else {
		printk(KERN_WARNING "Unknown DAC error interrupt\n");
	}

	return IRQ_HANDLED;
}

static irqreturn_t mxs_adc_err_irq(int irq, void *dev_id)
{
	struct mxs_adc_priv *mxs_adc = (struct mxs_adc_priv *)dev_id;
	u32 ctrl_reg;

	ctrl_reg = __raw_readl(mxs_adc->audioin_base + HW_AUDIOIN_CTRL);

	if (ctrl_reg & BM_AUDIOIN_CTRL_FIFO_UNDERFLOW_IRQ) {
		//printk(KERN_INFO "ADC underflow detected\n" );

		__raw_writel(BM_AUDIOIN_CTRL_FIFO_UNDERFLOW_IRQ,
				mxs_adc->audioin_base + HW_AUDIOIN_CTRL_CLR);

	} else if (ctrl_reg & BM_AUDIOIN_CTRL_FIFO_OVERFLOW_IRQ) {
		//printk(KERN_INFO "ADC overflow detected\n" );

		__raw_writel(BM_AUDIOIN_CTRL_FIFO_OVERFLOW_IRQ,
				mxs_adc->audioin_base + HW_AUDIOIN_CTRL_CLR);
	} else {
		printk(KERN_WARNING "Unknown ADC error interrupt\n");
	}
	
	return IRQ_HANDLED;
}
/* END IRQs */

static int mxs_trigger(struct snd_pcm_substream *substream,
				int cmd,
				struct snd_soc_dai *cpu_dai)
{
	struct mxs_adc_priv *mxs_adc = snd_soc_dai_get_drvdata(cpu_dai);
	int playback = substream->stream == SNDRV_PCM_STREAM_PLAYBACK ? 1 : 0;
	int ret = 0;

	switch (cmd) {

		case SNDRV_PCM_TRIGGER_RESUME:
		case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
			if (playback) {
				
				__raw_writel(BM_AUDIOOUT_HPVOL_MUTE,
						mxs_adc->audioout_base + HW_AUDIOOUT_HPVOL_CLR);
				__raw_writel(0x0, mxs_adc->audioout_base + HW_AUDIOOUT_DATA);
				__raw_writel(BM_AUDIOOUT_CTRL_FIFO_ERROR_IRQ_EN,
						mxs_adc->audioout_base + HW_AUDIOOUT_CTRL_SET);

			} else {

				__raw_writel(BM_AUDIOIN_CTRL_FIFO_ERROR_IRQ_EN,
						mxs_adc->audioin_base + HW_AUDIOIN_CTRL_SET);
				__raw_writel(BM_AUDIOIN_CTRL_RUN,
						mxs_adc->audioin_base + HW_AUDIOIN_CTRL_SET);
						
			}
		break;

		case SNDRV_PCM_TRIGGER_START:
		
			if (playback) {

				if (mxs_adc->play_state == MXS_ADC_PLAY_STATE_RUNNIG) {
					return 0;
				}

				__raw_writel(0x0, mxs_adc->audioout_base + HW_AUDIOOUT_DATA);
				__raw_writel(BM_AUDIOOUT_CTRL_FIFO_ERROR_IRQ_EN,
						mxs_adc->audioout_base + HW_AUDIOOUT_CTRL_SET);

				mxs_dac_queue_ramp_work(mxs_adc->dac_ramp_queue, 
						&mxs_adc->dac_ramp_work);

				/* DAC hangs somethimes...these seems helping a little bit...
				 * should be check if this has affects to other issues...*/
				 __raw_writel(BM_AUDIOOUT_CTRL_RUN,
						mxs_adc->audioout_base + HW_AUDIOOUT_CTRL_SET);
				__raw_writel(BM_AUDIOOUT_HPVOL_SELECT,
						mxs_adc->audioout_base + HW_AUDIOOUT_HPVOL_CLR);

				mxs_adc->play_state = MXS_ADC_PLAY_STATE_RUNNIG;
						
			} else {

				if (mxs_adc->cap_state == MXS_ADC_CAP_STATE_RUN) {
					return 0;
				}

				__raw_writel(BM_AUDIOIN_CTRL_FIFO_ERROR_IRQ_EN,
						mxs_adc->audioin_base + HW_AUDIOIN_CTRL_SET);
				__raw_writel(BM_AUDIOIN_CTRL_RUN,
						mxs_adc->audioin_base + HW_AUDIOIN_CTRL_SET);
				udelay(100);
				mxs_adc_queue_ramp_work(mxs_adc->adc_ramp_queue, 
						&mxs_adc->adc_ramp_work);

				if (mxs_adc->play_state == MXS_ADC_PLAY_STATE_STOPPED) {
					__raw_writel(BM_AUDIOOUT_HPVOL_SELECT,
							mxs_adc->audioout_base + HW_AUDIOOUT_HPVOL_SET);
				}
				
				mxs_adc->cap_state = MXS_ADC_CAP_STATE_RUN;
			}
			
			/* Make sure headphone is UP...DAPM put it down, somethimes ? */
			__raw_writel(BM_AUDIOOUT_ANACTRL_HP_HOLD_GND,
					mxs_adc->audioout_base + HW_AUDIOOUT_ANACTRL_SET);
			__raw_writel(BM_RTC_PERSISTENT0_RELEASE_GND,
					mxs_adc->rtc_base + HW_RTC_PERSISTENT0_SET);
			mdelay(1);
			__raw_writel(BM_AUDIOOUT_PWRDN_HEADPHONE,
					mxs_adc->audioout_base + HW_AUDIOOUT_PWRDN_CLR);
			mdelay(1);
			__raw_writel(BM_AUDIOOUT_ANACTRL_HP_HOLD_GND,
					mxs_adc->audioout_base + HW_AUDIOOUT_ANACTRL_CLR);

		break;

		case SNDRV_PCM_TRIGGER_SUSPEND:
		case SNDRV_PCM_TRIGGER_PAUSE_PUSH:

			if (playback) {
				__raw_writel(BM_AUDIOOUT_HPVOL_MUTE,
						mxs_adc->audioout_base + HW_AUDIOOUT_HPVOL_SET);
				__raw_writel(BM_AUDIOOUT_CTRL_FIFO_ERROR_IRQ_EN,
						mxs_adc->audioout_base + HW_AUDIOOUT_CTRL_CLR);
			} else {
				__raw_writel(BM_AUDIOIN_CTRL_FIFO_ERROR_IRQ_EN,
						mxs_adc->audioin_base + HW_AUDIOIN_CTRL_CLR);
				__raw_writel(BM_AUDIOIN_CTRL_RUN,
						mxs_adc->audioin_base + HW_AUDIOIN_CTRL_CLR);
			}

		break;

		case SNDRV_PCM_TRIGGER_STOP:

			if (playback) {

				if (mxs_adc->cap_state == MXS_ADC_PLAY_STATE_STOPPED) {
					return 0;
				}

				/* DAC hangs somethimes...this seems helping a little bit... */
				__raw_writel(BM_AUDIOOUT_CTRL_RUN,
						mxs_adc->audioout_base + HW_AUDIOOUT_CTRL_CLR);
				__raw_writel(BM_AUDIOOUT_CTRL_FIFO_ERROR_IRQ_EN,
						mxs_adc->audioout_base + HW_AUDIOOUT_CTRL_CLR);
				__raw_writel(BM_AUDIOOUT_HPVOL_MUTE,
						mxs_adc->audioout_base + HW_AUDIOOUT_HPVOL_SET);

				if (dac_ramp_done == 0) {
					cancel_delayed_work(&mxs_adc->dac_ramp_work);
					dac_ramp_done = 1;
				}

				/* DAPM not work ??? */
				if (mxs_adc->cap_state == MXS_ADC_CAP_STATE_STOPPED) {
					__raw_writel(BM_AUDIOOUT_PWRDN_HEADPHONE,
							mxs_adc->audioout_base + HW_AUDIOOUT_PWRDN_SET);
					//__raw_writel(BM_RTC_PERSISTENT0_RELEASE_GND,
					//		mxs_adc->rtc_base + HW_RTC_PERSISTENT0_CLR);
				}
				
				mxs_adc->play_state = MXS_ADC_PLAY_STATE_STOPPED;
				
				/* why this delay */
				mdelay(50);

			} else {

				if (mxs_adc->cap_state == MXS_ADC_CAP_STATE_STOPPED) {
					return 0;
				}
				
				__raw_writel(BM_AUDIOIN_CTRL_FIFO_ERROR_IRQ_EN,
						mxs_adc->audioin_base + HW_AUDIOIN_CTRL_CLR);

				if (adc_ramp_done == 0) {
					cancel_delayed_work(&mxs_adc->adc_ramp_work);
					adc_ramp_done = 1;
				}

				__raw_writel(BM_AUDIOIN_CTRL_RUN,	
						mxs_adc->audioin_base + HW_AUDIOIN_CTRL_CLR);

				/* DAPM not work ??? */
				if (mxs_adc->play_state == MXS_ADC_PLAY_STATE_STOPPED) {
					__raw_writel(BM_AUDIOOUT_PWRDN_HEADPHONE,
							mxs_adc->audioout_base + HW_AUDIOOUT_PWRDN_SET);
					//__raw_writel(BM_RTC_PERSISTENT0_RELEASE_GND,
					//		mxs_adc->rtc_base + HW_RTC_PERSISTENT0_CLR);
				}

				mxs_adc->cap_state = MXS_ADC_CAP_STATE_STOPPED;
			}
			break;
		
		default:
			printk(KERN_ERR "DAC/ADC TRIGGER ERROR\n");
			ret = -EINVAL;
		break;
	}

	return ret;
}

static int mxs_startup(struct snd_pcm_substream *substream,
				struct snd_soc_dai *cpu_dai)
{
	struct mxs_adc_priv *mxs_adc = snd_soc_dai_get_drvdata(cpu_dai);
	int playback = substream->stream == SNDRV_PCM_STREAM_PLAYBACK ? 1 : 0;

	/* clear error interrupts */
	if (playback) {
		__raw_writel(BM_AUDIOOUT_CTRL_FIFO_OVERFLOW_IRQ,
				mxs_adc->audioout_base + HW_AUDIOOUT_CTRL_CLR);
		__raw_writel(BM_AUDIOOUT_CTRL_FIFO_UNDERFLOW_IRQ,
				mxs_adc->audioout_base + HW_AUDIOOUT_CTRL_CLR);
	} else {
		__raw_writel(BM_AUDIOIN_CTRL_FIFO_OVERFLOW_IRQ,
				mxs_adc->audioin_base + HW_AUDIOIN_CTRL_CLR);
		__raw_writel(BM_AUDIOIN_CTRL_FIFO_UNDERFLOW_IRQ,
				mxs_adc->audioin_base + HW_AUDIOIN_CTRL_CLR);
	}

	return 0;
}

static void mxs_shutdown(struct snd_pcm_substream *substream,
				struct snd_soc_dai *cpu_dai)
{
	struct mxs_adc_priv *mxs_adc = snd_soc_dai_get_drvdata(cpu_dai);
	int playback = substream->stream == SNDRV_PCM_STREAM_PLAYBACK ? 1 : 0;

	/* Disable error interrupt */
	if (playback) {
		__raw_writel(BM_AUDIOOUT_CTRL_FIFO_ERROR_IRQ_EN,
			mxs_adc->audioout_base + HW_AUDIOOUT_CTRL_CLR);
	} else {
		__raw_writel(BM_AUDIOIN_CTRL_FIFO_ERROR_IRQ_EN,
			mxs_adc->audioin_base + HW_AUDIOIN_CTRL_CLR);
	}
}

#define MXS_ADC_RATES	SNDRV_PCM_RATE_8000_192000
#define MXS_ADC_FORMATS	(SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S32_LE)

static const struct snd_soc_dai_ops mxs_adc_dai_ops = {
	.startup = mxs_startup,
	.trigger = mxs_trigger,
	.shutdown = mxs_shutdown,
};

static int mxs_dai_probe(struct snd_soc_dai *dai)
{
	// TODO This does not make any sense.
	struct mxs_adc_priv *mxs_adc = dev_get_drvdata(dai->dev);

	snd_soc_dai_set_drvdata(dai, mxs_adc);

	return 0;
}

static struct snd_soc_dai_driver mxs_adc_dai = {
	.name = "mxs-builtin-cpu-dai",
	.probe = mxs_dai_probe,
	.playback = {
		.channels_min = 2,
		.channels_max = 2,
		.rates = MXS_ADC_RATES,
		.formats = MXS_ADC_FORMATS,
	},
	.capture = {
		.channels_min = 2,
		.channels_max = 2,
		.rates = MXS_ADC_RATES,
		.formats = MXS_ADC_FORMATS,
	},
	.ops = &mxs_adc_dai_ops,
};

static const struct snd_soc_component_driver mxs_adc_component = {
	.name		= "mxs-xxx",	//TODO change this name
};

static int mxs_adc_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct mxs_adc_priv *mxs_adc;
	int ret = 0;

	if (!np)
		return -EINVAL;

	mxs_adc = devm_kzalloc(&pdev->dev, sizeof(struct mxs_adc_priv), GFP_KERNEL);
	if (!mxs_adc)
		return -ENOMEM;
	
	mxs_adc->audioout_base = devm_ioremap(&pdev->dev, 0x80048000, 0x2000);
	if (IS_ERR(mxs_adc->audioout_base))
		return PTR_ERR(mxs_adc->audioout_base);
	
	mxs_adc->audioin_base = devm_ioremap(&pdev->dev, 0x8004c000, 0x2000);
	if (IS_ERR(mxs_adc->audioin_base))
		return PTR_ERR(mxs_adc->audioin_base);
	
	mxs_adc->rtc_base = devm_ioremap(&pdev->dev, 0x8005c000, 0x2000);
	if (IS_ERR(mxs_adc->rtc_base))
		return PTR_ERR(mxs_adc->rtc_base);
	
	/* Get IRQ numbers */
	mxs_adc->dma_adc_err_irq = platform_get_irq(pdev, 0);
	if (mxs_adc->dma_adc_err_irq < 0) {
		ret = mxs_adc->dma_adc_err_irq;
		dev_err(&pdev->dev, "failed to get ADC DMA ERR irq resource: %d\n", ret);
		return ret;
	}
	
	mxs_adc->dma_dac_err_irq = platform_get_irq(pdev, 1);
	if (mxs_adc->dma_dac_err_irq < 0) {
		ret = mxs_adc->dma_dac_err_irq;
		dev_err(&pdev->dev, "failed to get DAC DMA ERR irq resource: %d\n", ret);
		return ret;
	}
	
	/* Request IRQs */
#ifdef ENABLE_HS_DETECT
	mxs_adc->hp_short_irq = platform_get_irq(pdev, 2);
	if (mxs_adc->hp_short_irq < 0) {
		ret = mxs_adc->hp_short_irq;
		dev_err(&pdev->dev, "failed to get HP_SHORT irq resource: %d\n", ret);
		return ret;
	}
#endif	

	ret = devm_request_irq(&pdev->dev, mxs_adc->dma_adc_err_irq, mxs_adc_err_irq, 0, "MXS ADC Error",
			  mxs_adc);
	if (ret) {
		printk(KERN_ERR "%s: Unable to request ADC error irq %d\n",
		       __func__, mxs_adc->dma_adc_err_irq);
		return ret;
	}

	ret = devm_request_irq(&pdev->dev, mxs_adc->dma_dac_err_irq, mxs_dac_err_irq, 0, "MXS DAC Error",
			  mxs_adc);
	if (ret) {
		printk(KERN_ERR "%s: Unable to request DAC error irq %d\n",
		       __func__, mxs_adc->dma_dac_err_irq);
		return ret;
	}
#ifdef ENABLE_HS_DETECT
	ret = devm_request_irq(&pdev->dev, mxs_adc->hp_short_irq, mxs_short_irq,
		IRQF_DISABLED | IRQF_SHARED, "MXS DAC and ADC HP SHORT", mxs_adc);
	if (ret) {
		printk(KERN_ERR "%s: Unable to request ADC/DAC HP SHORT irq %d\n",
		       __func__, mxs_adc->hp_short_irq);
		return ret;
	}

	INIT_DELAYED_WORK(&mxs_adc->hs_det_work, mxs_adc_hs_det_work);
#endif

	INIT_DELAYED_WORK(&mxs_adc->adc_ramp_work, mxs_adc_ramp_work);
	INIT_DELAYED_WORK(&mxs_adc->dac_ramp_work, mxs_dac_ramp_work);
	mxs_adc->adc_ramp_queue = create_singlethread_workqueue("buildin-adc-queue");
	mxs_adc->dac_ramp_queue = create_singlethread_workqueue("buildin-dac-queue");

	if (!mxs_adc->adc_ramp_queue) {
		dev_err(&pdev->dev, "create adc_ramp_queue fail...\n");
		goto failed_pdev_thread1;
		ret = -EBUSY;
	}

	if (!mxs_adc->dac_ramp_queue) {
		dev_err(&pdev->dev, "create dac_ramp_queue fail...\n");
		goto failed_pdev_thread2;
		ret = -EBUSY;
	}

	mxs_adc->play_state = MXS_ADC_PLAY_STATE_STOPPED;
	mxs_adc->cap_state = MXS_ADC_CAP_STATE_STOPPED;

	platform_set_drvdata(pdev, mxs_adc);

	ret = snd_soc_register_component(&pdev->dev, &mxs_adc_component, &mxs_adc_dai, 1);
	if (ret) {
		dev_err(&pdev->dev, "register DAI failed\n");
		return ret;
	}

	ret = mxs_adc_pcm_platform_register(&pdev->dev);
	if (ret) {
		dev_err(&pdev->dev, "register PCM failed: %d\n", ret);
		goto failed_pdev_alloc;
	}

	return 0;

failed_pdev_thread2:
	destroy_workqueue(mxs_adc->adc_ramp_queue);
failed_pdev_thread1:
failed_pdev_alloc:
	snd_soc_unregister_component(&pdev->dev);

	return ret;
}

static int mxs_adc_remove(struct platform_device *pdev)
{
	struct mxs_adc_priv *mxs_adc = platform_get_drvdata(pdev);
	
	flush_workqueue(mxs_adc->adc_ramp_queue);
	destroy_workqueue(mxs_adc->adc_ramp_queue);
	flush_workqueue(mxs_adc->dac_ramp_queue);
	destroy_workqueue(mxs_adc->dac_ramp_queue);

	mxs_adc_pcm_platform_unregister(&pdev->dev);
	snd_soc_unregister_component(&pdev->dev);

	return 0;
}

static const struct of_device_id mxs_adc_dai_dt_ids[] = {
	{ .compatible = "fsl,mxs-builtin-cpu-dai", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mxs_adc_dai_dt_ids);

static struct platform_driver mxs_adc_dai_driver = {
	.probe = mxs_adc_probe,
	.remove = mxs_adc_remove,
	
	.driver = {
		.name = "mxs-builtin-cpu-dai",
		.owner = THIS_MODULE,
		.of_match_table = mxs_adc_dai_dt_ids,
	},
};

module_platform_driver(mxs_adc_dai_driver);
 
MODULE_DESCRIPTION("Freescale MXS ADC/DAC SoC Codec DAI Driver");
MODULE_AUTHOR("Michal Ulianko <michal.ulianko@gmail.com>");
MODULE_LICENSE("GPL");
