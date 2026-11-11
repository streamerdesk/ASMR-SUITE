/*
 * Streamerdesk ASMR Suite - presets integrados + presets de usuario (comun a todos los plugins).
 * Copyright (c) 2026 Streamerdesk. GPL-2.0-or-later.
 *
 * Uso:
 *   - La estructura privada del filtro DEBE empezar con `obs_source_t *context;`
 *     (los botones reciben ese puntero).
 *   - Definir una `struct sd_presets_def` estatica con las claves que se guardan y los
 *     presets integrados (en JSON), y llamar a sd_presets_add_properties() en get_properties.
 *   - Los presets de usuario se guardan en obs_module_config_path("presets.json"), p.ej.
 *     Windows: %APPDATA%\obs-studio\plugin_config\<modulo>\presets.json
 *     Linux:   ~/.config/obs-studio/plugin_config/<modulo>/presets.json
 *     macOS:   ~/Library/Application Support/obs-studio/plugin_config/<modulo>/presets.json
 *   - Textos de interfaz (en el locale de cada plugin): Presets.Group, Presets.Select,
 *     Presets.None, Presets.Name, Presets.Load, Presets.Save, Presets.Delete, Presets.Reset,
 *     Presets.BuiltinPrefix, Presets.UserPrefix, y sus ayudas <clave>.Help (Select, Load, Name,
 *     Save, Delete, Reset) + los name_key de cada preset integrado.
 *   - Sin exportar/importar: los presets de usuario son ajustes personales locales.
 */
#pragma once
#include <obs-module.h>

#ifdef __cplusplus
extern "C" {
#endif

struct sd_builtin_preset {
	const char *id;       /* identificador estable, p.ej. "asmr_soft" */
	const char *name_key; /* clave de locale del nombre visible */
	const char *json;     /* ajustes: {"clave": valor, ...}; las claves ausentes vuelven al defecto */
};

struct sd_presets_def {
	const char *const *keys; /* claves de ajustes que forman parte de un preset */
	size_t num_keys;
	const struct sd_builtin_preset *builtins;
	size_t num_builtins;
};

/* Anade el grupo de presets (desplegable, nombre, botones) a `props`. */
void sd_presets_add_properties(obs_properties_t *props, const struct sd_presets_def *def);

/* Defaults de las claves internas del modulo de presets. */
void sd_presets_defaults(obs_data_t *settings);

#ifdef __cplusplus
}
#endif
