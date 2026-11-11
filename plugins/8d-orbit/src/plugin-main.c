/*
 * Streamerdesk ASMR Suite - 8D / Orbita (filtro de audio para OBS Studio 28+)
 * Copyright (c) 2026 Streamerdesk. GPL-2.0-or-later. https://streamerdesk.com
 *
 * La voz gira lentamente alrededor de la cabeza del oyente (efecto "8D" con auriculares)
 * usando un modelo binaural (ITD, ILD, sombra de la cabeza, pistas delante/detras y elevacion).
 * Solo usa API de libobs 28.0.
 */
#include <obs-module.h>
#include <util/bmem.h>
#include <stdio.h>
#include "sd_atomic.h"
#include "sd_presets.h"
#include "orbit_dsp.h"

#ifndef PLUGIN_NAME
#define PLUGIN_NAME "streamerdesk-asmr-8d-orbit"
#endif
#ifndef PLUGIN_VERSION
#define PLUGIN_VERSION "1.0.0"
#endif

OBS_DECLARE_MODULE()
OBS_MODULE_AUTHOR("Streamerdesk")
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

#define T_(s) obs_module_text(s)

#define S_MODE "mode"
#define S_UNIT "speed_unit"
#define S_PERIOD "period_s"
#define S_RATE "rate_hz"
#define S_BPM "bpm"
#define S_BARS "bars"
#define S_DIR "direction"
#define S_START "start_deg"
#define S_ARC "arc_deg"
#define S_NEAR "near_ears"
#define S_ELEV "elevation"
#define S_BIN "binaural"
#define S_FB "front_back"
#define S_DIST "distance_m"
#define S_ROOM "room"
#define S_INPUT "input_mode"
#define S_MIX "mix"
#define S_GAIN "out_gain_db"

static const char *const preset_keys[] = {
	S_MODE, S_UNIT, S_PERIOD, S_RATE, S_BPM,  S_BARS, S_DIR,   S_START, S_ARC,
	S_NEAR, S_ELEV, S_BIN,    S_FB,   S_DIST, S_ROOM, S_INPUT, S_MIX,   S_GAIN,
};

/* presets de muestra: cada uno ensena un modo de movimiento */
static const struct sd_builtin_preset builtin_presets[] = {
	{"classic_8d", "Preset.Classic8D",
	 "{\"mode\":0,\"speed_unit\":0,\"period_s\":10.0,\"direction\":0,\"start_deg\":0.0,\"distance_m\":1.0,"
	 "\"front_back\":70.0,\"elevation\":15.0,\"binaural\":90.0,\"room\":25.0,\"near_ears\":25.0,\"mix\":100.0}"},
	{"ear_to_ear", "Preset.EarToEar",
	 "{\"mode\":1,\"speed_unit\":0,\"period_s\":8.0,\"direction\":0,\"start_deg\":-90.0,\"arc_deg\":180.0,"
	 "\"distance_m\":0.3,\"front_back\":40.0,\"elevation\":0.0,\"binaural\":100.0,\"room\":8.0,\"near_ears\":60.0,\"mix\":100.0}"},
	{"butterfly", "Preset.Butterfly",
	 "{\"mode\":2,\"speed_unit\":0,\"period_s\":14.0,\"direction\":0,\"distance_m\":0.5,\"front_back\":60.0,"
	 "\"elevation\":45.0,\"binaural\":85.0,\"room\":18.0,\"near_ears\":30.0,\"mix\":100.0}"},
	{"hypnotic_pulse", "Preset.Hypnotic",
	 "{\"mode\":3,\"bpm\":80.0,\"bars\":2.0,\"direction\":1,\"start_deg\":0.0,\"distance_m\":1.2,"
	 "\"front_back\":60.0,\"elevation\":25.0,\"binaural\":85.0,\"room\":35.0,\"near_ears\":20.0,\"mix\":100.0}"},
};

static const struct sd_presets_def presets_def = {
	preset_keys,
	sizeof(preset_keys) / sizeof(preset_keys[0]),
	builtin_presets,
	sizeof(builtin_presets) / sizeof(builtin_presets[0]),
};

struct orbit_filter {
	obs_source_t *context; /* DEBE ser el primer miembro (lo usa sd_presets) */
	orb_state dsp;
	int dsp_ok;
	orb_params slots[2];
	sd_atomic_long active;
	sd_atomic_long version;
	long seen_version;
	size_t channels;
};

static const char *of_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return T_("Orbit");
}

static void of_update(void *data, obs_data_t *s)
{
	struct orbit_filter *f = data;
	orb_params p;
	orb_default_params(&p);
	p.mode = (int)obs_data_get_int(s, S_MODE);
	if (p.mode == ORB_TEMPO)
		p.period_s = orb_tempo_period((float)obs_data_get_double(s, S_BPM), (float)obs_data_get_double(s, S_BARS));
	else if (obs_data_get_int(s, S_UNIT) == 1) {
		double hz = obs_data_get_double(s, S_RATE);
		p.period_s = (float)(1.0 / (hz < 0.001 ? 0.001 : hz));
	} else
		p.period_s = (float)obs_data_get_double(s, S_PERIOD);
	p.dir = obs_data_get_int(s, S_DIR) == 1 ? -1 : 1;
	p.start_deg = (float)obs_data_get_double(s, S_START);
	p.arc_deg = (float)obs_data_get_double(s, S_ARC);
	p.near_ears = (float)obs_data_get_double(s, S_NEAR) / 100.0f;
	p.elevation = (float)obs_data_get_double(s, S_ELEV) / 100.0f;
	p.binaural = (float)obs_data_get_double(s, S_BIN) / 100.0f;
	p.front_back = (float)obs_data_get_double(s, S_FB) / 100.0f;
	p.distance_m = (float)obs_data_get_double(s, S_DIST);
	p.room = (float)obs_data_get_double(s, S_ROOM) / 100.0f;
	p.mono_in = obs_data_get_int(s, S_INPUT) == 0;
	p.mix = (float)obs_data_get_double(s, S_MIX) / 100.0f;
	p.out_gain = sd_db_to_lin((float)obs_data_get_double(s, S_GAIN));

	/* doble buffer sin bloqueos: escribe en la ranura inactiva y la publica */
	long next = 1 - sd_atomic_load(&f->active);
	f->slots[next] = p;
	sd_atomic_store(&f->active, next);
	sd_atomic_inc(&f->version);
}

static void *of_create(obs_data_t *settings, obs_source_t *source)
{
	struct orbit_filter *f = bzalloc(sizeof(*f));
	f->context = source;
	audio_t *audio = obs_get_audio();
	f->channels = audio_output_get_channels(audio);
	f->dsp_ok = orb_init(&f->dsp, (float)audio_output_get_sample_rate(audio)) == 0;
	if (!f->dsp_ok)
		blog(LOG_ERROR, "[%s] no se pudo reservar memoria", PLUGIN_NAME);
	f->seen_version = -1;
	of_update(f, settings);
	return f;
}

static void of_destroy(void *data)
{
	struct orbit_filter *f = data;
	orb_free(&f->dsp);
	bfree(f);
}

static struct obs_audio_data *of_filter_audio(void *data, struct obs_audio_data *audio)
{
	struct orbit_filter *f = data;
	if (!f->dsp_ok || !audio || audio->frames == 0 || !audio->data[0])
		return audio;
	long v = sd_atomic_load(&f->version);
	if (v != f->seen_version) {
		orb_set_params(&f->dsp, &f->slots[sd_atomic_load(&f->active)]);
		f->seen_version = v;
	}
	float *l = (float *)audio->data[0];
	float *r = f->channels >= 2 ? (float *)audio->data[1] : NULL;
	/* canales 3+ (5.1, 7.1) pasan sin tocar */
	orb_process(&f->dsp, l, r, audio->frames);
	return audio;
}

static void of_defaults(obs_data_t *s)
{
	sd_presets_defaults(s);
	obs_data_set_default_int(s, S_MODE, ORB_ORBIT);
	obs_data_set_default_int(s, S_UNIT, 0);
	obs_data_set_default_double(s, S_PERIOD, 12.0);
	obs_data_set_default_double(s, S_RATE, 0.08);
	obs_data_set_default_double(s, S_BPM, 90.0);
	obs_data_set_default_double(s, S_BARS, 4.0);
	obs_data_set_default_int(s, S_DIR, 0);
	obs_data_set_default_double(s, S_START, -90.0);
	obs_data_set_default_double(s, S_ARC, 180.0);
	obs_data_set_default_double(s, S_NEAR, 30.0);
	obs_data_set_default_double(s, S_ELEV, 20.0);
	obs_data_set_default_double(s, S_BIN, 85.0);
	obs_data_set_default_double(s, S_FB, 60.0);
	obs_data_set_default_double(s, S_DIST, 0.6);
	obs_data_set_default_double(s, S_ROOM, 20.0);
	obs_data_set_default_int(s, S_INPUT, 0);
	obs_data_set_default_double(s, S_MIX, 100.0);
	obs_data_set_default_double(s, S_GAIN, 0.0);
}

static void vis(obs_properties_t *props, const char *key, bool on)
{
	obs_property_t *p = obs_properties_get(props, key);
	if (p)
		obs_property_set_visible(p, on);
}

static bool visibility_changed(obs_properties_t *props, obs_property_t *p, obs_data_t *s)
{
	UNUSED_PARAMETER(p);
	int mode = (int)obs_data_get_int(s, S_MODE);
	bool tempo = mode == ORB_TEMPO;
	bool hz = obs_data_get_int(s, S_UNIT) == 1;
	vis(props, S_UNIT, !tempo);
	vis(props, S_PERIOD, !tempo && !hz);
	vis(props, S_RATE, !tempo && hz);
	vis(props, S_BPM, tempo);
	vis(props, S_BARS, tempo);
	vis(props, S_ARC, mode == ORB_ARC);
	vis(props, S_START, mode != ORB_WANDER); /* la deriva aleatoria no tiene punto de partida fijo */
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

static obs_properties_t *of_properties(void *data)
{
	UNUSED_PARAMETER(data);
	obs_properties_t *props = obs_properties_create();
	obs_properties_t *g;
	obs_property_t *p;

	/* 1. Presets */
	sd_presets_add_properties(props, &presets_def);

	/* 2. Movimiento */
	g = obs_properties_create();
	p = obs_properties_add_list(g, S_MODE, T_("Mode"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, T_("Mode.Orbit"), ORB_ORBIT);
	obs_property_list_add_int(p, T_("Mode.Arc"), ORB_ARC);
	obs_property_list_add_int(p, T_("Mode.Wander"), ORB_WANDER);
	obs_property_list_add_int(p, T_("Mode.Tempo"), ORB_TEMPO);
	help(p, "Mode.Help");
	obs_property_set_modified_callback(p, visibility_changed);
	p = obs_properties_add_list(g, S_UNIT, T_("SpeedUnit"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, T_("SpeedUnit.Seconds"), 0);
	obs_property_list_add_int(p, T_("SpeedUnit.Hz"), 1);
	help(p, "SpeedUnit.Help");
	obs_property_set_modified_callback(p, visibility_changed);
	slider(g, S_PERIOD, "Period", 2.0, 120.0, 0.5, " s");
	slider(g, S_RATE, "Rate", 0.008, 0.5, 0.001, " Hz");
	slider(g, S_BPM, "Bpm", 40.0, 240.0, 1.0, " BPM");
	p = obs_properties_add_list(g, S_BARS, T_("Bars"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_FLOAT);
	obs_property_list_add_float(p, T_("Bars.Half"), 0.5);
	obs_property_list_add_float(p, T_("Bars.1"), 1.0);
	obs_property_list_add_float(p, T_("Bars.2"), 2.0);
	obs_property_list_add_float(p, T_("Bars.4"), 4.0);
	obs_property_list_add_float(p, T_("Bars.8"), 8.0);
	obs_property_list_add_float(p, T_("Bars.16"), 16.0);
	help(p, "Bars.Help");
	p = obs_properties_add_list(g, S_DIR, T_("Direction"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, T_("Direction.CW"), 0);
	obs_property_list_add_int(p, T_("Direction.CCW"), 1);
	help(p, "Direction.Help");
	slider(g, S_START, "StartAngle", -180.0, 180.0, 1.0, " °");
	slider(g, S_ARC, "ArcWidth", 10.0, 360.0, 1.0, " °");
	slider(g, S_NEAR, "NearEars", 0.0, 100.0, 1.0, " %");
	slider(g, S_ELEV, "Elevation", 0.0, 100.0, 1.0, " %");
	obs_properties_add_group(props, "grp_motion", T_("Group.Motion"), OBS_GROUP_NORMAL, g);

	/* 3. Cabeza / binaural */
	g = obs_properties_create();
	slider(g, S_BIN, "Binaural", 0.0, 100.0, 1.0, " %");
	slider(g, S_FB, "FrontBack", 0.0, 100.0, 1.0, " %");
	obs_properties_add_group(props, "grp_head", T_("Group.Head"), OBS_GROUP_NORMAL, g);

	/* 4. Distancia y sala */
	g = obs_properties_create();
	slider(g, S_DIST, "Distance", 0.25, 4.0, 0.05, " m");
	slider(g, S_ROOM, "Room", 0.0, 100.0, 1.0, " %");
	obs_properties_add_group(props, "grp_space", T_("Group.Space"), OBS_GROUP_NORMAL, g);

	/* 5. Entrada y mezcla */
	g = obs_properties_create();
	p = obs_properties_add_list(g, S_INPUT, T_("InputMode"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, T_("InputMode.Mono"), 0);
	obs_property_list_add_int(p, T_("InputMode.Stereo"), 1);
	help(p, "InputMode.Help");
	slider(g, S_MIX, "DryWet", 0.0, 100.0, 1.0, " %");
	slider(g, S_GAIN, "OutputGain", -24.0, 12.0, 0.1, " dB");
	obs_properties_add_group(props, "grp_mix", T_("Group.Mix"), OBS_GROUP_NORMAL, g);

	return props;
}

static struct obs_source_info orbit_filter_info = {
	.id = "streamerdesk_asmr_8d_orbit",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_AUDIO,
	.get_name = of_get_name,
	.create = of_create,
	.destroy = of_destroy,
	.update = of_update,
	.filter_audio = of_filter_audio,
	.get_defaults = of_defaults,
	.get_properties = of_properties,
};

const char *obs_module_name(void)
{
	return "Streamerdesk ASMR - 8D / Orbita";
}

const char *obs_module_description(void)
{
	return T_("Description");
}

bool obs_module_load(void)
{
	obs_register_source(&orbit_filter_info);
	blog(LOG_INFO, "[%s] cargado (version %s)", PLUGIN_NAME, PLUGIN_VERSION);
	return true;
}
