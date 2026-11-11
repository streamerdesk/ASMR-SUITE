/*
 * Streamerdesk ASMR Suite - Reverb (filtro de audio para OBS Studio 28+)
 * Copyright (c) 2026 Streamerdesk. GPL-2.0-or-later. https://streamerdesk.com
 *
 * Reverb algoritmica estereo (reflexiones tempranas + FDN de 8 lineas) pensada para
 * susurros con micros USB/condensador mono. Solo usa API de libobs 28.0.
 */
#include <obs-module.h>
#include <util/bmem.h>
#include <stdio.h>
#include "sd_atomic.h"
#include "sd_presets.h"
#include "reverb_dsp.h"

#ifndef PLUGIN_NAME
#define PLUGIN_NAME "streamerdesk-asmr-reverb"
#endif
#ifndef PLUGIN_VERSION
#define PLUGIN_VERSION "1.0.1"
#endif

OBS_DECLARE_MODULE()
OBS_MODULE_AUTHOR("Streamerdesk")
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

#define T_(s) obs_module_text(s)

#define S_SIZE "size"
#define S_DECAY "decay_s"
#define S_PREDELAY "predelay_ms"
#define S_DIFFUSION "diffusion"
#define S_FREEZE "freeze"
#define S_DAMPING "damping"
#define S_LOWCUT "lowcut_hz"
#define S_HIGHCUT "highcut_hz"
#define S_ERLATE "er_late"
#define S_WIDTH "width"
#define S_MOD "modulation"
#define S_INPUT "input_mode"
#define S_DUCK "duck"
#define S_DUCK_REL "duck_release_ms"
#define S_MIX "mix"
#define S_GAIN "out_gain_db"

static const char *const preset_keys[] = {
	S_SIZE,    S_DECAY,  S_PREDELAY, S_DIFFUSION, S_FREEZE, S_DAMPING,  S_LOWCUT, S_HIGHCUT,
	S_ERLATE,  S_WIDTH,  S_MOD,      S_INPUT,     S_DUCK,   S_DUCK_REL, S_MIX,    S_GAIN,
};

/* presets integrados: pocos, representativos, editables y guardables como propios */
static const struct sd_builtin_preset builtin_presets[] = {
	{"intimate_room", "Preset.IntimateRoom",
	 "{\"size\":22.0,\"decay_s\":0.6,\"predelay_ms\":6.0,\"diffusion\":65.0,\"damping\":60.0,"
	 "\"lowcut_hz\":220.0,\"highcut_hz\":8000.0,\"er_late\":35.0,\"width\":90.0,\"modulation\":25.0,"
	 "\"duck\":20.0,\"mix\":22.0}"},
	{"soft_hall", "Preset.SoftHall",
	 "{\"size\":58.0,\"decay_s\":1.9,\"predelay_ms\":24.0,\"diffusion\":72.0,\"damping\":50.0,"
	 "\"lowcut_hz\":170.0,\"highcut_hz\":8500.0,\"er_late\":55.0,\"width\":110.0,\"modulation\":35.0,"
	 "\"duck\":35.0,\"duck_release_ms\":450.0,\"mix\":28.0}"},
	{"whisper_cathedral", "Preset.WhisperCathedral",
	 "{\"size\":96.0,\"decay_s\":6.5,\"predelay_ms\":65.0,\"diffusion\":80.0,\"damping\":40.0,"
	 "\"lowcut_hz\":240.0,\"highcut_hz\":7000.0,\"er_late\":80.0,\"width\":130.0,\"modulation\":50.0,"
	 "\"duck\":55.0,\"duck_release_ms\":800.0,\"mix\":32.0}"},
	{"warm_plate", "Preset.WarmPlate",
	 "{\"size\":62.0,\"decay_s\":2.4,\"predelay_ms\":12.0,\"diffusion\":90.0,\"damping\":68.0,"
	 "\"lowcut_hz\":150.0,\"highcut_hz\":6000.0,\"er_late\":88.0,\"width\":120.0,\"modulation\":45.0,"
	 "\"duck\":30.0,\"mix\":26.0}"},
};

static const struct sd_presets_def presets_def = {
	preset_keys,
	sizeof(preset_keys) / sizeof(preset_keys[0]),
	builtin_presets,
	sizeof(builtin_presets) / sizeof(builtin_presets[0]),
};

struct rv_filter {
	obs_source_t *context; /* DEBE ser el primer miembro (lo usa sd_presets) */
	rvb_state dsp;
	int dsp_ok;
	rvb_params slots[2];
	sd_atomic_long active;
	sd_atomic_long version;
	long seen_version;
	size_t channels;
};

static const char *rv_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return T_("Reverb");
}

static void rv_update(void *data, obs_data_t *s)
{
	struct rv_filter *f = data;
	rvb_params p;
	rvb_default_params(&p);
	p.size = (float)obs_data_get_double(s, S_SIZE) / 100.0f;
	p.decay_s = (float)obs_data_get_double(s, S_DECAY);
	p.predelay_ms = (float)obs_data_get_double(s, S_PREDELAY);
	p.diffusion = (float)obs_data_get_double(s, S_DIFFUSION) / 100.0f;
	p.freeze = obs_data_get_bool(s, S_FREEZE);
	p.damping = (float)obs_data_get_double(s, S_DAMPING) / 100.0f;
	p.lowcut_hz = (float)obs_data_get_double(s, S_LOWCUT);
	p.highcut_hz = (float)obs_data_get_double(s, S_HIGHCUT);
	p.er_late = (float)obs_data_get_double(s, S_ERLATE) / 100.0f;
	p.width = (float)obs_data_get_double(s, S_WIDTH) / 100.0f;
	p.modulation = (float)obs_data_get_double(s, S_MOD) / 100.0f;
	p.mono_in = obs_data_get_int(s, S_INPUT) == 0;
	p.duck = (float)obs_data_get_double(s, S_DUCK) / 100.0f;
	p.duck_release_ms = (float)obs_data_get_double(s, S_DUCK_REL);
	p.mix = (float)obs_data_get_double(s, S_MIX) / 100.0f;
	p.out_gain = sd_db_to_lin((float)obs_data_get_double(s, S_GAIN));

	/* doble buffer sin bloqueos: escribe en la ranura inactiva y la publica */
	long next = 1 - sd_atomic_load(&f->active);
	f->slots[next] = p;
	sd_atomic_store(&f->active, next);
	sd_atomic_inc(&f->version);
}

static void *rv_create(obs_data_t *settings, obs_source_t *source)
{
	struct rv_filter *f = bzalloc(sizeof(*f));
	f->context = source;
	audio_t *audio = obs_get_audio();
	f->channels = audio_output_get_channels(audio);
	f->dsp_ok = rvb_init(&f->dsp, (float)audio_output_get_sample_rate(audio)) == 0;
	if (!f->dsp_ok)
		blog(LOG_ERROR, "[%s] no se pudo reservar memoria para la reverb", PLUGIN_NAME);
	f->seen_version = -1;
	rv_update(f, settings);
	return f;
}

static void rv_destroy(void *data)
{
	struct rv_filter *f = data;
	rvb_free(&f->dsp);
	bfree(f);
}

static struct obs_audio_data *rv_filter_audio(void *data, struct obs_audio_data *audio)
{
	struct rv_filter *f = data;
	if (!f->dsp_ok || !audio || audio->frames == 0 || !audio->data[0])
		return audio;
	long v = sd_atomic_load(&f->version);
	if (v != f->seen_version) {
		rvb_set_params(&f->dsp, &f->slots[sd_atomic_load(&f->active)]);
		f->seen_version = v;
	}
	float *l = (float *)audio->data[0];
	float *r = f->channels >= 2 ? (float *)audio->data[1] : NULL;
	/* canales 3+ (5.1, 7.1) pasan sin tocar */
	rvb_process(&f->dsp, l, r, audio->frames);
	return audio;
}

static void rv_defaults(obs_data_t *s)
{
	sd_presets_defaults(s);
	obs_data_set_default_double(s, S_SIZE, 45.0);
	obs_data_set_default_double(s, S_DECAY, 1.6);
	obs_data_set_default_double(s, S_PREDELAY, 20.0);
	obs_data_set_default_double(s, S_DIFFUSION, 70.0);
	obs_data_set_default_bool(s, S_FREEZE, false);
	obs_data_set_default_double(s, S_DAMPING, 50.0);
	obs_data_set_default_double(s, S_LOWCUT, 180.0);
	obs_data_set_default_double(s, S_HIGHCUT, 9000.0);
	obs_data_set_default_double(s, S_ERLATE, 55.0);
	obs_data_set_default_double(s, S_WIDTH, 100.0);
	obs_data_set_default_double(s, S_MOD, 35.0);
	obs_data_set_default_int(s, S_INPUT, 0);
	obs_data_set_default_double(s, S_DUCK, 30.0);
	obs_data_set_default_double(s, S_DUCK_REL, 400.0);
	obs_data_set_default_double(s, S_MIX, 25.0);
	obs_data_set_default_double(s, S_GAIN, 0.0);
}

static bool visibility_changed(obs_properties_t *props, obs_property_t *p, obs_data_t *s)
{
	UNUSED_PARAMETER(p);
	bool frozen = obs_data_get_bool(s, S_FREEZE);
	/* congelada, la cola no decae: el tiempo de decaimiento y la amortiguacion no actuan */
	obs_property_set_visible(obs_properties_get(props, S_DECAY), !frozen);
	obs_property_set_visible(obs_properties_get(props, S_DAMPING), !frozen);
	obs_property_set_visible(obs_properties_get(props, S_DUCK_REL), obs_data_get_double(s, S_DUCK) > 0.0);
	return true;
}

static obs_property_t *help(obs_property_t *p, const char *key)
{
	obs_property_set_long_description(p, T_(key));
	return p;
}

static obs_property_t *slider(obs_properties_t *g, const char *key, const char *label, double min, double max,
			      double step, const char *suffix)
{
	obs_property_t *p = obs_properties_add_float_slider(g, key, T_(label), min, max, step);
	obs_property_float_set_suffix(p, suffix);
	char hk[64];
	snprintf(hk, sizeof(hk), "%s.Help", label);
	return help(p, hk);
}

static obs_properties_t *rv_properties(void *data)
{
	UNUSED_PARAMETER(data);
	obs_properties_t *props = obs_properties_create();
	obs_properties_t *g;
	obs_property_t *p;

	/* 1. Presets */
	sd_presets_add_properties(props, &presets_def);

	/* 2. Sala */
	g = obs_properties_create();
	slider(g, S_SIZE, "Size", 0.0, 100.0, 1.0, " %");
	slider(g, S_DECAY, "Decay", 0.2, 10.0, 0.05, " s");
	slider(g, S_PREDELAY, "PreDelay", 0.0, 200.0, 1.0, " ms");
	slider(g, S_DIFFUSION, "Diffusion", 0.0, 100.0, 1.0, " %");
	p = help(obs_properties_add_bool(g, S_FREEZE, T_("Freeze")), "Freeze.Help");
	obs_property_set_modified_callback(p, visibility_changed);
	obs_properties_add_group(props, "grp_room", T_("Group.Room"), OBS_GROUP_NORMAL, g);

	/* 3. Tono */
	g = obs_properties_create();
	slider(g, S_DAMPING, "Damping", 0.0, 100.0, 1.0, " %");
	slider(g, S_LOWCUT, "LowCut", 20.0, 1000.0, 1.0, " Hz");
	slider(g, S_HIGHCUT, "HighCut", 1000.0, 20000.0, 10.0, " Hz");
	obs_properties_add_group(props, "grp_tone", T_("Group.Tone"), OBS_GROUP_NORMAL, g);

	/* 4. Espacio / estereo */
	g = obs_properties_create();
	slider(g, S_ERLATE, "EarlyLate", 0.0, 100.0, 1.0, " %");
	slider(g, S_WIDTH, "Width", 0.0, 150.0, 1.0, " %");
	slider(g, S_MOD, "Modulation", 0.0, 100.0, 1.0, " %");
	p = obs_properties_add_list(g, S_INPUT, T_("InputMode"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, T_("InputMode.Mono"), 0);
	obs_property_list_add_int(p, T_("InputMode.Stereo"), 1);
	help(p, "InputMode.Help");
	obs_properties_add_group(props, "grp_space", T_("Group.Space"), OBS_GROUP_NORMAL, g);

	/* 5. Ducking */
	g = obs_properties_create();
	p = slider(g, S_DUCK, "Duck", 0.0, 100.0, 1.0, " %");
	obs_property_set_modified_callback(p, visibility_changed);
	slider(g, S_DUCK_REL, "DuckRelease", 20.0, 2000.0, 10.0, " ms");
	obs_properties_add_group(props, "grp_duck", T_("Group.Duck"), OBS_GROUP_NORMAL, g);

	/* 6. Mezcla */
	g = obs_properties_create();
	slider(g, S_MIX, "DryWet", 0.0, 100.0, 1.0, " %");
	slider(g, S_GAIN, "OutputGain", -24.0, 12.0, 0.1, " dB");
	obs_properties_add_group(props, "grp_mix", T_("Group.Mix"), OBS_GROUP_NORMAL, g);

	return props;
}

static struct obs_source_info reverb_filter = {
	.id = "streamerdesk_asmr_reverb",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_AUDIO,
	.get_name = rv_get_name,
	.create = rv_create,
	.destroy = rv_destroy,
	.update = rv_update,
	.filter_audio = rv_filter_audio,
	.get_defaults = rv_defaults,
	.get_properties = rv_properties,
};

const char *obs_module_name(void)
{
	return "Streamerdesk ASMR - Reverb";
}

const char *obs_module_description(void)
{
	return T_("Description");
}

bool obs_module_load(void)
{
	obs_register_source(&reverb_filter);
	blog(LOG_INFO, "[%s] cargado (version %s)", PLUGIN_NAME, PLUGIN_VERSION);
	return true;
}
