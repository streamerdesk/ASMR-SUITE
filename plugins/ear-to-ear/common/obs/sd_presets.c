/*
 * Streamerdesk ASMR Suite - presets integrados + presets de usuario.
 * Copyright (c) 2026 Streamerdesk. GPL-2.0-or-later.
 */
#include <sys/stat.h>
#include "sd_presets.h"
#include <util/bmem.h>
#include <util/dstr.h>
#include <util/platform.h>
#include <string.h>

#define K_SELECT "sd_preset"
#define K_LAST "sd_preset_last"
#define K_NAME "sd_preset_name"
#define PRESETS_FILE "presets.json"

/* El primer miembro de la estructura privada de cada filtro es su obs_source_t*. */
struct sd_ctx_head {
	obs_source_t *context;
};

/* Cada plugin de la suite es un modulo independiente con un unico filtro, asi que
 * basta con una definicion por modulo (este .c se compila dentro de cada plugin). */
static const struct sd_presets_def *g_defs_last;

static obs_properties_t *root_props(obs_properties_t *props)
{
	obs_properties_t *parent;
	while (props && (parent = obs_properties_get_parent(props)) != NULL)
		props = parent;
	return props;
}

static const struct sd_presets_def *get_def(obs_property_t *p)
{
	(void)p;
	return g_defs_last;
}

/* ---------- fichero de presets de usuario ---------- */

static char *presets_path(void)
{
	return obs_module_config_path(PRESETS_FILE);
}

static obs_data_t *load_user_file(void)
{
	char *path = presets_path();
	obs_data_t *d = NULL;
	if (path)
		d = obs_data_create_from_json_file_safe(path, "bak");
	bfree(path);
	if (!d)
		d = obs_data_create();
	obs_data_array_t *arr = obs_data_get_array(d, "presets");
	if (!arr) {
		arr = obs_data_array_create();
		obs_data_set_array(d, "presets", arr);
	}
	obs_data_array_release(arr);
	return d;
}

static bool save_user_file(obs_data_t *d)
{
	char *cfg_dir = obs_module_config_path("");
	if (cfg_dir) {
		os_mkdirs(cfg_dir);
		bfree(cfg_dir);
	}
	char *path = presets_path();
	bool ok = false;
	if (path) {
		obs_data_set_int(d, "version", 1);
		ok = obs_data_save_json_safe(d, path, "tmp", "bak");
		if (!ok)
			blog(LOG_WARNING, "[Streamerdesk ASMR] no se pudo guardar %s", path);
	}
	bfree(path);
	return ok;
}

/* devuelve indice del preset de usuario con ese nombre o -1 */
static long long find_user(obs_data_array_t *arr, const char *name)
{
	size_t n = obs_data_array_count(arr);
	for (size_t i = 0; i < n; i++) {
		obs_data_t *it = obs_data_array_item(arr, i);
		bool eq = strcmp(obs_data_get_string(it, "name"), name) == 0;
		obs_data_release(it);
		if (eq)
			return (long long)i;
	}
	return -1;
}

/* ---------- copiar ajustes ---------- */

static void copy_key(obs_data_t *dst, obs_data_t *src, const char *key)
{
	obs_data_item_t *it = obs_data_item_byname(src, key);
	if (!it)
		return;
	switch (obs_data_item_gettype(it)) {
	case OBS_DATA_NUMBER:
		if (obs_data_item_numtype(it) == OBS_DATA_NUM_INT)
			obs_data_set_int(dst, key, obs_data_item_get_int(it));
		else
			obs_data_set_double(dst, key, obs_data_item_get_double(it));
		break;
	case OBS_DATA_BOOLEAN:
		obs_data_set_bool(dst, key, obs_data_item_get_bool(it));
		break;
	case OBS_DATA_STRING:
		obs_data_set_string(dst, key, obs_data_item_get_string(it));
		break;
	default:
		break;
	}
	obs_data_item_release(&it);
}

/* resetea las claves del preset y aplica `values` (puede ser NULL) */
static void apply_values(obs_data_t *settings, const struct sd_presets_def *def, obs_data_t *values)
{
	for (size_t i = 0; i < def->num_keys; i++)
		obs_data_unset_user_value(settings, def->keys[i]);
	if (!values)
		return;
	for (size_t i = 0; i < def->num_keys; i++) {
		if (obs_data_has_user_value(values, def->keys[i]))
			copy_key(settings, values, def->keys[i]);
	}
}

static bool apply_selection(obs_data_t *settings, const struct sd_presets_def *def, const char *sel)
{
	if (!sel || !*sel)
		return false;
	if (strncmp(sel, "b:", 2) == 0) {
		for (size_t i = 0; i < def->num_builtins; i++) {
			if (strcmp(def->builtins[i].id, sel + 2) == 0) {
				obs_data_t *v = obs_data_create_from_json(def->builtins[i].json);
				apply_values(settings, def, v);
				obs_data_release(v);
				return true;
			}
		}
	} else if (strncmp(sel, "u:", 2) == 0) {
		obs_data_t *file = load_user_file();
		obs_data_array_t *arr = obs_data_get_array(file, "presets");
		long long idx = find_user(arr, sel + 2);
		bool ok = false;
		if (idx >= 0) {
			obs_data_t *it = obs_data_array_item(arr, (size_t)idx);
			obs_data_t *v = obs_data_get_obj(it, "settings");
			apply_values(settings, def, v);
			obs_data_release(v);
			obs_data_release(it);
			ok = true;
		}
		obs_data_array_release(arr);
		obs_data_release(file);
		return ok;
	}
	return false;
}

/* ---------- lista ---------- */

static void fill_list(obs_property_t *list, const struct sd_presets_def *def)
{
	struct dstr label = {0};
	obs_property_list_clear(list);
	obs_property_list_add_string(list, obs_module_text("Presets.None"), "");
	for (size_t i = 0; i < def->num_builtins; i++) {
		struct dstr val = {0};
		dstr_printf(&label, "%s %s", obs_module_text("Presets.BuiltinPrefix"),
			    obs_module_text(def->builtins[i].name_key));
		dstr_printf(&val, "b:%s", def->builtins[i].id);
		obs_property_list_add_string(list, label.array, val.array);
		dstr_free(&val);
	}
	obs_data_t *file = load_user_file();
	obs_data_array_t *arr = obs_data_get_array(file, "presets");
	size_t n = obs_data_array_count(arr);
	for (size_t i = 0; i < n; i++) {
		obs_data_t *it = obs_data_array_item(arr, i);
		const char *name = obs_data_get_string(it, "name");
		if (name && *name) {
			struct dstr val = {0};
			dstr_printf(&label, "%s %s", obs_module_text("Presets.UserPrefix"), name);
			dstr_printf(&val, "u:%s", name);
			obs_property_list_add_string(list, label.array, val.array);
			dstr_free(&val);
		}
		obs_data_release(it);
	}
	obs_data_array_release(arr);
	obs_data_release(file);
	dstr_free(&label);
}

static void refill(obs_properties_t *props, const struct sd_presets_def *def)
{
	obs_property_t *list = obs_properties_get(root_props(props), K_SELECT);
	if (list)
		fill_list(list, def);
}

/* ---------- callbacks ---------- */

/* al elegir un preset en el desplegable se aplica (solo si cambia la seleccion,
 * para no pisar los ajustes cada vez que se abre la ventana de propiedades) */
static bool on_select(obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
	(void)props;
	const struct sd_presets_def *def = get_def(p);
	const char *sel = obs_data_get_string(settings, K_SELECT);
	const char *last = obs_data_get_string(settings, K_LAST);
	if (!def || strcmp(sel, last) == 0)
		return false;
	obs_data_set_string(settings, K_LAST, sel);
	if (strncmp(sel, "u:", 2) == 0)
		obs_data_set_string(settings, K_NAME, sel + 2);
	return apply_selection(settings, def, sel);
}

static obs_source_t *ctx_source(void *data)
{
	return data ? ((struct sd_ctx_head *)data)->context : NULL;
}

static bool on_load(obs_properties_t *props, obs_property_t *p, void *data)
{
	(void)props;
	const struct sd_presets_def *def = get_def(p);
	obs_source_t *src = ctx_source(data);
	if (!def || !src)
		return false;
	obs_data_t *s = obs_source_get_settings(src);
	bool ok = apply_selection(s, def, obs_data_get_string(s, K_SELECT));
	if (ok)
		obs_source_update(src, s);
	obs_data_release(s);
	return ok;
}

static bool on_save(obs_properties_t *props, obs_property_t *p, void *data)
{
	const struct sd_presets_def *def = get_def(p);
	obs_source_t *src = ctx_source(data);
	if (!def || !src)
		return false;
	obs_data_t *s = obs_source_get_settings(src);
	struct dstr name = {0};
	dstr_copy(&name, obs_data_get_string(s, K_NAME));
	dstr_depad(&name);
	if (dstr_is_empty(&name)) {
		obs_data_release(s);
		dstr_free(&name);
		return false;
	}
	obs_data_t *values = obs_data_create();
	for (size_t i = 0; i < def->num_keys; i++)
		copy_key(values, s, def->keys[i]);

	obs_data_t *file = load_user_file();
	obs_data_array_t *arr = obs_data_get_array(file, "presets");
	long long idx = find_user(arr, name.array);
	obs_data_t *entry = obs_data_create();
	obs_data_set_string(entry, "name", name.array);
	obs_data_set_obj(entry, "settings", values);
	if (idx >= 0)
		obs_data_array_erase(arr, (size_t)idx);
	obs_data_array_push_back(arr, entry);
	save_user_file(file);
	obs_data_release(entry);
	obs_data_array_release(arr);
	obs_data_release(file);
	obs_data_release(values);

	struct dstr sel = {0};
	dstr_printf(&sel, "u:%s", name.array);
	obs_data_set_string(s, K_SELECT, sel.array);
	obs_data_set_string(s, K_LAST, sel.array);
	obs_data_set_string(s, K_NAME, name.array);
	dstr_free(&sel);
	dstr_free(&name);
	obs_data_release(s);
	refill(props, def);
	return true;
}

static bool on_delete(obs_properties_t *props, obs_property_t *p, void *data)
{
	const struct sd_presets_def *def = get_def(p);
	obs_source_t *src = ctx_source(data);
	if (!def || !src)
		return false;
	obs_data_t *s = obs_source_get_settings(src);
	const char *sel = obs_data_get_string(s, K_SELECT);
	const char *name = strncmp(sel, "u:", 2) == 0 ? sel + 2 : obs_data_get_string(s, K_NAME);
	bool changed = false;
	if (name && *name) {
		obs_data_t *file = load_user_file();
		obs_data_array_t *arr = obs_data_get_array(file, "presets");
		long long idx = find_user(arr, name);
		if (idx >= 0) {
			obs_data_array_erase(arr, (size_t)idx);
			save_user_file(file);
			changed = true;
		}
		obs_data_array_release(arr);
		obs_data_release(file);
	}
	if (changed) {
		obs_data_set_string(s, K_SELECT, "");
		obs_data_set_string(s, K_LAST, "");
		obs_data_set_string(s, K_NAME, "");
		refill(props, def);
	}
	obs_data_release(s);
	return changed;
}

static bool on_reset(obs_properties_t *props, obs_property_t *p, void *data)
{
	(void)props;
	const struct sd_presets_def *def = get_def(p);
	obs_source_t *src = ctx_source(data);
	if (!def || !src)
		return false;
	obs_data_t *s = obs_source_get_settings(src);
	apply_values(s, def, NULL);
	obs_data_set_string(s, K_SELECT, "");
	obs_data_set_string(s, K_LAST, "");
	obs_source_update(src, s);
	obs_data_release(s);
	return true;
}

void sd_presets_defaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, K_SELECT, "");
	obs_data_set_default_string(settings, K_LAST, "");
	obs_data_set_default_string(settings, K_NAME, "");
}

void sd_presets_add_properties(obs_properties_t *props, const struct sd_presets_def *def)
{
	g_defs_last = def;
	obs_properties_t *g = obs_properties_create();
	obs_property_t *list = obs_properties_add_list(g, K_SELECT, obs_module_text("Presets.Select"),
						       OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	fill_list(list, def);
	obs_property_set_modified_callback(list, on_select);
	obs_property_set_long_description(list, obs_module_text("Presets.Select.Help"));
	obs_property_t *p;
	p = obs_properties_add_button(g, "sd_preset_load", obs_module_text("Presets.Load"), on_load);
	obs_property_set_long_description(p, obs_module_text("Presets.Load.Help"));
	p = obs_properties_add_text(g, K_NAME, obs_module_text("Presets.Name"), OBS_TEXT_DEFAULT);
	obs_property_set_long_description(p, obs_module_text("Presets.Name.Help"));
	p = obs_properties_add_button(g, "sd_preset_save", obs_module_text("Presets.Save"), on_save);
	obs_property_set_long_description(p, obs_module_text("Presets.Save.Help"));
	p = obs_properties_add_button(g, "sd_preset_delete", obs_module_text("Presets.Delete"), on_delete);
	obs_property_set_long_description(p, obs_module_text("Presets.Delete.Help"));
	p = obs_properties_add_button(g, "sd_preset_reset", obs_module_text("Presets.Reset"), on_reset);
	obs_property_set_long_description(p, obs_module_text("Presets.Reset.Help"));
	obs_properties_add_group(props, "sd_presets_group", obs_module_text("Presets.Group"), OBS_GROUP_NORMAL, g);
}
