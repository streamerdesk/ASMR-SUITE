/*
 * Streamerdesk ASMR Suite - Proximidad (filtro de audio para OBS Studio)
 * Copyright (c) 2026 Streamerdesk. GPL-2.0-or-later.
 *
 * Acerca o aleja la voz del oido del oyente (pensado para auriculares) sin que el
 * creador se mueva: efecto proximidad, aire, perdida de agudos y volumen con la
 * distancia, ambiente de habitacion, posicion binaural en un oido, calidez y
 * suavizado de eses. Compatible con OBS Studio 28.0 y posteriores (solo API de 28.0).
 */
#include <obs-module.h>
#include <util/bmem.h>
#include "sd_presets.h"
#include "proximity_dsp.h"
#include "sd_dsp.h"
#include <stdio.h>
#include <string.h>

#ifndef PLUGIN_NAME
#define PLUGIN_NAME "streamerdesk-asmr-proximity"
#endif
#ifndef PLUGIN_VERSION
#define PLUGIN_VERSION "1.0.0"
#endif

OBS_DECLARE_MODULE()
OBS_MODULE_AUTHOR("Streamerdesk")
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

#define T_(s) obs_module_text(s)

/* ---- cerrojo minimo sin bloqueo para el hilo de audio ---- */
#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
typedef volatile long prox_lock_t;
#define LOCK_XCHG(p, v) _InterlockedExchange((p), (v))
#define LOCK_LOAD(p) _InterlockedOr((p), 0)
#define LOCK_STORE(p, v) _InterlockedExchange((p), (v))
#else
typedef volatile long prox_lock_t;
#define LOCK_XCHG(p, v) __atomic_exchange_n((p), (v), __ATOMIC_ACQ_REL)
#define LOCK_LOAD(p) __atomic_load_n((p), __ATOMIC_ACQUIRE)
#define LOCK_STORE(p, v) __atomic_store_n((p), (v), __ATOMIC_RELEASE)
#endif

/* claves de ajustes */
#define S_DISTANCE "distance"
#define S_LEVEL_LOSS "level_loss"
#define S_HF_LOSS "hf_loss"
#define S_ROOM "room"
#define S_PROX_AMOUNT "prox_amount"
#define S_PROX_FREQ "prox_freq"
#define S_AIR "air_db"
#define S_WARMTH "warmth"
#define S_DEHARSH "deharsh"
#define S_DEHARSH_AMT "deharsh_amt"
#define S_LOWCUT "lowcut_hz"
#define S_POS_MODE "position_mode"
#define S_POSITION "position"
#define S_BINAURAL "binaural"
#define S_INPUT "input_mode"
#define S_MIX "mix"
#define S_GAIN "out_gain_db"

enum { POS_CENTER = 0, POS_LEFT = 1, POS_RIGHT = 2, POS_CUSTOM = 3 };

static const char *const preset_keys[] = {
	S_DISTANCE, S_LEVEL_LOSS, S_HF_LOSS, S_ROOM,     S_PROX_AMOUNT, S_PROX_FREQ, S_AIR,   S_WARMTH, S_DEHARSH,
	S_DEHARSH_AMT, S_LOWCUT, S_POS_MODE, S_POSITION, S_BINAURAL,    S_INPUT,     S_MIX,   S_GAIN,
};

/* presets de fabrica aprobados: ensenan lo que hace el efecto; cada creador los ajusta y guarda los suyos */
static const struct sd_builtin_preset builtin_presets[] = {
	{"al_oido", "Preset.AlOido",
	 "{\"distance\":5.0,\"level_loss\":60.0,\"hf_loss\":60.0,\"room\":8.0,\"prox_amount\":110.0,"
	 "\"prox_freq\":150.0,\"air_db\":1.0,\"warmth\":25.0,\"deharsh\":true,\"deharsh_amt\":50.0,"
	 "\"lowcut_hz\":70.0,\"position_mode\":0,\"binaural\":true,\"input_mode\":0,\"mix\":100.0,"
	 "\"out_gain_db\":-1.0}"},
	{"cercano_suave", "Preset.CercanoSuave",
	 "{\"distance\":30.0,\"level_loss\":20.0,\"hf_loss\":50.0,\"room\":12.0,\"prox_amount\":60.0,"
	 "\"prox_freq\":170.0,\"air_db\":0.5,\"warmth\":10.0,\"deharsh\":true,\"deharsh_amt\":30.0,"
	 "\"lowcut_hz\":80.0,\"position_mode\":0,\"binaural\":true,\"input_mode\":0,\"mix\":100.0,"
	 "\"out_gain_db\":1.0}"},
	{"media_distancia", "Preset.MediaDistancia",
	 "{\"distance\":65.0,\"level_loss\":50.0,\"hf_loss\":45.0,\"room\":40.0,\"prox_amount\":80.0,"
	 "\"prox_freq\":160.0,\"air_db\":2.5,\"warmth\":5.0,\"deharsh\":false,\"deharsh_amt\":40.0,"
	 "\"lowcut_hz\":100.0,\"position_mode\":0,\"binaural\":true,\"input_mode\":0,\"mix\":100.0,"
	 "\"out_gain_db\":3.0}"},
	{"micro_contacto", "Preset.MicroContacto",
	 "{\"distance\":0.0,\"level_loss\":0.0,\"hf_loss\":0.0,\"room\":0.0,\"prox_amount\":160.0,"
	 "\"prox_freq\":120.0,\"air_db\":-1.5,\"warmth\":35.0,\"deharsh\":true,\"deharsh_amt\":60.0,"
	 "\"lowcut_hz\":30.0,\"position_mode\":0,\"binaural\":true,\"input_mode\":0,\"mix\":100.0,"
	 "\"out_gain_db\":-4.0}"},
};

static const struct sd_presets_def presets_def = {
	preset_keys,
	sizeof(preset_keys) / sizeof(preset_keys[0]),
	builtin_presets,
	sizeof(builtin_presets) / sizeof(builtin_presets[0]),
};

struct prox_filter {
	obs_source_t *context; /* DEBE ser el primer miembro (lo usa sd_presets) */
	prox_state dsp;
	int dsp_ok;
	size_t channels;
	/* parametros nuevos: los escribe update() y los recoge el hilo de audio sin esperar */
	prox_lock_t lock;
	prox_lock_t pending;
	prox_params next;
};

static const char *prox_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return T_("Proximity");
}

static void read_params(obs_data_t *s, prox_params *p)
{
	prox_default_params(p);
	p->distance = (float)obs_data_get_double(s, S_DISTANCE) / 100.0f;
	p->level_loss = (float)obs_data_get_double(s, S_LEVEL_LOSS) / 100.0f;
	p->hf_loss = (float)obs_data_get_double(s, S_HF_LOSS) / 100.0f;
	p->room = (float)obs_data_get_double(s, S_ROOM) / 100.0f;
	p->prox_amount = (float)obs_data_get_double(s, S_PROX_AMOUNT) / 100.0f;
	p->prox_freq = (float)obs_data_get_double(s, S_PROX_FREQ);
	p->air_db = (float)obs_data_get_double(s, S_AIR);
	p->warmth = (float)obs_data_get_double(s, S_WARMTH) / 100.0f;
	p->deharsh = obs_data_get_bool(s, S_DEHARSH);
	p->deharsh_amt = (float)obs_data_get_double(s, S_DEHARSH_AMT) / 100.0f;
	p->lowcut_hz = (float)obs_data_get_double(s, S_LOWCUT);
	switch ((int)obs_data_get_int(s, S_POS_MODE)) {
	case POS_LEFT:
		p->position = -1.0f;
		break;
	case POS_RIGHT:
		p->position = 1.0f;
		break;
	case POS_CUSTOM:
		p->position = (float)obs_data_get_double(s, S_POSITION) / 100.0f;
		break;
	default:
		p->position = 0.0f;
		break;
	}
	p->binaural = obs_data_get_bool(s, S_BINAURAL);
	p->mono_in = obs_data_get_int(s, S_INPUT) == 0;
	p->mix = (float)obs_data_get_double(s, S_MIX) / 100.0f;
	p->out_gain = sd_db_to_lin((float)obs_data_get_double(s, S_GAIN));
}

static void prox_update(void *data, obs_data_t *settings)
{
	struct prox_filter *f = data;
	prox_params p;
	read_params(settings, &p);
	/* el hilo de la interfaz puede esperar un instante; el de audio nunca */
	while (LOCK_XCHG(&f->lock, 1) != 0)
		;
	f->next = p;
	LOCK_STORE(&f->pending, 1);
	LOCK_STORE(&f->lock, 0);
}

static void *prox_create(obs_data_t *settings, obs_source_t *source)
{
	struct prox_filter *f = bzalloc(sizeof(*f));
	f->context = source;
	audio_t *audio = obs_get_audio();
	uint32_t sr = audio_output_get_sample_rate(audio);
	f->channels = audio_output_get_channels(audio);
	f->dsp_ok = prox_init(&f->dsp, (float)sr) == 0;
	if (!f->dsp_ok)
		blog(LOG_ERROR, "[%s] no se pudo reservar memoria", PLUGIN_NAME);
	prox_params p;
	read_params(settings, &p);
	if (f->dsp_ok) {
		prox_set_params(&f->dsp, &p);
		prox_snap(&f->dsp); /* al crear, empieza ya en su sitio */
	}
	return f;
}

static void prox_destroy(void *data)
{
	struct prox_filter *f = data;
	prox_free(&f->dsp);
	bfree(f);
}

static struct obs_audio_data *prox_filter_audio(void *data, struct obs_audio_data *audio)
{
	struct prox_filter *f = data;
	if (!f->dsp_ok || !audio || audio->frames == 0)
		return audio;

	if (LOCK_LOAD(&f->pending) && LOCK_XCHG(&f->lock, 1) == 0) {
		prox_params p = f->next;
		LOCK_STORE(&f->pending, 0);
		LOCK_STORE(&f->lock, 0);
		prox_set_params(&f->dsp, &p);
	}

	float *l = (float *)audio->data[0];
	float *r = f->channels >= 2 ? (float *)audio->data[1] : NULL;
	if (!l)
		return audio;
	/* canales 3+ (5.1, 7.1) pasan sin tocar */
	prox_process(&f->dsp, l, r, audio->frames);
	return audio;
}

static void prox_defaults(obs_data_t *s)
{
	sd_presets_defaults(s);
	obs_data_set_default_double(s, S_DISTANCE, 20.0);
	obs_data_set_default_double(s, S_LEVEL_LOSS, 50.0);
	obs_data_set_default_double(s, S_HF_LOSS, 60.0);
	obs_data_set_default_double(s, S_ROOM, 20.0);
	obs_data_set_default_double(s, S_PROX_AMOUNT, 100.0);
	obs_data_set_default_double(s, S_PROX_FREQ, 160.0);
	obs_data_set_default_double(s, S_AIR, 1.0);
	obs_data_set_default_double(s, S_WARMTH, 15.0);
	obs_data_set_default_bool(s, S_DEHARSH, true);
	obs_data_set_default_double(s, S_DEHARSH_AMT, 40.0);
	obs_data_set_default_double(s, S_LOWCUT, 80.0);
	obs_data_set_default_int(s, S_POS_MODE, POS_CENTER);
	obs_data_set_default_double(s, S_POSITION, 0.0);
	obs_data_set_default_bool(s, S_BINAURAL, true);
	obs_data_set_default_int(s, S_INPUT, 0);
	obs_data_set_default_double(s, S_MIX, 100.0);
	obs_data_set_default_double(s, S_GAIN, 0.0);
}

/* oculta lo que no aplica en cada momento */
static bool visibility_changed(obs_properties_t *props, obs_property_t *p, obs_data_t *s)
{
	UNUSED_PARAMETER(p);
	int mode = (int)obs_data_get_int(s, S_POS_MODE);
	obs_property_set_visible(obs_properties_get(props, S_POSITION), mode == POS_CUSTOM);
	obs_property_set_visible(obs_properties_get(props, S_BINAURAL), mode != POS_CENTER);
	obs_property_set_visible(obs_properties_get(props, S_DEHARSH_AMT), obs_data_get_bool(s, S_DEHARSH));
	return true;
}

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

static obs_properties_t *prox_properties(void *data)
{
	UNUSED_PARAMETER(data);
	obs_properties_t *props = obs_properties_create();
	obs_properties_t *g;
	obs_property_t *p;

	/* 1. Presets */
	sd_presets_add_properties(props, &presets_def);

	/* 2. Distancia: el control principal */
	g = obs_properties_create();
	slider(g, S_DISTANCE, "Distance", 0.0, 100.0, 1.0, " %");
	slider(g, S_LEVEL_LOSS, "LevelLoss", 0.0, 100.0, 1.0, " %");
	slider(g, S_HF_LOSS, "HfLoss", 0.0, 100.0, 1.0, " %");
	slider(g, S_ROOM, "Room", 0.0, 100.0, 1.0, " %");
	obs_properties_add_group(props, "grp_distance", T_("Group.Distance"), OBS_GROUP_NORMAL, g);

	/* 3. Cercania y tono */
	g = obs_properties_create();
	slider(g, S_PROX_AMOUNT, "ProxAmount", 0.0, 200.0, 1.0, " %");
	slider(g, S_PROX_FREQ, "ProxFreq", 80.0, 300.0, 1.0, " Hz");
	slider(g, S_AIR, "Air", -6.0, 6.0, 0.1, " dB");
	slider(g, S_WARMTH, "Warmth", 0.0, 100.0, 1.0, " %");
	p = obs_properties_add_bool(g, S_DEHARSH, T_("Deharsh"));
	obs_property_set_modified_callback(p, visibility_changed);
	help(p, "Deharsh.Help");
	slider(g, S_DEHARSH_AMT, "DeharshAmount", 0.0, 100.0, 1.0, " %");
	slider(g, S_LOWCUT, "LowCut", 20.0, 300.0, 1.0, " Hz");
	obs_properties_add_group(props, "grp_tone", T_("Group.Tone"), OBS_GROUP_NORMAL, g);

	/* 4. Posicion (auriculares) */
	g = obs_properties_create();
	p = obs_properties_add_list(g, S_POS_MODE, T_("PositionMode"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, T_("PositionMode.Center"), POS_CENTER);
	obs_property_list_add_int(p, T_("PositionMode.Left"), POS_LEFT);
	obs_property_list_add_int(p, T_("PositionMode.Right"), POS_RIGHT);
	obs_property_list_add_int(p, T_("PositionMode.Custom"), POS_CUSTOM);
	obs_property_set_modified_callback(p, visibility_changed);
	help(p, "PositionMode.Help");
	slider(g, S_POSITION, "Position", -100.0, 100.0, 1.0, " %");
	help(obs_properties_add_bool(g, S_BINAURAL, T_("Binaural")), "Binaural.Help");
	p = obs_properties_add_list(g, S_INPUT, T_("InputMode"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, T_("InputMode.Mono"), 0);
	obs_property_list_add_int(p, T_("InputMode.Stereo"), 1);
	help(p, "InputMode.Help");
	obs_properties_add_group(props, "grp_position", T_("Group.Position"), OBS_GROUP_NORMAL, g);

	/* 5. Salida */
	g = obs_properties_create();
	slider(g, S_MIX, "Mix", 0.0, 100.0, 1.0, " %");
	slider(g, S_GAIN, "OutputGain", -24.0, 12.0, 0.1, " dB");
	obs_properties_add_group(props, "grp_output", T_("Group.Output"), OBS_GROUP_NORMAL, g);

	return props;
}

static struct obs_source_info proximity_filter = {
	.id = "streamerdesk_asmr_proximity",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_AUDIO,
	.get_name = prox_get_name,
	.create = prox_create,
	.destroy = prox_destroy,
	.update = prox_update,
	.filter_audio = prox_filter_audio,
	.get_defaults = prox_defaults,
	.get_properties = prox_properties,
};

const char *obs_module_name(void)
{
	return "Streamerdesk ASMR - Proximidad";
}

const char *obs_module_description(void)
{
	return T_("Description");
}

bool obs_module_load(void)
{
	obs_register_source(&proximity_filter);
	blog(LOG_INFO, "[%s] cargado (version %s)", PLUGIN_NAME, PLUGIN_VERSION);
	return true;
}
