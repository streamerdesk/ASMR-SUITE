/*
 * Streamerdesk ASMR Suite - Eco Ping-Pong (filtro de audio para OBS Studio)
 * Copyright (c) 2026 Streamerdesk. GPL-2.0-or-later.
 *
 * Controles inspirados en TAL-Dub 1 (Togu Audio Line, 2007): DELAY L/R + LINK,
 * DAMP, RESONANCE, LOW CUT, FB L/R + LINK, DRY/WET, FILTER FB, IN, SWAP L/R,
 * limitador/distorsion en la realimentacion. Anadidos: ping-pong, balance,
 * modo de entrada mono/estereo, lado inicial, ganancia de salida, tempo en BPM.
 */
#include <obs-module.h>
#include "sd_atomic.h"
#include <util/bmem.h>
#include "pingpong_dsp.h"
#include "sd_presets.h"
#include <stdio.h>

#ifndef PLUGIN_NAME
#define PLUGIN_NAME "streamerdesk-asmr-pingpong-echo"
#endif
#ifndef PLUGIN_VERSION
#define PLUGIN_VERSION "1.0.1"
#endif

OBS_DECLARE_MODULE()
OBS_MODULE_AUTHOR("Streamerdesk")
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

#define T_(s) obs_module_text(s)

/* claves de ajustes */
#define S_DELAY_LINK "delay_link"
#define S_SYNC "sync_mode"
#define S_DELAY_L "delay_l_ms"
#define S_DELAY_R "delay_r_ms"
#define S_BPM "bpm"
#define S_DIV_L "div_l"
#define S_DIV_R "div_r"
#define S_DAMP "damp_hz"
#define S_RES "resonance"
#define S_LOWCUT "lowcut_hz"
#define S_FILTER_FB "filter_fb"
#define S_FILTER_IN "filter_in"
#define S_FB_LINK "fb_link"
#define S_FB_L "fb_l"
#define S_FB_R "fb_r"
#define S_SATURATE "saturate"
#define S_PINGPONG "pingpong"
#define S_START "start_side"
#define S_INPUT "input_mode"
#define S_SWAP "swap_lr"
#define S_MIX "mix"
#define S_BALANCE "balance"
#define S_GAIN "out_gain_db"

/* divisiones de nota en negras (beats) */
static const struct {
	const char *label;
	double beats;
} divisions[] = {
	{"1/32", 0.125}, {"1/16T", 1.0 / 6.0}, {"1/16", 0.25},     {"1/16.", 0.375}, {"1/8T", 1.0 / 3.0},
	{"1/8", 0.5},    {"1/8.", 0.75},       {"1/4T", 2.0 / 3.0}, {"1/4", 1.0},     {"1/4.", 1.5},
	{"1/2T", 4.0 / 3.0}, {"1/2", 2.0},     {"1/2.", 3.0},      {"1/1", 4.0},     {"2/1", 8.0},
};
#define NUM_DIVS (sizeof(divisions) / sizeof(divisions[0]))
#define DEFAULT_DIV_L 6 /* 1/8. */
#define DEFAULT_DIV_R 6

/* claves que forman parte de un preset */
static const char *const preset_keys[] = {
	S_DELAY_LINK, S_SYNC, S_DELAY_L, S_DELAY_R, S_BPM,     S_DIV_L,   S_DIV_R,    S_DAMP,
	S_RES,        S_LOWCUT, S_FILTER_FB, S_FILTER_IN, S_FB_LINK, S_FB_L,  S_FB_R,     S_SATURATE,
	S_PINGPONG,   S_START, S_INPUT,   S_SWAP,      S_MIX,     S_BALANCE, S_GAIN,
};

/* presets integrados: pocos y representativos; editables y guardables como preset propio */
static const struct sd_builtin_preset builtin_presets[] = {
	{"asmr_soft", "Preset.AsmrSoft",
	 "{\"delay_l_ms\":280.0,\"fb_l\":30.0,\"damp_hz\":3500.0,\"resonance\":5.0,"
	 "\"lowcut_hz\":150.0,\"mix\":22.0,\"pingpong\":100.0}"},
	{"dub_classic", "Preset.DubClassic",
	 "{\"sync_mode\":1,\"bpm\":90.0,\"div_l\":6,\"fb_l\":68.0,\"damp_hz\":2200.0,"
	 "\"resonance\":55.0,\"lowcut_hz\":120.0,\"mix\":38.0,\"pingpong\":100.0,\"saturate\":true}"},
	{"wide_bounce", "Preset.WideBounce",
	 "{\"delay_link\":false,\"delay_l_ms\":300.0,\"delay_r_ms\":450.0,\"fb_l\":50.0,"
	 "\"damp_hz\":6500.0,\"resonance\":10.0,\"lowcut_hz\":100.0,\"mix\":35.0,\"pingpong\":100.0}"},
	{"binaural_wrap", "Preset.BinauralWrap",
	 "{\"delay_link\":false,\"delay_l_ms\":23.0,\"delay_r_ms\":31.0,\"fb_l\":35.0,"
	 "\"damp_hz\":7000.0,\"resonance\":0.0,\"lowcut_hz\":150.0,\"mix\":30.0,\"pingpong\":70.0}"},
};

static const struct sd_presets_def presets_def = {
	preset_keys,
	sizeof(preset_keys) / sizeof(preset_keys[0]),
	builtin_presets,
	sizeof(builtin_presets) / sizeof(builtin_presets[0]),
};

struct pp_filter {
	obs_source_t *context; /* DEBE ser el primer miembro (lo usa sd_presets) */
	ppe_state dsp;
	int dsp_ok;
	ppe_params slots[2];
	sd_atomic_long active;
	sd_atomic_long version;
	long seen_version;
	size_t channels;
};

static const char *pp_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return T_("PingPongEcho");
}

static double div_ms(double bpm, long long idx)
{
	if (idx < 0 || (size_t)idx >= NUM_DIVS)
		idx = DEFAULT_DIV_L;
	if (bpm < 20.0)
		bpm = 20.0;
	return 60000.0 / bpm * divisions[idx].beats;
}

static void pp_update(void *data, obs_data_t *settings)
{
	struct pp_filter *f = data;
	ppe_params p;
	ppe_default_params(&p);

	bool sync = obs_data_get_int(settings, S_SYNC) == 1;
	bool dlink = obs_data_get_bool(settings, S_DELAY_LINK);
	if (sync) {
		double bpm = obs_data_get_double(settings, S_BPM);
		long long dl = obs_data_get_int(settings, S_DIV_L);
		long long dr = dlink ? dl : obs_data_get_int(settings, S_DIV_R);
		p.delay_l_ms = (float)div_ms(bpm, dl);
		p.delay_r_ms = (float)div_ms(bpm, dr);
	} else {
		p.delay_l_ms = (float)obs_data_get_double(settings, S_DELAY_L);
		p.delay_r_ms = dlink ? p.delay_l_ms : (float)obs_data_get_double(settings, S_DELAY_R);
	}

	p.damp_hz = (float)obs_data_get_double(settings, S_DAMP);
	p.resonance = (float)obs_data_get_double(settings, S_RES) / 100.0f;
	p.lowcut_hz = (float)obs_data_get_double(settings, S_LOWCUT);
	p.filter_fb = obs_data_get_bool(settings, S_FILTER_FB);
	p.filter_in = obs_data_get_bool(settings, S_FILTER_IN);

	bool flink = obs_data_get_bool(settings, S_FB_LINK);
	p.fb_l = (float)obs_data_get_double(settings, S_FB_L) / 100.0f;
	p.fb_r = flink ? p.fb_l : (float)obs_data_get_double(settings, S_FB_R) / 100.0f;
	p.saturate = obs_data_get_bool(settings, S_SATURATE);

	p.pingpong = (float)obs_data_get_double(settings, S_PINGPONG) / 100.0f;
	p.start_right = obs_data_get_int(settings, S_START) == 1;
	p.mono_in = obs_data_get_int(settings, S_INPUT) == 0;
	p.swap_lr = obs_data_get_bool(settings, S_SWAP);

	p.mix = (float)obs_data_get_double(settings, S_MIX) / 100.0f;
	p.balance = (float)obs_data_get_double(settings, S_BALANCE) / 100.0f;
	p.out_gain = sd_db_to_lin((float)obs_data_get_double(settings, S_GAIN));

	/* doble buffer sin bloqueos: escribe en la ranura inactiva y la publica */
	long next = 1 - sd_atomic_load(&f->active);
	f->slots[next] = p;
	sd_atomic_store(&f->active, next);
	sd_atomic_inc(&f->version);
}

static void *pp_create(obs_data_t *settings, obs_source_t *source)
{
	struct pp_filter *f = bzalloc(sizeof(*f));
	f->context = source;
	audio_t *audio = obs_get_audio();
	uint32_t sr = audio_output_get_sample_rate(audio);
	f->channels = audio_output_get_channels(audio);
	f->dsp_ok = ppe_init(&f->dsp, (float)sr) == 0;
	if (!f->dsp_ok)
		blog(LOG_ERROR, "[%s] no se pudo reservar la memoria del retardo", PLUGIN_NAME);
	f->seen_version = -1;
	pp_update(f, settings);
	return f;
}

static void pp_destroy(void *data)
{
	struct pp_filter *f = data;
	ppe_free(&f->dsp);
	bfree(f);
}

static struct obs_audio_data *pp_filter_audio(void *data, struct obs_audio_data *audio)
{
	struct pp_filter *f = data;
	if (!f->dsp_ok || !audio || audio->frames == 0)
		return audio;

	long v = sd_atomic_load(&f->version);
	if (v != f->seen_version) {
		long idx = sd_atomic_load(&f->active);
		ppe_set_params(&f->dsp, &f->slots[idx]);
		f->seen_version = v;
	}

	float *l = (float *)audio->data[0];
	float *r = f->channels >= 2 ? (float *)audio->data[1] : NULL;
	if (!l)
		return audio;
	/* canales 3+ (5.1, 7.1) pasan sin tocar */
	ppe_process(&f->dsp, l, r, audio->frames);
	return audio;
}

static void pp_defaults(obs_data_t *s)
{
	sd_presets_defaults(s);
	obs_data_set_default_bool(s, S_DELAY_LINK, true);
	obs_data_set_default_int(s, S_SYNC, 0);
	obs_data_set_default_double(s, S_DELAY_L, 375.0);
	obs_data_set_default_double(s, S_DELAY_R, 375.0);
	obs_data_set_default_double(s, S_BPM, 120.0);
	obs_data_set_default_int(s, S_DIV_L, DEFAULT_DIV_L);
	obs_data_set_default_int(s, S_DIV_R, DEFAULT_DIV_R);
	obs_data_set_default_double(s, S_DAMP, 4500.0);
	obs_data_set_default_double(s, S_RES, 15.0);
	obs_data_set_default_double(s, S_LOWCUT, 100.0);
	obs_data_set_default_bool(s, S_FILTER_FB, true);
	obs_data_set_default_bool(s, S_FILTER_IN, false);
	obs_data_set_default_bool(s, S_FB_LINK, true);
	obs_data_set_default_double(s, S_FB_L, 45.0);
	obs_data_set_default_double(s, S_FB_R, 45.0);
	obs_data_set_default_bool(s, S_SATURATE, true);
	obs_data_set_default_double(s, S_PINGPONG, 100.0);
	obs_data_set_default_int(s, S_START, 0);
	obs_data_set_default_int(s, S_INPUT, 0);
	obs_data_set_default_bool(s, S_SWAP, false);
	obs_data_set_default_double(s, S_MIX, 30.0);
	obs_data_set_default_double(s, S_BALANCE, 0.0);
	obs_data_set_default_double(s, S_GAIN, 0.0);
}

static bool visibility_changed(obs_properties_t *props, obs_property_t *p, obs_data_t *s)
{
	UNUSED_PARAMETER(p);
	bool sync = obs_data_get_int(s, S_SYNC) == 1;
	bool dlink = obs_data_get_bool(s, S_DELAY_LINK);
	bool flink = obs_data_get_bool(s, S_FB_LINK);
	obs_property_set_visible(obs_properties_get(props, S_DELAY_L), !sync);
	obs_property_set_visible(obs_properties_get(props, S_DELAY_R), !sync && !dlink);
	obs_property_set_visible(obs_properties_get(props, S_BPM), sync);
	obs_property_set_visible(obs_properties_get(props, S_DIV_L), sync);
	obs_property_set_visible(obs_properties_get(props, S_DIV_R), sync && !dlink);
	obs_property_set_visible(obs_properties_get(props, S_FB_R), !flink);
	/* SWAP L/R solo tiene efecto con entrada estereo */
	obs_property_set_visible(obs_properties_get(props, S_SWAP), obs_data_get_int(s, S_INPUT) == 1);
	return true;
}

/* ayuda (?) de cada control: clave de locale "<Etiqueta>.Help" */
static obs_property_t *help(obs_property_t *p, const char *help_key)
{
	obs_property_set_long_description(p, T_(help_key));
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

static obs_properties_t *pp_properties(void *data)
{
	UNUSED_PARAMETER(data);
	obs_properties_t *props = obs_properties_create();
	obs_property_t *p;
	obs_properties_t *g;

	/* --- 1. PRESETS --- */
	sd_presets_add_properties(props, &presets_def);

	/* --- 2. ECO: tiempo + realimentacion (DELAY / FB de TAL-Dub) --- */
	g = obs_properties_create();
	p = obs_properties_add_list(g, S_SYNC, T_("SyncMode"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, T_("SyncMode.Manual"), 0);
	obs_property_list_add_int(p, T_("SyncMode.Tempo"), 1);
	obs_property_set_modified_callback(p, visibility_changed);
	help(p, "SyncMode.Help");
	p = obs_properties_add_bool(g, S_DELAY_LINK, T_("DelayLink"));
	obs_property_set_modified_callback(p, visibility_changed);
	help(p, "DelayLink.Help");
	slider(g, S_DELAY_L, "DelayL", 1.0, PPE_MAX_DELAY_MS, 1.0, " ms");
	slider(g, S_DELAY_R, "DelayR", 1.0, PPE_MAX_DELAY_MS, 1.0, " ms");
	slider(g, S_BPM, "BPM", 30.0, 300.0, 0.1, " BPM");
	obs_property_t *dl = obs_properties_add_list(g, S_DIV_L, T_("DivL"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_t *dr = obs_properties_add_list(g, S_DIV_R, T_("DivR"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	for (size_t i = 0; i < NUM_DIVS; i++) {
		obs_property_list_add_int(dl, divisions[i].label, (long long)i);
		obs_property_list_add_int(dr, divisions[i].label, (long long)i);
	}
	help(dl, "DivL.Help");
	help(dr, "DivR.Help");
	p = obs_properties_add_bool(g, S_FB_LINK, T_("FeedbackLink"));
	obs_property_set_modified_callback(p, visibility_changed);
	help(p, "FeedbackLink.Help");
	slider(g, S_FB_L, "FeedbackL", 0.0, 120.0, 0.5, " %");
	slider(g, S_FB_R, "FeedbackR", 0.0, 120.0, 0.5, " %");
	help(obs_properties_add_bool(g, S_SATURATE, T_("Saturate")), "Saturate.Help");
	obs_properties_add_group(props, "grp_echo", T_("Group.Echo"), OBS_GROUP_NORMAL, g);

	/* --- 3. ESTEREO / BINAURAL --- */
	g = obs_properties_create();
	slider(g, S_PINGPONG, "PingPong", 0.0, 100.0, 1.0, " %");
	p = obs_properties_add_list(g, S_START, T_("StartSide"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, T_("StartSide.Left"), 0);
	obs_property_list_add_int(p, T_("StartSide.Right"), 1);
	help(p, "StartSide.Help");
	p = obs_properties_add_list(g, S_INPUT, T_("InputMode"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, T_("InputMode.Mono"), 0);
	obs_property_list_add_int(p, T_("InputMode.Stereo"), 1);
	obs_property_set_modified_callback(p, visibility_changed);
	help(p, "InputMode.Help");
	help(obs_properties_add_bool(g, S_SWAP, T_("SwapLR")), "SwapLR.Help");
	obs_properties_add_group(props, "grp_stereo", T_("Group.Stereo"), OBS_GROUP_NORMAL, g);

	/* --- 4. TONO: filtro (DAMP / RESONANCE / LOW CUT / FILTER FB / IN de TAL-Dub) --- */
	g = obs_properties_create();
	slider(g, S_DAMP, "Damp", 200.0, 20000.0, 10.0, " Hz");
	slider(g, S_RES, "Resonance", 0.0, 100.0, 1.0, " %");
	slider(g, S_LOWCUT, "LowCut", 10.0, 1000.0, 1.0, " Hz");
	help(obs_properties_add_bool(g, S_FILTER_FB, T_("FilterFB")), "FilterFB.Help");
	help(obs_properties_add_bool(g, S_FILTER_IN, T_("FilterIn")), "FilterIn.Help");
	obs_properties_add_group(props, "grp_tone", T_("Group.Tone"), OBS_GROUP_NORMAL, g);

	/* --- 5. MEZCLA --- */
	g = obs_properties_create();
	slider(g, S_MIX, "DryWet", 0.0, 100.0, 1.0, " %");
	slider(g, S_BALANCE, "Balance", -100.0, 100.0, 1.0, " %");
	slider(g, S_GAIN, "OutputGain", -24.0, 12.0, 0.1, " dB");
	obs_properties_add_group(props, "grp_mix", T_("Group.Mix"), OBS_GROUP_NORMAL, g);

	return props;
}

static struct obs_source_info pingpong_echo_filter = {
	.id = "streamerdesk_asmr_pingpong_echo",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_AUDIO,
	.get_name = pp_get_name,
	.create = pp_create,
	.destroy = pp_destroy,
	.update = pp_update,
	.filter_audio = pp_filter_audio,
	.get_defaults = pp_defaults,
	.get_properties = pp_properties,
};

const char *obs_module_name(void)
{
	return "Streamerdesk ASMR - Eco Ping-Pong";
}

const char *obs_module_description(void)
{
	return T_("Description");
}

bool obs_module_load(void)
{
	obs_register_source(&pingpong_echo_filter);
	blog(LOG_INFO, "[%s] cargado (version %s)", PLUGIN_NAME, PLUGIN_VERSION);
	return true;
}
