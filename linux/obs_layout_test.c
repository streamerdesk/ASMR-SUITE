/*
 * Prueba de "instalacion real" en Linux: libobs busca los modulos igual que OBS Studio
 * (<config>/obs-studio/plugins/%module%/bin/64bit + data) tras ejecutar install.sh,
 * carga TODOS los efectos a la vez en el mismo proceso y comprueba que cada filtro se
 * registra con su nombre en espanol (locale leido de data/), crea cada filtro y lo destruye.
 * Uso: obs_layout_test <obs-studio_config_dir> <id1> [id2 ...]
 */
#include <obs.h>
#include <obs-module.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
	if (argc < 3) return 2;
	char bin[1024], data[1024], cfg[1024];
	snprintf(bin, sizeof(bin), "%s/plugins/%%module%%/bin/64bit", argv[1]);
	snprintf(data, sizeof(data), "%s/plugins/%%module%%/data", argv[1]);
	snprintf(cfg, sizeof(cfg), "%s/plugin_config", argv[1]);
	if (!obs_startup("es-ES", cfg, NULL)) { printf("obs_startup fallo\n"); return 1; }
	printf("libobs %s\n", obs_get_version_string());
	struct obs_audio_info ai = {.samples_per_sec = 48000, .speakers = SPEAKERS_STEREO};
	if (!obs_reset_audio(&ai)) { printf("obs_reset_audio fallo\n"); return 1; }
	obs_add_module_path(bin, data);
	obs_load_all_modules();
	obs_post_load_modules();
	int fails = 0, found = 0;
	for (int a = 2; a < argc; a++) {
		obs_module_t *m = obs_get_module(argv[a]);
		if (!m) { printf("  FALLO: modulo %s no cargado\n", argv[a]); fails++; continue; }
		printf("  ok: modulo %s cargado desde %s\n", argv[a], obs_get_module_binary_path(m));
	}
	const char *id;
	for (size_t i = 0; obs_enum_filter_types(i, &id); i++) {
		const char *dn = obs_source_get_display_name(id);
		uint32_t flags = obs_get_source_output_flags(id);
		obs_source_t *f = obs_source_create_private(id, "t", NULL);
		obs_properties_t *p = f ? obs_source_properties(f) : NULL;
		int n = 0, help = 0;
		for (obs_property_t *q = obs_properties_first(p); q; obs_property_next(&q)) {
			n++;
			if (obs_property_long_description(q)) help++;
			if (obs_property_get_type(q) == OBS_PROPERTY_GROUP) {
				obs_properties_t *g = obs_property_group_content(q);
				for (obs_property_t *r = obs_properties_first(g); r; obs_property_next(&r)) {
					n++;
					if (obs_property_long_description(r)) help++;
				}
			}
		}
		bool ok = f && dn && strncmp(dn, "Streamerdesk ASMR - ", 20) == 0 && (flags & OBS_SOURCE_AUDIO) && n > 0;
		printf("  %s: filtro %-40s \"%s\" audio=%d controles=%d ayudas=%d\n", ok ? "ok" : "FALLO", id, dn ? dn : "(null)",
		       !!(flags & OBS_SOURCE_AUDIO), n, help);
		if (!ok) fails++;
		found++;
		obs_properties_destroy(p);
		obs_source_release(f);
	}
	if (found != argc - 2) { printf("  FALLO: %d filtros registrados, esperados %d\n", found, argc - 2); fails++; }
	obs_shutdown();
	printf("\nRESULTADO INSTALACION: %s (%d filtros)\n", fails ? "FALLO" : "OK", found);
	return fails ? 1 : 0;
}
