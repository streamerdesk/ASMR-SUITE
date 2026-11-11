/*
 * Streamerdesk ASMR Suite - Susurro de oido a oido (filtro de audio para OBS Studio)
 * Copyright (c) 2026 Streamerdesk. GPL-2.0-or-later. https://streamerdesk.com
 *
 * La voz se desliza suavemente de una oreja a la otra para escuchar con auriculares.
 * Compatible con OBS Studio 28.0 y posteriores (solo usa API de libobs 28.0).
 */
#include <obs-module.h>
#include <util/bmem.h>
#include "sd_atomic.h"
#include "sd_presets.h"
#include "ear_dsp.h"

#ifndef PLUGIN_NAME
#define PLUGIN_NAME "streamerdesk-asmr-ear-to-ear"
#endif
#ifndef PLUGIN_VERSION
#define PLUGIN_VERSION "1.0.0"
#endif

OBS_DECLARE_MODULE()
OBS_MODULE_AUTHOR("Streamerdesk")
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

#define T_(s) obs_module_text(s)

#define S_MODE "mode"
#define S_POSITION "position"
#define S_SPEED_UNIT "speed_unit"
#define S_RATE_HZ "rate_hz"
#define S_SWEEP_S "sweep_s"
#define S_BPM "bpm"
#define S_DIV "division"
#define S_DEPTH "depth"
#define S_SHAPE "shape"
#define S_DWELL "dwell"
#define S_PAN "pan_mode"
#define S_BINAURAL "binaural"
#define S_PROX "proximity"
#define S_KEEP "center_keep"
#define S_DIP "center_dip_db"
#define S_GAIN "out_gain_db"
#define S_START "start_side"

/* duracion de un recorrido oreja -> oreja, en negras */
static const struct {
	const char *label;
	double beats;
} divisions[] = {
	{"Div.Quarter", 1.0}, {"Div.Half", 2.0},  {"Div.DottedHalf", 3.0}, {"Div.Bar1", 4.0},
	{"Div.Bar2", 8.0},    {"Div.Bar4", 16.0}, {"Div.Bar8", 32.0},
};
#define NUM_DIVS (sizeof(divisions) / sizeof(divisions[0]))
#define DEFAULT_DIV 3

/* presets de fabrica (relativos a los valores por defecto) */
static const struct sd_builtin_preset builtin_presets[] = {
	{"susurro_suave", "Preset.SoftWhisper", "{}"},
	{"oreja_lenta", "Preset.SlowEars",
	 "{\"sweep_s\":8.0,\"speed_unit\":1,\"dwell\":50.0,\"depth\":85.0,\"binaural\":70.0,\"proximity\":40.0}"},
	{"cosquillas", "Preset.Tingles",
	 "{\"sweep_s\":1.2,\"speed_unit\":1,\"shape\":0,\"depth\":60.0,\"binaural\":55.0,\"proximity\":20.0}"},
	{"paseo_organico", "Preset.Wander", "{\"mode\":2,\"sweep_s\":3.0,\"speed_unit\":1,\"depth\":75.0}"},
	{"pegado_oreja_izq", "Preset.LeftEar",
	 "{\"mode\":0,\"position\":-85.0,\"binaural\":80.0,\"proximity\":60.0}"},
	{"pegado_oreja_der", "Preset.RightEar", "{\"mode\":0,\"position\":85.0,\"binaural\":80.0,\"proximity\":60.0}"},
	{"ritmo_70", "Preset.Tempo", "{\"mode\":3,\"bpm\":70.0,\"division\":3,\"dwell\":35.0}"},
	{"amplio_cercano", "Preset.WideClose",
	 "{\"depth\":100.0,\"binaural\":100.0,\"proximity\":70.0,\"center_dip_db\":1.5,\"dwell\":40.0,\"sweep_s\":5.0,\"speed_unit\":1}"},
};
#define NUM_PRESETS (sizeof(builtin_presets) / sizeof(builtin_presets[0]))

/* claves que forman parte de un preset */
static const char *const preset_keys[] = {S_MODE,  S_POSITION, S_SPEED_UNIT, S_RATE_HZ, S_SWEEP_S, S_BPM,
					  S_DIV,   S_DEPTH,    S_SHAPE,      S_DWELL,   S_PAN,     S_BINAURAL,
					  S_PROX,  S_KEEP,     S_DIP,        S_GAIN,    S_START};
static const struct sd_presets_def presets_def = {
	preset_keys, sizeof(preset_keys) / sizeof(preset_keys[0]), builtin_presets, NUM_PRESETS,
};

struct ete_filter {
	obs_source_t *context; /* DEBE ser el primer miembro (lo usa sd_presets) */
	ete_state dsp;
	/* seqlock: update() escribe, filter_audio() lee sin bloquear */
	ete_params pending;
	sd_atomic_long seq;
	long seen_seq;
	size_t channels;
};

static const char *ete_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return T_("EarToEar");
}

static void ete_update(void *data, obs_data_t *settings)
{
	struct ete_filter *f = data;
	ete_params p;
	ete_default_params(&p);
	p.mode = (int)obs_data_get_int(settings, S_MODE);
	p.position = (float)obs_data_get_double(settings, S_POSITION) / 100.0f;

	if (p.mode == ETE_MODE_TEMPO) {
		double bpm = obs_data_get_double(settings, S_BPM);
		long long d = obs_data_get_int(settings, S_DIV);
		if (bpm < 20.0)
			bpm = 20.0;
		if (d < 0 || (size_t)d >= NUM_DIVS)
			d = DEFAULT_DIV;
		p.sweep_s = (float)(60.0 / bpm * divisions[d].beats);
	} else if (obs_data_get_int(settings, S_SPEED_UNIT) == 0) {
		double hz = obs_data_get_double(settings, S_RATE_HZ);
		if (hz < 0.005)
			hz = 0.005;
		p.sweep_s = (float)(0.5 / hz); /* 1 Hz = ida y vuelta en 1 s */
	} else {
		p.sweep_s = (float)obs_data_get_double(settings, S_SWEEP_S);
	}

	p.depth = (float)obs_data_get_double(settings, S_DEPTH) / 100.0f;
	p.shape = (int)obs_data_get_int(settings, S_SHAPE);
	p.dwell = (float)obs_data_get_double(settings, S_DWELL) / 100.0f;
	p.pan_mode = (int)obs_data_get_int(settings, S_PAN);
	p.binaural = (float)obs_data_get_double(settings, S_BINAURAL) / 100.0f;
	p.proximity = (float)obs_data_get_double(settings, S_PROX) / 100.0f;
	p.center_keep = (float)obs_data_get_double(settings, S_KEEP) / 100.0f;
	p.center_dip_db = (float)obs_data_get_double(settings, S_DIP);
	p.out_gain = sd_db_to_lin((float)obs_data_get_double(settings, S_GAIN));
	p.start_right = obs_data_get_int(settings, S_START) == 1;

	/* seqlock: impar = escribiendo */
	sd_atomic_inc(&f->seq);
	f->pending = p;
	sd_atomic_inc(&f->seq);
}

static void *ete_create(obs_data_t *settings, obs_source_t *source)
{
	struct ete_filter *f = bzalloc(sizeof(*f));
	f->context = source;
	audio_t *audio = obs_get_audio();
	uint32_t sr = audio_output_get_sample_rate(audio);
	f->channels = audio_output_get_channels(audio);
	ete_update(f, settings);
	ete_init(&f->dsp, (float)sr, &f->pending);
	f->seen_seq = sd_atomic_load(&f->seq);
	return f;
}

static void ete_destroy(void *data)
{
	bfree(data);
}

static struct obs_audio_data *ete_filter_audio(void *data, struct obs_audio_data *audio)
{
	struct ete_filter *f = data;
	if (!audio || audio->frames == 0)
		return audio;

	long s1 = sd_atomic_load(&f->seq);
	if (s1 != f->seen_seq && !(s1 & 1)) {
		ete_params copy = f->pending;
#if defined(_MSC_VER) && !defined(__clang__)
		_ReadWriteBarrier();
#else
		__atomic_thread_fence(__ATOMIC_ACQUIRE);
#endif
		long s2 = sd_atomic_load(&f->seq);
		if (s1 == s2) { /* copia coherente; si no, se reintenta en el siguiente bloque */
			ete_set_params(&f->dsp, &copy);
			f->seen_seq = s1;
		}
	}

	float *l = (float *)audio->data[0];
	float *r = f->channels >= 2 ? (float *)audio->data[1] : NULL;
	if (!l)
		return audio;
	ete_process(&f->dsp, l, r, audio->frames); /* canales 3+ pasan sin tocar */
	return audio;
}

static void ete_defaults(obs_data_t *s)
{
	sd_presets_defaults(s);
	obs_data_set_default_int(s, S_MODE, ETE_MODE_AUTO);
	obs_data_set_default_double(s, S_POSITION, 0.0);
	obs_data_set_default_int(s, S_SPEED_UNIT, 1);
	obs_data_set_default_double(s, S_RATE_HZ, 0.125);
	obs_data_set_default_double(s, S_SWEEP_S, 4.0);
	obs_data_set_default_double(s, S_BPM, 70.0);
	obs_data_set_default_int(s, S_DIV, DEFAULT_DIV);
	obs_data_set_default_double(s, S_DEPTH, 70.0);
	obs_data_set_default_int(s, S_SHAPE, ETE_SHAPE_HOLD);
	obs_data_set_default_double(s, S_DWELL, 30.0);
	obs_data_set_default_int(s, S_PAN, ETE_PAN_BINAURAL);
	obs_data_set_default_double(s, S_BINAURAL, 60.0);
	obs_data_set_default_double(s, S_PROX, 30.0);
	obs_data_set_default_double(s, S_KEEP, 0.0);
	obs_data_set_default_double(s, S_DIP, 0.0);
	obs_data_set_default_double(s, S_GAIN, 0.0);
	obs_data_set_default_int(s, S_START, 0);
}

static void vis(obs_properties_t *props, const char *key, bool v)
{
	obs_property_t *p = obs_properties_get(props, key);
	if (p)
		obs_property_set_visible(p, v);
}

static bool visibility_changed(obs_properties_t *props, obs_property_t *p, obs_data_t *s)
{
	UNUSED_PARAMETER(p);
	long long mode = obs_data_get_int(s, S_MODE);
	bool manual = mode == ETE_MODE_MANUAL;
	bool tempo = mode == ETE_MODE_TEMPO;
	bool hz = obs_data_get_int(s, S_SPEED_UNIT) == 0;
	bool lfo = mode == ETE_MODE_AUTO || tempo;
	vis(props, S_POSITION, manual);
	vis(props, S_SPEED_UNIT, !manual && !tempo);
	vis(props, S_RATE_HZ, !manual && !tempo && hz);
	vis(props, S_SWEEP_S, !manual && !tempo && !hz);
	vis(props, S_BPM, tempo);
	vis(props, S_DIV, tempo);
	vis(props, S_DEPTH, !manual);
	vis(props, S_SHAPE, lfo);
	vis(props, S_DWELL, lfo && obs_data_get_int(s, S_SHAPE) == ETE_SHAPE_HOLD);
	vis(props, S_START, lfo);
	vis(props, S_BINAURAL, obs_data_get_int(s, S_PAN) == ETE_PAN_BINAURAL);
	return true;
}

static obs_property_t *slider(obs_properties_t *g, const char *key, const char *label, double min, double max,
			      double step, const char *suffix)
{
	obs_property_t *p = obs_properties_add_float_slider(g, key, T_(label), min, max, step);
	if (suffix)
		obs_property_float_set_suffix(p, suffix);
	char help[96];
	snprintf(help, sizeof(help), "%s.Help", label);
	obs_property_set_long_description(p, T_(help));
	return p;
}

static obs_property_t *combo(obs_properties_t *g, const char *key, const char *label)
{
	obs_property_t *p = obs_properties_add_list(g, key, T_(label), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	char help[96];
	snprintf(help, sizeof(help), "%s.Help", label);
	obs_property_set_long_description(p, T_(help));
	obs_property_set_modified_callback(p, visibility_changed);
	return p;
}

static obs_properties_t *ete_properties(void *data)
{
	UNUSED_PARAMETER(data);
	obs_properties_t *props = obs_properties_create();
	obs_property_t *p;

	sd_presets_add_properties(props, &presets_def);

	/* --- MOVIMIENTO --- */
	obs_properties_t *g = obs_properties_create();
	p = combo(g, S_MODE, "Mode");
	obs_property_list_add_int(p, T_("Mode.Manual"), ETE_MODE_MANUAL);
	obs_property_list_add_int(p, T_("Mode.Auto"), ETE_MODE_AUTO);
	obs_property_list_add_int(p, T_("Mode.Random"), ETE_MODE_RANDOM);
	obs_property_list_add_int(p, T_("Mode.Tempo"), ETE_MODE_TEMPO);
	slider(g, S_POSITION, "Position", -100.0, 100.0, 1.0, NULL);
	p = combo(g, S_SPEED_UNIT, "SpeedUnit");
	obs_property_list_add_int(p, T_("SpeedUnit.Hz"), 0);
	obs_property_list_add_int(p, T_("SpeedUnit.Seconds"), 1);
	slider(g, S_RATE_HZ, "RateHz", 0.01, 4.0, 0.005, " Hz");
	slider(g, S_SWEEP_S, "SweepSeconds", 0.25, 60.0, 0.05, " s");
	slider(g, S_BPM, "BPM", 30.0, 300.0, 0.1, " BPM");
	p = obs_properties_add_list(g, S_DIV, T_("Division"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_set_long_description(p, T_("Division.Help"));
	for (size_t i = 0; i < NUM_DIVS; i++)
		obs_property_list_add_int(p, T_(divisions[i].label), (long long)i);
	slider(g, S_DEPTH, "Depth", 0.0, 100.0, 1.0, " %");
	p = combo(g, S_SHAPE, "Shape");
	obs_property_list_add_int(p, T_("Shape.Sine"), ETE_SHAPE_SINE);
	obs_property_list_add_int(p, T_("Shape.Hold"), ETE_SHAPE_HOLD);
	slider(g, S_DWELL, "Dwell", 0.0, 90.0, 1.0, " %");
	p = combo(g, S_START, "StartSide");
	obs_property_list_add_int(p, T_("StartSide.Left"), 0);
	obs_property_list_add_int(p, T_("StartSide.Right"), 1);
	obs_properties_add_group(props, "grp_motion", T_("Group.Motion"), OBS_GROUP_NORMAL, g);

	/* --- ESPACIO --- */
	g = obs_properties_create();
	p = combo(g, S_PAN, "PanMode");
	obs_property_list_add_int(p, T_("PanMode.EqualPower"), ETE_PAN_EQUAL_POWER);
	obs_property_list_add_int(p, T_("PanMode.Binaural"), ETE_PAN_BINAURAL);
	slider(g, S_BINAURAL, "Binaural", 0.0, 100.0, 1.0, " %");
	slider(g, S_PROX, "Proximity", 0.0, 100.0, 1.0, " %");
	slider(g, S_KEEP, "CenterKeep", 0.0, 100.0, 1.0, " %");
	slider(g, S_DIP, "CenterDip", 0.0, 6.0, 0.1, " dB");
	obs_properties_add_group(props, "grp_space", T_("Group.Space"), OBS_GROUP_NORMAL, g);

	/* --- SALIDA --- */
	g = obs_properties_create();
	slider(g, S_GAIN, "OutputGain", -24.0, 12.0, 0.1, " dB");
	obs_properties_add_group(props, "grp_out", T_("Group.Output"), OBS_GROUP_NORMAL, g);

	return props;
}

static struct obs_source_info ear_to_ear_filter = {
	.id = "streamerdesk_asmr_ear_to_ear",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_AUDIO,
	.get_name = ete_get_name,
	.create = ete_create,
	.destroy = ete_destroy,
	.update = ete_update,
	.filter_audio = ete_filter_audio,
	.get_defaults = ete_defaults,
	.get_properties = ete_properties,
};

const char *obs_module_name(void)
{
	return "Streamerdesk ASMR - Susurro de oido a oido";
}

const char *obs_module_description(void)
{
	return T_("Description");
}

bool obs_module_load(void)
{
	obs_register_source(&ear_to_ear_filter);
	blog(LOG_INFO, "[%s] cargado (version %s)", PLUGIN_NAME, PLUGIN_VERSION);
	return true;
}
