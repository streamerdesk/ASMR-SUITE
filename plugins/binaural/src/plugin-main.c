/*
 * Streamerdesk ASMR Suite - Espacialidad binaural (filtro de audio para OBS Studio)
 * Copyright (c) 2026 Streamerdesk. GPL-2.0-or-later.
 *
 * Coloca la voz en un punto fijo del espacio 3D alrededor de la cabeza del oyente
 * (auriculares) y la desliza suavemente cuando cambias de posicion.
 * Compatible con OBS Studio 28.0 y posteriores (solo API de 28.0).
 */
#include <obs-module.h>
#include <util/bmem.h>
#include "sd_presets.h"
#include "binaural_dsp.h"
#include "sd_dsp.h"
#include <stdio.h>
#include <string.h>

#ifndef PLUGIN_NAME
#define PLUGIN_NAME "streamerdesk-asmr-binaural-spatializer"
#endif
#ifndef PLUGIN_VERSION
#define PLUGIN_VERSION "1.0.0"
#endif

OBS_DECLARE_MODULE()
OBS_MODULE_AUTHOR("Streamerdesk")
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

#define T_(s) obs_module_text(s)

/* cerrojo minimo: el hilo de audio solo lo intenta, nunca espera */
#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
typedef volatile long bin_lock_t;
#define LOCK_XCHG(p, v) _InterlockedExchange((p), (v))
#define LOCK_LOAD(p) _InterlockedOr((p), 0)
#define LOCK_STORE(p, v) _InterlockedExchange((p), (v))
#else
typedef volatile long bin_lock_t;
#define LOCK_XCHG(p, v) __atomic_exchange_n((p), (v), __ATOMIC_ACQ_REL)
#define LOCK_LOAD(p) __atomic_load_n((p), __ATOMIC_ACQUIRE)
#define LOCK_STORE(p, v) __atomic_store_n((p), (v), __ATOMIC_RELEASE)
#endif

#define S_QUICK "quick_pos"
#define S_AZ "azimuth"
#define S_EL "elevation"
#define S_DIST "distance"
#define S_GLIDE "glide_s"
#define S_CUES "cues"
#define S_PROX "prox"
#define S_AIR "air_loss"
#define S_LEVEL "level_loss"
#define S_ROOM "room"
#define S_INPUT "input_mode"
#define S_MIX "mix"
#define S_GAIN "out_gain_db"

enum { Q_LEFT = 0, Q_RIGHT, Q_FRONT, Q_BEHIND, Q_ABOVE, Q_CUSTOM };
static const float quick_az[] = {270.0f, 90.0f, 0.0f, 180.0f, 0.0f};
static const float quick_el[] = {0.0f, 0.0f, 0.0f, 0.0f, 90.0f};

static const char *const preset_keys[] = {
	S_QUICK, S_AZ, S_EL, S_DIST, S_GLIDE, S_CUES, S_PROX, S_AIR, S_LEVEL, S_ROOM, S_INPUT, S_MIX, S_GAIN,
};

/* presets de fabrica: ensenan lo que hace el efecto; cada creador los ajusta y guarda los suyos */
static const struct sd_builtin_preset builtin_presets[] = {
	{"oido_izquierdo", "Preset.LeftEar",
	 "{\"quick_pos\":0,\"azimuth\":270.0,\"elevation\":0.0,\"distance\":3.0,\"glide_s\":1.5,\"cues\":100.0,"
	 "\"prox\":100.0,\"air_loss\":40.0,\"level_loss\":40.0,\"room\":5.0,\"input_mode\":0,\"mix\":100.0,"
	 "\"out_gain_db\":-1.0}"},
	{"detras", "Preset.Behind",
	 "{\"quick_pos\":3,\"azimuth\":180.0,\"elevation\":0.0,\"distance\":30.0,\"glide_s\":1.5,\"cues\":140.0,"
	 "\"prox\":60.0,\"air_loss\":50.0,\"level_loss\":40.0,\"room\":15.0,\"input_mode\":0,\"mix\":100.0,"
	 "\"out_gain_db\":2.0}"},
	{"encima", "Preset.Above",
	 "{\"quick_pos\":4,\"azimuth\":0.0,\"elevation\":90.0,\"distance\":20.0,\"glide_s\":1.5,\"cues\":140.0,"
	 "\"prox\":50.0,\"air_loss\":40.0,\"level_loss\":30.0,\"room\":10.0,\"input_mode\":0,\"mix\":100.0,"
	 "\"out_gain_db\":1.0}"},
	{"frente_cercano", "Preset.FrontClose",
	 "{\"quick_pos\":2,\"azimuth\":0.0,\"elevation\":0.0,\"distance\":12.0,\"glide_s\":1.5,\"cues\":100.0,"
	 "\"prox\":90.0,\"air_loss\":40.0,\"level_loss\":40.0,\"room\":8.0,\"input_mode\":0,\"mix\":100.0,"
	 "\"out_gain_db\":0.0}"},
};

static const struct sd_presets_def presets_def = {
	preset_keys,
	sizeof(preset_keys) / sizeof(preset_keys[0]),
	builtin_presets,
	sizeof(builtin_presets) / sizeof(builtin_presets[0]),
};

struct bin_filter {
	obs_source_t *context; /* DEBE ser el primer miembro (lo usa sd_presets) */
	bin_state dsp;
	int dsp_ok;
	size_t channels;
	bin_lock_t lock;
	bin_lock_t pending;
	bin_params next;
};

static const char *bin_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return T_("Spatializer");
}

static void read_params(obs_data_t *s, bin_params *p)
{
	bin_default_params(p);
	int q = (int)obs_data_get_int(s, S_QUICK);
	if (q >= Q_LEFT && q < Q_CUSTOM) {
		p->azimuth = quick_az[q];
		p->elevation = quick_el[q];
	} else {
		p->azimuth = (float)obs_data_get_double(s, S_AZ);
		p->elevation = (float)obs_data_get_double(s, S_EL);
	}
	p->distance = (float)obs_data_get_double(s, S_DIST) / 100.0f;
	p->glide_s = (float)obs_data_get_double(s, S_GLIDE);
	p->cues = (float)obs_data_get_double(s, S_CUES) / 100.0f;
	p->prox = (float)obs_data_get_double(s, S_PROX) / 100.0f;
	p->air_loss = (float)obs_data_get_double(s, S_AIR) / 100.0f;
	p->level_loss = (float)obs_data_get_double(s, S_LEVEL) / 100.0f;
	p->room = (float)obs_data_get_double(s, S_ROOM) / 100.0f;
	p->mono_in = obs_data_get_int(s, S_INPUT) == 0;
	p->mix = (float)obs_data_get_double(s, S_MIX) / 100.0f;
	p->out_gain = sd_db_to_lin((float)obs_data_get_double(s, S_GAIN));
}

static void bin_update(void *data, obs_data_t *settings)
{
	struct bin_filter *f = data;
	bin_params p;
	read_params(settings, &p);
	while (LOCK_XCHG(&f->lock, 1) != 0)
		;
	f->next = p;
	LOCK_STORE(&f->pending, 1);
	LOCK_STORE(&f->lock, 0);
}

static void *bin_create(obs_data_t *settings, obs_source_t *source)
{
	struct bin_filter *f = bzalloc(sizeof(*f));
	f->context = source;
	audio_t *audio = obs_get_audio();
	f->channels = audio_output_get_channels(audio);
	f->dsp_ok = bin_init(&f->dsp, (float)audio_output_get_sample_rate(audio)) == 0;
	if (!f->dsp_ok)
		blog(LOG_ERROR, "[%s] no se pudo reservar memoria", PLUGIN_NAME);
	bin_params p;
	read_params(settings, &p);
	if (f->dsp_ok) {
		bin_set_params(&f->dsp, &p);
		bin_snap(&f->dsp); /* al crear, la voz empieza ya en su sitio */
	}
	return f;
}

static void bin_destroy(void *data)
{
	struct bin_filter *f = data;
	bin_free(&f->dsp);
	bfree(f);
}

static struct obs_audio_data *bin_filter_audio(void *data, struct obs_audio_data *audio)
{
	struct bin_filter *f = data;
	if (!f->dsp_ok || !audio || audio->frames == 0)
		return audio;
	if (LOCK_LOAD(&f->pending) && LOCK_XCHG(&f->lock, 1) == 0) {
		bin_params p = f->next;
		LOCK_STORE(&f->pending, 0);
		LOCK_STORE(&f->lock, 0);
		bin_set_params(&f->dsp, &p);
	}
	float *l = (float *)audio->data[0];
	float *r = f->channels >= 2 ? (float *)audio->data[1] : NULL;
	if (l)
		bin_process(&f->dsp, l, r, audio->frames); /* canales 3+ pasan sin tocar */
	return audio;
}

static void bin_defaults(obs_data_t *s)
{
	sd_presets_defaults(s);
	obs_data_set_default_int(s, S_QUICK, Q_FRONT);
	obs_data_set_default_double(s, S_AZ, 0.0);
	obs_data_set_default_double(s, S_EL, 0.0);
	obs_data_set_default_double(s, S_DIST, 25.0);
	obs_data_set_default_double(s, S_GLIDE, 1.5);
	obs_data_set_default_double(s, S_CUES, 100.0);
	obs_data_set_default_double(s, S_PROX, 100.0);
	obs_data_set_default_double(s, S_AIR, 50.0);
	obs_data_set_default_double(s, S_LEVEL, 50.0);
	obs_data_set_default_double(s, S_ROOM, 15.0);
	obs_data_set_default_int(s, S_INPUT, 0);
	obs_data_set_default_double(s, S_MIX, 100.0);
	obs_data_set_default_double(s, S_GAIN, 0.0);
}

static bool visibility_changed(obs_properties_t *props, obs_property_t *p, obs_data_t *s)
{
	UNUSED_PARAMETER(p);
	bool custom = obs_data_get_int(s, S_QUICK) == Q_CUSTOM;
	obs_property_set_visible(obs_properties_get(props, S_AZ), custom);
	obs_property_set_visible(obs_properties_get(props, S_EL), custom);
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

static obs_properties_t *bin_properties(void *data)
{
	UNUSED_PARAMETER(data);
	obs_properties_t *props = obs_properties_create();
	obs_properties_t *g;
	obs_property_t *p;

	sd_presets_add_properties(props, &presets_def);

	/* Posicion */
	g = obs_properties_create();
	p = obs_properties_add_list(g, S_QUICK, T_("QuickPos"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, T_("QuickPos.Left"), Q_LEFT);
	obs_property_list_add_int(p, T_("QuickPos.Right"), Q_RIGHT);
	obs_property_list_add_int(p, T_("QuickPos.Front"), Q_FRONT);
	obs_property_list_add_int(p, T_("QuickPos.Behind"), Q_BEHIND);
	obs_property_list_add_int(p, T_("QuickPos.Above"), Q_ABOVE);
	obs_property_list_add_int(p, T_("QuickPos.Custom"), Q_CUSTOM);
	obs_property_set_modified_callback(p, visibility_changed);
	help(p, "QuickPos.Help");
	slider(g, S_AZ, "Azimuth", 0.0, 359.0, 1.0, " °");
	slider(g, S_EL, "Elevation", -45.0, 90.0, 1.0, " °");
	slider(g, S_DIST, "Distance", 0.0, 100.0, 1.0, " %");
	slider(g, S_GLIDE, "Glide", 0.0, 10.0, 0.1, " s");
	obs_properties_add_group(props, "grp_position", T_("Group.Position"), OBS_GROUP_NORMAL, g);

	/* Realismo */
	g = obs_properties_create();
	slider(g, S_CUES, "Cues", 0.0, 200.0, 1.0, " %");
	slider(g, S_PROX, "Prox", 0.0, 200.0, 1.0, " %");
	slider(g, S_AIR, "AirLoss", 0.0, 100.0, 1.0, " %");
	slider(g, S_LEVEL, "LevelLoss", 0.0, 100.0, 1.0, " %");
	slider(g, S_ROOM, "Room", 0.0, 100.0, 1.0, " %");
	obs_properties_add_group(props, "grp_realism", T_("Group.Realism"), OBS_GROUP_NORMAL, g);

	/* Entrada y salida */
	g = obs_properties_create();
	p = obs_properties_add_list(g, S_INPUT, T_("InputMode"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, T_("InputMode.Mono"), 0);
	obs_property_list_add_int(p, T_("InputMode.Stereo"), 1);
	help(p, "InputMode.Help");
	slider(g, S_MIX, "Mix", 0.0, 100.0, 1.0, " %");
	slider(g, S_GAIN, "OutputGain", -24.0, 12.0, 0.1, " dB");
	obs_properties_add_group(props, "grp_output", T_("Group.Output"), OBS_GROUP_NORMAL, g);
	return props;
}

static struct obs_source_info spatializer_filter = {
	.id = "streamerdesk_asmr_binaural_spatializer",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_AUDIO,
	.get_name = bin_get_name,
	.create = bin_create,
	.destroy = bin_destroy,
	.update = bin_update,
	.filter_audio = bin_filter_audio,
	.get_defaults = bin_defaults,
	.get_properties = bin_properties,
};

const char *obs_module_name(void)
{
	return "Streamerdesk ASMR - Espacialidad binaural";
}

const char *obs_module_description(void)
{
	return T_("Description");
}

bool obs_module_load(void)
{
	obs_register_source(&spatializer_filter);
	blog(LOG_INFO, "[%s] cargado (version %s)", PLUGIN_NAME, PLUGIN_VERSION);
	return true;
}
