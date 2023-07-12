// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2023, Linaro Limited

#include <linux/input-event-codes.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/soundwire/sdw.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/soc-dapm.h>
#include <sound/soc.h>

#include "qdsp6/q6afe.h"
#include "common.h"
#include "sdw.h"

#define MI2S_BCLK_RATE		(48000ULL * 2 * 16)

struct sm6115_snd_data {
	bool stream_prepared[AFE_PORT_MAX];
	struct snd_soc_card *card;
	struct sdw_stream_runtime *sruntime[AFE_PORT_MAX];
	struct snd_soc_jack jack;
	bool jack_setup;
};

static int sm6115_snd_init(struct snd_soc_pcm_runtime *rtd)
{
	struct sm6115_snd_data *data = snd_soc_card_get_drvdata(rtd->card);

	return qcom_snd_wcd_jack_setup(rtd, &data->jack, &data->jack_setup);
}

static int sm6115_be_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
				     struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *channels = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_CHANNELS);

	rate->min = rate->max = 48000;
	channels->min = channels->max = 2;

	return 0;
}

static int sm6115_snd_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);
	struct sm6115_snd_data *pdata = snd_soc_card_get_drvdata(rtd->card);

	return qcom_snd_sdw_hw_params(substream, params, &pdata->sruntime[cpu_dai->id]);
}

static int sm6115_snd_prepare(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);
	struct sm6115_snd_data *data = snd_soc_card_get_drvdata(rtd->card);
	struct sdw_stream_runtime *sruntime = data->sruntime[cpu_dai->id];

	return qcom_snd_sdw_prepare(substream, sruntime,
				    &data->stream_prepared[cpu_dai->id]);
}

static int sm6115_snd_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct sm6115_snd_data *data = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);
	struct sdw_stream_runtime *sruntime = data->sruntime[cpu_dai->id];

	return qcom_snd_sdw_hw_free(substream, sruntime,
				    &data->stream_prepared[cpu_dai->id]);
}

static const struct snd_soc_ops sm6115_be_ops = {
	.hw_params = sm6115_snd_hw_params,
	.hw_free = sm6115_snd_hw_free,
	.prepare = sm6115_snd_prepare,
};

static void sm6115_add_be_ops(struct snd_soc_card *card)
{
	struct snd_soc_dai_link *link;
	int i;

	for_each_card_prelinks(card, i, link) {
		if (link->no_pcm == 1) {
			link->init = sm6115_snd_init;
			link->be_hw_params_fixup = sm6115_be_hw_params_fixup;
			link->ops = &sm6115_be_ops;
		}
	}
}

static int sm6115_platform_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card;
	struct sm6115_snd_data *data;
	struct device *dev = &pdev->dev;
	int ret;

	card = devm_kzalloc(dev, sizeof(*card), GFP_KERNEL);
	if (!card)
		return -ENOMEM;

	card->owner = THIS_MODULE;
	/* Allocate the private data */
	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	card->dev = dev;
	dev_set_drvdata(dev, card);
	snd_soc_card_set_drvdata(card, data);
	ret = qcom_snd_parse_of(card);
	if (ret)
		return ret;

	card->driver_name = "sm6115";
	sm6115_add_be_ops(card);
	return devm_snd_soc_register_card(dev, card);
}

static const struct of_device_id snd_sm6115_dt_match[] = {
	{ .compatible = "qcom,sm6115-sndcard" },
	{ }
};

MODULE_DEVICE_TABLE(of, snd_sm6115_dt_match);

static struct platform_driver snd_sm6115_driver = {
	.probe  = sm6115_platform_probe,
	.driver = {
		.name = "snd-sm6115",
		.of_match_table = snd_sm6115_dt_match,
	},
};
module_platform_driver(snd_sm6115_driver);
MODULE_DESCRIPTION("SM6115 ASoC Machine Driver");
MODULE_LICENSE("GPL");
