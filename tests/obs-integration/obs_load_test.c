/*
 * Prueba de integracion con libobs REAL (sin interfaz): carga el plugin como lo hace OBS,
 * comprueba registro, nombre localizado, propiedades + ayudas, presets (guardar/aplicar)
 * y pasa audio real (fuente MONO, tipo micro USB) por el pipeline de audio de OBS con el filtro.
 * Uso: obs_load_test <plugin.so> <data_dir> <config_root>  (config_root = plugin_config de OBS)
 */
#include <obs.h>
#include <obs-module.h>
#include <util/platform.h>
#include <util/threading.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>

#define SR 48000
#define CAP_SEC 2
static float capL[SR * CAP_SEC], capR[SR * CAP_SEC];
static volatile long cap_pos = 0;
static volatile bool capturing = false;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("  FALLO: " __VA_ARGS__); printf("\n"); } else { printf("  ok: " __VA_ARGS__); printf("\n"); } } while (0)

/* callback de captura de la fuente: OBS lo llama con el audio YA filtrado
 * (es lo mismo que alimenta el vumetro y el mezclador). */
static void on_audio(void *param, obs_source_t *source, const struct audio_data *data, bool muted)
{
	(void)param; (void)source; (void)muted;
	if (!capturing) return;
	long p = cap_pos;
	for (uint32_t i = 0; i < data->frames && p < SR * CAP_SEC; i++, p++) {
		capL[p] = ((float *)data->data[0])[i];
		capR[p] = ((float *)data->data[1])[i];
	}
	cap_pos = p;
}

/* fuente de prueba: audio asincrono MONO */
static const char *src_name(void *u) { (void)u; return "test mono source"; }
static void *src_create(obs_data_t *s, obs_source_t *src) { (void)s; return src; }
static void src_destroy(void *d) { (void)d; }
static struct obs_source_info test_src = {
	.id = "test_mono_src", .type = OBS_SOURCE_TYPE_INPUT, .output_flags = OBS_SOURCE_AUDIO,
	.get_name = src_name, .create = src_create, .destroy = src_destroy,
};

static int count_props(obs_properties_t *props, int *with_help, int depth)
{
	int n = 0;
	for (obs_property_t *p = obs_properties_first(props); p; obs_property_next(&p)) {
		const char *ld = obs_property_long_description(p);
		if (obs_property_get_type(p) == OBS_PROPERTY_GROUP) {
			printf("  %*s[grupo] %s\n", depth * 2, "", obs_property_description(p));
			n += count_props(obs_property_group_content(p), with_help, depth + 1);
			continue;
		}
		n++;
		if (ld && *ld) (*with_help)++;
		printf("  %*s- %-16s %s%s\n", depth * 2, "", obs_property_name(p), obs_property_description(p),
		       ld && *ld ? "  (?)" : "  (sin ayuda)");
	}
	return n;
}

int main(int argc, char **argv)
{
	if (argc < 4) return 2;
	if (!obs_startup("es-ES", argv[3], NULL)) { printf("obs_startup fallo\n"); return 1; }
	printf("libobs version %s\n", obs_get_version_string());
	struct obs_audio_info ai = {.samples_per_sec = SR, .speakers = SPEAKERS_STEREO};
	CHECK(obs_reset_audio(&ai), "obs_reset_audio estereo 48 kHz");

	obs_module_t *mod = NULL;
	int r = obs_open_module(&mod, argv[1], argv[2]);
	CHECK(r == MODULE_SUCCESS, "obs_open_module (codigo %d)", r);
	if (r != MODULE_SUCCESS) return 1;
	CHECK(obs_init_module(mod), "obs_init_module");
	printf("  modulo: %s - %s\n", obs_get_module_name(mod), obs_get_module_description(mod));

	const char *id = "streamerdesk_asmr_pingpong_echo";
	const char *disp = obs_source_get_display_name(id);
	CHECK(disp && strcmp(disp, "Streamerdesk ASMR - Eco Ping-Pong") == 0, "filtro registrado, nombre en espanol: %s", disp ? disp : "(null)");
	CHECK((obs_get_source_output_flags(id) & OBS_SOURCE_AUDIO) != 0, "flags OBS_SOURCE_AUDIO");

	obs_source_t *filter = obs_source_create(id, "Eco", NULL, NULL);
	CHECK(filter != NULL, "obs_source_create del filtro");

	printf("Propiedades:\n");
	obs_properties_t *props = obs_source_properties(filter);
	int help = 0, n = count_props(props, &help, 1);
	CHECK(n > 20 && help == n, "%d controles, %d con ayuda (?)", n, help);

	/* --- presets --- */
	obs_data_t *s = obs_source_get_settings(filter);
	obs_data_set_double(s, "fb_l", 77.0);
	obs_data_set_string(s, "sd_preset_name", "Mi preset de prueba");
	obs_source_update(filter, s);
	obs_property_t *save = obs_properties_get(props, "sd_preset_save");
	CHECK(save != NULL, "boton Guardar preset");
	obs_property_button_clicked(save, filter);
	char path[512];
	snprintf(path, sizeof(path), "%s/streamerdesk-asmr-pingpong-echo/presets.json", argv[3]);
	char *json = os_quick_read_utf8_file(path);
	CHECK(json && strstr(json, "Mi preset de prueba") && strstr(json, "77"), "presets.json escrito en %s", path);
	bfree(json);
	obs_properties_destroy(props);
	props = obs_source_properties(filter);
	obs_property_t *list = obs_properties_get(props, "sd_preset");
	bool found = false;
	for (size_t i = 0; i < obs_property_list_item_count(list); i++)
		if (strcmp(obs_property_list_item_string(list, i), "u:Mi preset de prueba") == 0) found = true;
	CHECK(found && obs_property_list_item_count(list) == 6, "desplegable: ninguno + 4 integrados + 1 de usuario (%zu items)", obs_property_list_item_count(list));
	obs_data_set_string(s, "sd_preset", "b:dub_classic");
	obs_property_modified(list, s);
	CHECK(fabs(obs_data_get_double(s, "fb_l") - 68.0) < 1e-6 && obs_data_get_int(s, "sync_mode") == 1,
	      "aplicar preset integrado 'Dub clasico' (fb_l=%.0f, sync=%lld)", obs_data_get_double(s, "fb_l"), obs_data_get_int(s, "sync_mode"));
	obs_property_t *reset = obs_properties_get(props, "sd_preset_reset");
	obs_property_button_clicked(reset, filter);
	obs_data_release(s);
	s = obs_source_get_settings(filter);
	CHECK(fabs(obs_data_get_double(s, "fb_l") - 45.0) < 1e-6, "Restablecer -> valores por defecto (fb_l=%.0f)", obs_data_get_double(s, "fb_l"));
	obs_property_t *del = obs_properties_get(props, "sd_preset_delete");
	obs_data_set_string(s, "sd_preset", "u:Mi preset de prueba");
	obs_property_button_clicked(del, filter);
	json = os_quick_read_utf8_file(path);
	CHECK(json && !strstr(json, "Mi preset de prueba"), "Borrar preset");
	bfree(json);
	obs_properties_destroy(props);

	/* --- audio real por el pipeline de OBS: fuente mono -> filtro -> mezcla estereo --- */
	obs_data_set_double(s, "delay_l_ms", 100.0);
	obs_data_set_double(s, "fb_l", 50.0);
	obs_data_set_double(s, "mix", 100.0);
	obs_data_set_double(s, "damp_hz", 20000.0);
	obs_data_set_double(s, "lowcut_hz", 10.0);
	obs_data_set_bool(s, "saturate", false);
	obs_source_update(filter, s);
	obs_data_release(s);

	obs_register_source(&test_src);
	obs_source_t *src = obs_source_create("test_mono_src", "mic", NULL, NULL);
	if (!getenv("NOFILTER")) obs_source_filter_add(src, filter);
	obs_set_output_source(0, src);
	obs_source_add_audio_capture_callback(src, on_audio, NULL);

	float buf[480];
	uint64_t t0 = os_gettime_ns();
	for (int blk = 0; blk < 300; blk++) { /* 3 s en bloques de 10 ms */
		memset(buf, 0, sizeof(buf));
		if (blk == 50) buf[0] = 1.0f;
		if (blk == 40) capturing = true;
		struct obs_source_audio a = {0};
		a.data[0] = (uint8_t *)buf;
		a.frames = 480;
		a.speakers = SPEAKERS_MONO;
		a.format = AUDIO_FORMAT_FLOAT_PLANAR;
		a.samples_per_sec = SR;
		a.timestamp = t0 + (uint64_t)blk * 10000000ULL;
		obs_source_output_audio(src, &a);
	}
	capturing = false;
	long N = cap_pos;
	float mx = 0; long mxi = 0;
	for (long i = 0; i < N; i++) { float v = fmaxf(fabsf(capL[i]), fabsf(capR[i])); if (v > mx) { mx = v; mxi = i; } }
	printf("Audio filtrado capturado de la fuente en OBS: %ld muestras, pico %.3f en %ld (mixers=0x%x activo=%d)\n", N, mx, mxi,
	       obs_source_get_audio_mixers(src), obs_source_active(src));
	/* buscar picos */
	int k = 0; long prev = -1; float prevamp = 10; int alt_ok = 1; char lastside = 0;
	for (long i = 0; i < N && k < 6; i++) {
		float l = fabsf(capL[i]), rr = fabsf(capR[i]);
		if (l > 0.02f || rr > 0.02f) {
			long j = i, best = i; float bv = 0; char side = l > rr ? 'L' : 'R';
			for (; j < i + 200 && j < N; j++) { float v = fmaxf(fabsf(capL[j]), fabsf(capR[j])); if (v > bv) { bv = v; best = j; } }
			float bl = fabsf(capL[best]), br = fabsf(capR[best]);
			side = bl > br ? 'L' : 'R';
			printf("  repeticion %d: muestra %ld (%+.1f ms desde la anterior) lado %c  L=%.3f R=%.3f\n", k + 1, best,
			       prev < 0 ? 0.0 : (best - prev) * 1000.0 / SR, side, bl, br);
			if (prev >= 0 && labs(best - prev - 4800) > 2) alt_ok = 0;
			if (lastside && side == lastside) alt_ok = 0;
			if (fmaxf(bl, br) >= prevamp) alt_ok = 0;
			prevamp = fmaxf(bl, br); lastside = side; prev = best; k++; i = j;
		}
	}
	CHECK(k >= 5 && alt_ok, "repeticiones alternando L/R cada 100 ms y decayendo, con fuente MONO en OBS real");

	obs_source_remove_audio_capture_callback(src, on_audio, NULL);
	obs_set_output_source(0, NULL);
	obs_source_release(src);
	obs_source_release(filter);
	obs_shutdown();
	printf("\nRESULTADO INTEGRACION: %s\n", fails ? "FALLOS" : "OK");
	return fails ? 1 : 0;
}
