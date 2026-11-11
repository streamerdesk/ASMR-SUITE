/*
 * Prueba de integracion con libobs REAL (sin interfaz) para Streamerdesk ASMR - Proximidad.
 * Carga el plugin como lo hace OBS, comprueba registro, nombre localizado, propiedades + ayudas,
 * visibilidad dinamica, presets (guardar/aplicar/restablecer/borrar) y pasa audio de una fuente
 * MONO (como un micro USB) por el pipeline de audio de OBS con el filtro.
 * Adaptado de streamerdesk-asmr-suite/tests/obs-integration/obs_load_test.c
 * Uso: obs_load_test <plugin.so> <data_dir> <config_root>
 */
#include <obs.h>
#include <obs-module.h>
#include <util/platform.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define SR 48000
#define CAP 48000
static float capL[CAP], capR[CAP];
static volatile long cap_pos = 0;
static volatile bool capturing = false;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("  FALLO: " __VA_ARGS__); } else { printf("  ok: " __VA_ARGS__); } printf("\n"); } while (0)

static void on_audio(void *param, obs_source_t *source, const struct audio_data *data, bool muted)
{
	(void)param; (void)source; (void)muted;
	if (!capturing) return;
	long p = cap_pos;
	for (uint32_t i = 0; i < data->frames && p < CAP; i++, p++) {
		capL[p] = ((float *)data->data[0])[i];
		capR[p] = ((float *)data->data[1])[i];
	}
	cap_pos = p;
}

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
		printf("  %*s- %-16s %s%s%s\n", depth * 2, "", obs_property_name(p), obs_property_description(p),
		       ld && *ld ? "  (?)" : "  (sin ayuda)", obs_property_visible(p) ? "" : "  [oculto]");
	}
	return n;
}

static long t_blk = 0;
static uint64_t t0;
/* manda `secs` segundos de seno 1 kHz mono y captura el ultimo medio segundo; devuelve rms L/R */
static void run_audio(obs_source_t *src, double secs, double *rl, double *rr)
{
	float buf[480];
	int nblk = (int)(secs * 100);
	cap_pos = 0;
	for (int b = 0; b < nblk; b++, t_blk++) {
		for (int i = 0; i < 480; i++)
			buf[i] = 0.1f * sinf(2.0f * 3.14159265f * 1000.0f * (float)(t_blk * 480 + i) / SR);
		if (b == nblk - 50) capturing = true;
		struct obs_source_audio a = {0};
		a.data[0] = (uint8_t *)buf;
		a.frames = 480;
		a.speakers = SPEAKERS_MONO;
		a.format = AUDIO_FORMAT_FLOAT_PLANAR;
		a.samples_per_sec = SR;
		a.timestamp = t0 + (uint64_t)t_blk * 10000000ULL;
		obs_source_output_audio(src, &a);
	}
	os_sleep_ms(200);
	capturing = false;
	double al = 0, ar = 0;
	long n = cap_pos;
	for (long i = 0; i < n; i++) { al += capL[i] * capL[i]; ar += capR[i] * capR[i]; }
	*rl = n ? sqrt(al / n) : 0; *rr = n ? sqrt(ar / n) : 0;
	printf("  (capturadas %ld muestras: RMS L %.4f, R %.4f)\n", n, *rl, *rr);
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

	const char *id = "streamerdesk_asmr_proximity";
	const char *disp = obs_source_get_display_name(id);
	CHECK(disp && strcmp(disp, "Streamerdesk ASMR - Proximidad") == 0, "filtro registrado, nombre en espanol: %s", disp ? disp : "(null)");
	CHECK((obs_get_source_output_flags(id) & OBS_SOURCE_AUDIO) != 0, "flags OBS_SOURCE_AUDIO");

	obs_source_t *filter = obs_source_create(id, "Proximidad", NULL, NULL);
	CHECK(filter != NULL, "obs_source_create del filtro");

	printf("Propiedades:\n");
	obs_properties_t *props = obs_source_properties(filter);
	int help = 0, n = count_props(props, &help, 1);
	CHECK(n >= 20 && help == n, "%d controles, %d con ayuda (?)", n, help);
	CHECK(!obs_property_visible(obs_properties_get(props, "position")), "posicion personalizada oculta en modo Centro");
	CHECK(!obs_property_visible(obs_properties_get(props, "binaural")), "binaural oculto en modo Centro");
	obs_data_t *s = obs_source_get_settings(filter);
	obs_data_set_int(s, "position_mode", 3);
	obs_data_set_bool(s, "deharsh", false);
	obs_property_modified(obs_properties_get(props, "position_mode"), s);
	CHECK(obs_property_visible(obs_properties_get(props, "position")) && obs_property_visible(obs_properties_get(props, "binaural")),
	      "modo Personalizada muestra posicion y binaural");
	CHECK(!obs_property_visible(obs_properties_get(props, "deharsh_amt")), "sin suavizar eses se oculta su cantidad");
	obs_data_set_int(s, "position_mode", 0);
	obs_data_set_bool(s, "deharsh", true);

	/* presets */
	obs_data_set_double(s, "distance", 77.0);
	obs_data_set_string(s, "sd_preset_name", "Mi susurro");
	obs_source_update(filter, s);
	obs_property_button_clicked(obs_properties_get(props, "sd_preset_save"), filter);
	char path[512];
	snprintf(path, sizeof(path), "%s/streamerdesk-asmr-proximity/presets.json", argv[3]);
	char *json = os_quick_read_utf8_file(path);
	CHECK(json && strstr(json, "Mi susurro") && strstr(json, "77"), "presets.json escrito en %s", path);
	bfree(json);
	obs_properties_destroy(props);
	props = obs_source_properties(filter);
	obs_property_t *list = obs_properties_get(props, "sd_preset");
	printf("  desplegable:");
	for (size_t i = 0; i < obs_property_list_item_count(list); i++)
		printf(" [%s]", obs_property_list_item_name(list, i));
	printf("\n");
	CHECK(obs_property_list_item_count(list) == 6, "desplegable: ninguno + 4 integrados + 1 de usuario");
	obs_data_release(s);
	s = obs_source_get_settings(filter);
	obs_data_set_string(s, "sd_preset", "b:micro_contacto");
	obs_property_modified(list, s);
	CHECK(fabs(obs_data_get_double(s, "prox_amount") - 160.0) < 1e-6 && fabs(obs_data_get_double(s, "distance")) < 1e-6,
	      "aplicar 'Micro de contacto' (prox_amount=%.0f, distance=%.0f)", obs_data_get_double(s, "prox_amount"), obs_data_get_double(s, "distance"));
	obs_data_set_string(s, "sd_preset", "b:al_oido");
	obs_property_modified(list, s);
	CHECK(fabs(obs_data_get_double(s, "distance") - 5.0) < 1e-6, "aplicar 'Al oido' (distance=%.0f)", obs_data_get_double(s, "distance"));
	obs_property_button_clicked(obs_properties_get(props, "sd_preset_reset"), filter);
	obs_data_release(s);
	s = obs_source_get_settings(filter);
	CHECK(fabs(obs_data_get_double(s, "distance") - 20.0) < 1e-6, "Restablecer -> por defecto (distance=%.0f)", obs_data_get_double(s, "distance"));
	obs_data_set_string(s, "sd_preset", "u:Mi susurro");
	obs_property_button_clicked(obs_properties_get(props, "sd_preset_delete"), filter);
	json = os_quick_read_utf8_file(path);
	CHECK(json && !strstr(json, "Mi susurro"), "Borrar preset");
	bfree(json);
	obs_properties_destroy(props);

	/* audio: fuente mono -> filtro -> estereo */
	obs_register_source(&test_src);
	obs_source_t *src = obs_source_create("test_mono_src", "mic", NULL, NULL);
	obs_source_filter_add(src, filter);
	obs_set_output_source(0, src);
	obs_source_add_audio_capture_callback(src, on_audio, NULL);
	t0 = os_gettime_ns();
	double l, rr;
	obs_data_set_double(s, "distance", 0.0);
	obs_data_set_double(s, "level_loss", 100.0);
	obs_data_set_double(s, "room", 0.0);
	obs_source_update(filter, s);
	run_audio(src, 1.5, &l, &rr);
	CHECK(l > 0.01 && fabs(l - rr) < 1e-4, "centro: L = R con fuente mono");
	double near_l = l;
	obs_data_set_double(s, "distance", 100.0);
	obs_source_update(filter, s);
	run_audio(src, 1.5, &l, &rr);
	CHECK(20 * log10(l / near_l) < -15.0, "distancia 100 %%: la voz baja %.1f dB", 20 * log10(l / near_l));
	obs_data_set_double(s, "distance", 0.0);
	obs_data_set_int(s, "position_mode", 1);
	obs_source_update(filter, s);
	run_audio(src, 1.5, &l, &rr);
	CHECK(20 * log10(l / rr) > 12.0, "pegada al oido izquierdo: L %.1f dB por encima de R", 20 * log10(l / rr));
	obs_data_release(s);

	obs_source_remove_audio_capture_callback(src, on_audio, NULL);
	obs_set_output_source(0, NULL);
	obs_source_release(src);
	obs_source_release(filter);
	obs_shutdown();
	printf("\nRESULTADO INTEGRACION: %s\n", fails ? "FALLOS" : "OK");
	return fails ? 1 : 0;
}
