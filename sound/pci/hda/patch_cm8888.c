/*
 * Copyright (c) 2014 Navicron
 * 
 * CM8888 adaption for dwams project. 
 * 
 * Notes! 
 * 	- Special firmware needed for CM8888. 
 * 	- This driver will not work with commercial cards
 *  - Removed almost everything unnecessary parts
 *
 *
 *  This driver is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This driver is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/module.h>
#include <sound/core.h>
#include "hda_codec.h"
#include "hda_local.h"
#include "hda_auto_parser.h"
#include "hda_jack.h"
#include "hda_generic.h"

#define NUM_PINS	10


struct cmi_spec {
	struct hda_gen_spec gen;

	/* below are only for static models */

	/* playback */
	struct hda_multi_out multiout;
	hda_nid_t dac_nids[AUTO_CFG_MAX_OUTS];	/* NID for each DAC */
	int num_dacs;

	/* capture */
	const hda_nid_t *adc_nids;

	/* capture source */
	const struct hda_input_mux *input_mux;
	unsigned int cur_mux[2];

	/* channel mode */
	int num_channel_modes;
	const struct hda_channel_mode *channel_modes;

	struct hda_pcm pcm_rec[2];	/* PCM information */

	/* pin default configuration */
	hda_nid_t pin_nid[NUM_PINS];
	unsigned int def_conf[NUM_PINS];
	unsigned int pin_def_confs;

	/* multichannel pins */
	struct hda_verb multi_init[9];	/* 2 verbs for each pin + terminator */
};

static const hda_nid_t cm8888_dac_nids[] = {
	0x02, 0x03, 0x04, 0x05
};

static const hda_nid_t cm8888_adc_nids[] = {
	0x08, 0x0a, 0x07, 0x09
};

static hda_nid_t fch_dac_nid = 0x06;
static hda_nid_t fch_adc_nid = 0x0b;


static const struct hda_verb cm8888_ctrl_init[] = {

//	{ 0x10, AC_VERB_SET_CONFIG_DEFAULT_BYTES_0, 0x13 },
//	{ 0x10, AC_VERB_SET_CONFIG_DEFAULT_BYTES_1, 0x30 },
//	{ 0x10, AC_VERB_SET_CONFIG_DEFAULT_BYTES_2, 0x01 },
//	{ 0x10, AC_VERB_SET_CONFIG_DEFAULT_BYTES_3, 0x01 },
//	{ 0x15, AC_VERB_SET_CONFIG_DEFAULT_BYTES_1, 0x33 },

	/* set output PINs  */
	{ 0x0c, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x0d, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x0e, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x0f, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x10, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },

	/* set input PINs  */
	{ 0x11, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN },
	{ 0x12, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN },
	{ 0x13, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN },
	{ 0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN },
	{ 0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN },
	{} /* terminator */
};

/*
 */

static int cm8888_init(struct hda_codec *codec)
{
	snd_hda_sequence_write(codec, cm8888_ctrl_init);

	return 0;
}

/*
 * Analog playback callbacks
 */
static int cm8888_playback_pcm_open(struct hda_pcm_stream *hinfo,
				     struct hda_codec *codec,
				     struct snd_pcm_substream *substream)
{
	struct cmi_spec *spec = codec->spec;

	/* force subdev0, stream 0 == 8ch stream */
	if(substream->number == 0) {
		snd_hda_multi_out_analog_open(codec, &spec->multiout, substream, hinfo);
		substream->runtime->hw.channels_min = 8;
		substream->runtime->hw.channels_max = 8;
	} 
	/* force subdev1, stream 1 == 2ch stream */
	else {
		substream->runtime->hw.channels_min = 2;
		substream->runtime->hw.channels_max = 2;
		snd_pcm_hw_constraint_step(substream->runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS, 2);
	}
	
	return 0;					
}

static int cm8888_playback_pcm_prepare(struct hda_pcm_stream *hinfo,
					struct hda_codec *codec,
					unsigned int stream_tag,
					unsigned int format,
					struct snd_pcm_substream *substream)
{
	struct cmi_spec *spec = codec->spec;

	/* stream 0 == 8ch stream */
	if(substream->number == 0) {
		snd_hda_multi_out_analog_prepare(codec, &spec->multiout, stream_tag,
						format, substream);
	}
	else {
		snd_hda_codec_setup_stream(codec, fch_dac_nid, stream_tag, 0, format);
	}
	
	return 0;					
}

static int cm8888_playback_pcm_cleanup(struct hda_pcm_stream *hinfo,
				       struct hda_codec *codec,
				       struct snd_pcm_substream *substream)
{
	struct cmi_spec *spec = codec->spec;

	if(substream->number == 0) {
		snd_hda_multi_out_analog_cleanup(codec, &spec->multiout);
	}
	else {
		snd_hda_codec_cleanup_stream(codec, fch_dac_nid);
	}

	return 0;					
}

/*
 * Analog capture
 */
static int cm8888_capture_pcm_open(struct hda_pcm_stream *hinfo,
				     struct hda_codec *codec,
				     struct snd_pcm_substream *substream)
{
	/* force subdev0, stream 0 == 8ch stream */
	if(substream->number == 0) {
		substream->runtime->hw.channels_min = 8;
		substream->runtime->hw.channels_max = 8;
	} 
	/* force subdev1, stream 1 == 2ch stream */
	else {
		substream->runtime->hw.channels_min = 2;
		substream->runtime->hw.channels_max = 2;
	}
	
	return 0;					
}
 
static int cm8888_capture_pcm_prepare(struct hda_pcm_stream *hinfo,
				      struct hda_codec *codec,
				      unsigned int stream_tag,
				      unsigned int format,
				      struct snd_pcm_substream *substream)
{
	if(substream->number == 0) {
		snd_hda_codec_setup_stream(codec, cm8888_adc_nids[0], stream_tag, 0, format);
		snd_hda_codec_setup_stream(codec, cm8888_adc_nids[1], stream_tag, 2, format);
		snd_hda_codec_setup_stream(codec, cm8888_adc_nids[2], stream_tag, 4, format);
		snd_hda_codec_setup_stream(codec, cm8888_adc_nids[3], stream_tag, 6, format);
	} 
	else {
		snd_hda_codec_setup_stream(codec, fch_adc_nid, stream_tag, 0, format);
	}
	return 0;
}

static int cm8888_capture_pcm_cleanup(struct hda_pcm_stream *hinfo,
				      struct hda_codec *codec,
				      struct snd_pcm_substream *substream)
{
	/* stream 0 == 8ch stream */
	if(substream->number == 0) {
		snd_hda_codec_cleanup_stream(codec, cm8888_adc_nids[0]);
		snd_hda_codec_cleanup_stream(codec, cm8888_adc_nids[1]);
		snd_hda_codec_cleanup_stream(codec, cm8888_adc_nids[2]);
		snd_hda_codec_cleanup_stream(codec, cm8888_adc_nids[3]);
	} 
	else {
		snd_hda_codec_cleanup_stream(codec, fch_adc_nid);
	}

	return 0;
}

/*
 */
static const struct hda_pcm_stream cm8888_pcm_analog_playback = {
	.substreams = 2,
	.channels_min = 2,
	.channels_max = 8,
	.nid = 0x06, /* NID to query formats and rates */
	.ops = {
		.open = cm8888_playback_pcm_open,
		.prepare = cm8888_playback_pcm_prepare,
		.cleanup = cm8888_playback_pcm_cleanup
	},
};

static const struct hda_pcm_stream cm8888_pcm_analog_capture = {
	.substreams = 2,
	.channels_min = 2,
	.channels_max = 8,
	.nid = 0x0b, /* NID to query formats and rates */
	.ops = {
		.open = cm8888_capture_pcm_open,
		.prepare = cm8888_capture_pcm_prepare,
		.cleanup = cm8888_capture_pcm_cleanup
	},
};

static int cm8888_build_pcms(struct hda_codec *codec)
{
	struct cmi_spec *spec = codec->spec;
	struct hda_pcm *info = spec->pcm_rec;

	codec->num_pcms = 1;
	codec->pcm_info = info;

	info->name = "CM8888";
	info->stream[SNDRV_PCM_STREAM_PLAYBACK] = cm8888_pcm_analog_playback;
	info->stream[SNDRV_PCM_STREAM_CAPTURE] = cm8888_pcm_analog_capture;

	return 0;
}

static void cm8888_free(struct hda_codec *codec)
{
	kfree(codec->spec);
}

/*
 */
static const struct hda_codec_ops cm8888_patch_ops = {
	.build_pcms = cm8888_build_pcms,
	.init = cm8888_init,
	.free = cm8888_free,
};

/*
 * stuff for auto-parser
 */
static const struct hda_codec_ops cmi_auto_patch_ops = {
	.build_controls = snd_hda_gen_build_controls,
	.build_pcms = snd_hda_gen_build_pcms,
	.init = snd_hda_gen_init,
	.free = snd_hda_gen_free,
	.unsol_event = snd_hda_jack_unsol_event,
};

static int patch_cm8888(struct hda_codec *codec)
{
	struct cmi_spec *spec;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;
	
	/* copy default DAC NIDs */
	memcpy(spec->dac_nids, cm8888_dac_nids, sizeof(spec->dac_nids));
	spec->num_dacs = sizeof(cm8888_dac_nids)/sizeof(hda_nid_t);
	spec->multiout.max_channels = 8;
	spec->multiout.num_dacs = spec->num_dacs;
	spec->multiout.dac_nids = spec->dac_nids;

	spec->adc_nids = cm8888_adc_nids;

	codec->patch_ops = cm8888_patch_ops;

	return 0;
}

/*
 * patch entries
 */
static const struct hda_codec_preset snd_hda_preset_cm8888[] = {
	{ .id = 0x13f68888, .name = "CM8888", .patch = patch_cm8888 },
	{} /* terminator */
};

MODULE_ALIAS("snd-hda-codec-id:13f68888");

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("C-Media CM8888-audio codec");

static struct hda_codec_preset_list cm8888_list = {
	.preset = snd_hda_preset_cm8888,
	.owner = THIS_MODULE,
};

static int __init patch_cm8888_init(void)
{
	return snd_hda_add_codec_preset(&cm8888_list);
}

static void __exit patch_cm8888_exit(void)
{
	snd_hda_delete_codec_preset(&cm8888_list);
}

module_init(patch_cm8888_init)
module_exit(patch_cm8888_exit)
