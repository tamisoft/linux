/*
 * Copyright 2013 Navicron.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/jack.h>
#include <sound/soc-dapm.h>

#include "mxs-saif.h"

static int mxs_admp441_hw_params(struct snd_pcm_substream *substream,
   struct snd_pcm_hw_params *params)
{
   struct snd_soc_pcm_runtime *rtd = substream->private_data;
   struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
   u32 dai_format;
   int ret;

   dai_format = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
           SND_SOC_DAIFMT_CBS_CFS;

   /* set cpu DAI configuration */
   ret = snd_soc_dai_set_fmt(cpu_dai, dai_format);
   if (ret) {
       dev_err(cpu_dai->dev, "Failed to set dai format to %08x\n",
           dai_format);
       return ret;
   }

   return 0;
}

static struct snd_soc_ops mxs_admp441_ops = {
   .hw_params = mxs_admp441_hw_params,
};

static struct snd_soc_dai_link mxs_admp441_dai[] = {
   {
       .name = "mxs-admp441-dmic-dai",
       .stream_name    = "mxs-admp441-capture",
       .codec_dai_name = "admp441-dmic-dai",
       .ops        = &mxs_admp441_ops,
       .capture_only   = true,
   },
};

static struct snd_soc_card mxs_admp441 = {
   .name       = "mxs-admp441",
   .owner      = THIS_MODULE,
   .dai_link   = mxs_admp441_dai,
   .num_links  = ARRAY_SIZE(mxs_admp441_dai),
};

static int mxs_admp441_probe_dt(struct platform_device *pdev)
{
   struct device_node *np = pdev->dev.of_node;
   struct device_node *saif_np, *codec_np;

   if (!np)
       return 1; /* no device tree */

   saif_np = of_parse_phandle(np, "saif-controller", 0);
   codec_np = of_parse_phandle(np, "dmic-codec", 0);

   if (!saif_np || !codec_np ) {
       dev_err(&pdev->dev, "phandle missing or invalid\n");
       return -EINVAL;
   }

   mxs_admp441_dai[0].codec_name = NULL;
   mxs_admp441_dai[0].codec_of_node = codec_np;
   mxs_admp441_dai[0].cpu_dai_name = NULL;
   mxs_admp441_dai[0].cpu_of_node = saif_np;
   mxs_admp441_dai[0].platform_name = NULL;
   mxs_admp441_dai[0].platform_of_node = saif_np;

   of_node_put(codec_np);
   of_node_put(saif_np);

   return 0;
}

static int mxs_admp441_probe(struct platform_device *pdev)
{
   struct snd_soc_card *card = &mxs_admp441;
   int ret;

   ret = mxs_admp441_probe_dt(pdev);
   if (ret < 0)
       return ret;

   card->dev = &pdev->dev;
   platform_set_drvdata(pdev, card);

   ret = snd_soc_register_card(card);
   if (ret) {
       dev_err(&pdev->dev, "mxs_admp441_probe: snd_soc_register_card failed (%d)\n",
           ret);
       return ret;
   }

   return 0;
}

static int mxs_admp441_remove(struct platform_device *pdev)
{
   struct snd_soc_card *card = platform_get_drvdata(pdev);

   mxs_saif_put_mclk(0);

   snd_soc_unregister_card(card);

   return 0;
}

static const struct of_device_id mxs_admp441_dt_ids[] = {
   { .compatible = "navicron,mxs-admp441", },
   { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mxs_admp441_dt_ids);

static struct platform_driver mxs_admp441_audio_driver = {
   .driver = {
       .name = "mxs-admp441",
       .owner = THIS_MODULE,
       .of_match_table = mxs_admp441_dt_ids,
   },
   .probe = mxs_admp441_probe,
   .remove = mxs_admp441_remove,
};

module_platform_driver(mxs_admp441_audio_driver);

MODULE_AUTHOR("Navicron");
MODULE_DESCRIPTION("MXS ALSA admp441 Machine driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:mxs-admp441");