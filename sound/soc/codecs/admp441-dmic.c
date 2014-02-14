/*
 * Copyright 2013 Navicron.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>

static struct snd_soc_dai_driver dmic_dai = {
   .name = "admp441-dmic-dai",
   .capture = {
       .stream_name = "Capture",
       .channels_min = 2,
       .channels_max = 2,
       .rates = SNDRV_PCM_RATE_48000,
       .formats = (SNDRV_PCM_FMTBIT_S24_LE |\
                   SNDRV_PCM_FMTBIT_S32_LE),
   },
};

static const struct snd_soc_dapm_widget dmic_dapm_widgets[] = {
   SND_SOC_DAPM_AIF_OUT("DMIC AIF", "Capture", 0, SND_SOC_NOPM, 0, 0),
   SND_SOC_DAPM_INPUT("DMic"),
};

static const struct snd_soc_dapm_route intercon[] = {
   {"DMIC AIF", NULL, "DMic"},
};

static struct snd_soc_codec_driver soc_dmic = {
   .dapm_widgets = dmic_dapm_widgets,
   .num_dapm_widgets = ARRAY_SIZE(dmic_dapm_widgets),
   .dapm_routes = intercon,
   .num_dapm_routes = ARRAY_SIZE(intercon),
};

static int dmic_dev_probe(struct platform_device *pdev)
{
   return snd_soc_register_codec(&pdev->dev,
           &soc_dmic, &dmic_dai, 1);
}

static int dmic_dev_remove(struct platform_device *pdev)
{
   snd_soc_unregister_codec(&pdev->dev);
   return 0;
}

static const struct of_device_id dmic_dt_ids[] = {
   { .compatible = "admp441-dmic-codec", },
   { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, dmic_dt_ids);

static struct platform_driver dmic_driver = {
   .driver = {
       .name = "admp441-dmic-codec",
       .owner = THIS_MODULE,
      .of_match_table = dmic_dt_ids,
   },
   .probe = dmic_dev_probe,
   .remove = dmic_dev_remove,
};

module_platform_driver(dmic_driver);

MODULE_DESCRIPTION("ADMP441 DMIC driver");
MODULE_AUTHOR("Navicron");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:admp441-dmic");