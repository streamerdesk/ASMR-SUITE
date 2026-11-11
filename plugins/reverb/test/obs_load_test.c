/*
 * Prueba de integracion de la Reverb con libobs REAL (sin interfaz): carga el plugin como OBS,
 * comprueba registro, nombre localizado, propiedades + ayudas, visibilidad, presets y pasa
 * audio real (fuente MONO tipo micro USB) por el filtro dentro de OBS.
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
#define CAP (SR * 4)
static float capL[CAP], capR[CAP];
static long cap_pos = 0;
static int capturing = 0;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("  FALLO: " __VA_ARGS__); } else printf("  ok: " __VA_ARGS__); printf("\n"); } while (0)

static void on_audio(void *param, obs_source_t *source, const struct audio_data *data, bool muted)
{
	(void)param; (void)source; (void)muted;
	if (!capturing) return;
	for (uint32_t i = 0; i < data->frames && cap_pos < CAP; i++, cap_pos++) {
		capL[cap_pos] = ((float *)data->data[0])[i];
		capR[cap_pos] = ((float *)data->data[1])[i];
	}
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
		printf("  %*s- %-16s %s%s\n", depth * 2, "", obs_property_name(p), obs_property_description(p),
		       ld && *ld ? "  (?)" : "  (sin ayuda)");
	}
	return n;
}

static double win_rms(const float *x, long a, long b)
{
	double s = 0; for (long i = a; i < b; i++) s += (double)x[i] * x[i];
	return sqrt(s / (b - a));
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

	const char *id = "streamerdesk_asmr_reverb";
	const char *disp = obs_source_get_display_name(id);
	CHECK(disp && strcmp(disp, "Streamerdesk ASMR - Reverb") == 0, "filtro registrado: %s", disp ? disp : "(null)");
	obs_source_t *filter = obs_source_create(id, "Reverb", NULL, NULL);
	CHECK(filter != NULL, "obs_source_create del filtro");

	printf("Propiedades:\n");
	obs_properties_t *props = obs_source_properties(filter);
	int help = 0, n = count_props(props, &help, 1);
	CHECK(n >= 20 && help == n, "%d controles, %d con ayuda (?)", n, help);

	obs_data_t *s = obs_source_get_settings(filter);
	/* visibilidad: congelar oculta decaimiento y amortiguacion; ducking 0 oculta recuperacion */
	obs_data_set_bool(s, "freeze", true);
	obs_data_set_double(s, "duck", 0.0);
	obs_property_modified(obs_properties_get(props, "freeze"), s);
	CHECK(!obs_property_visible(obs_properties_get(props, "decay_s")) &&
	      !obs_property_visible(obs_properties_get(props, "duck_release_ms")),
	      "controles irrelevantes ocultos (congelar -> sin decaimiento; ducking 0 -> sin recuperacion)");
	obs_data_set_bool(s, "freeze", false);
	obs_data_set_double(s, "duck", 30.0);
	obs_property_modified(obs_properties_get(props, "freeze"), s);
	CHECK(obs_property_visible(obs_properties_get(props, "decay_s")) &&
	      obs_property_visible(obs_properties_get(props, "duck_release_ms")), "y vuelven a mostrarse");

	/* presets */
	obs_data_set_double(s, "decay_s", 4.25);
	obs_data_set_string(s, "sd_preset_name", "Mi reverb");
	obs_source_update(filter, s);
	obs_property_button_clicked(obs_properties_get(props, "sd_preset_save"), filter);
	char path[512];
	snprintf(path, sizeof(path), "%s/streamerdesk-asmr-reverb/presets.json", argv[3]);
	char *json = os_quick_read_utf8_file(path);
	CHECK(json && strstr(json, "Mi reverb") && strstr(json, "4.25"), "preset de usuario guardado en presets.json");
	bfree(json);
	obs_properties_destroy(props);
	props = obs_source_properties(filter);
	obs_property_t *list = obs_properties_get(props, "sd_preset");
	CHECK(obs_property_list_item_count(list) == 6, "desplegable: ninguno + 4 integrados + 1 de usuario");
	obs_data_set_string(s, "sd_preset", "b:whisper_cathedral");
	obs_property_modified(list, s);
	CHECK(fabs(obs_data_get_double(s, "decay_s") - 6.5) < 1e-6 && fabs(obs_data_get_double(s, "size") - 96.0) < 1e-6,
	      "preset 'Catedral susurro' aplicado (decay %.1f s, tamano %.0f %%)", obs_data_get_double(s, "decay_s"), obs_data_get_double(s, "size"));
	obs_property_button_clicked(obs_properties_get(props, "sd_preset_reset"), filter);
	obs_data_release(s);
	s = obs_source_get_settings(filter);
	CHECK(fabs(obs_data_get_double(s, "decay_s") - 1.6) < 1e-6, "Restablecer -> defecto (decay %.2f s)", obs_data_get_double(s, "decay_s"));
	obs_data_set_string(s, "sd_preset", "u:Mi reverb");
	obs_property_button_clicked(obs_properties_get(props, "sd_preset_delete"), filter);
	json = os_quick_read_utf8_file(path);
	CHECK(json && !strstr(json, "Mi reverb"), "Borrar preset");
	bfree(json);
	obs_properties_destroy(props);

	/* audio real: impulso MONO -> reverb solo humeda, sin ducking ni filtros */
	obs_data_set_double(s, "mix", 100.0);
	obs_data_set_double(s, "duck", 0.0);
	obs_data_set_double(s, "decay_s", 1.0);
	obs_data_set_double(s, "predelay_ms", 50.0);
	obs_source_update(filter, s);
	obs_data_release(s);

	obs_register_source(&test_src);
	obs_source_t *src = obs_source_create("test_mono_src", "mic", NULL, NULL);
	obs_source_filter_add(src, filter);
	obs_set_output_source(0, src);
	obs_source_add_audio_capture_callback(src, on_audio, NULL);
	float buf[480];
	uint64_t t0 = os_gettime_ns();
	for (int blk = 0; blk < 450; blk++) {
		memset(buf, 0, sizeof(buf));
		if (blk == 10) capturing = 1;
		if (blk == 20) buf[0] = 1.0f; /* impulso en la muestra 4800 de la captura */
		struct obs_source_audio a = {0};
		a.data[0] = (uint8_t *)buf;
		a.frames = 480;
		a.speakers = SPEAKERS_MONO;
		a.format = AUDIO_FORMAT_FLOAT_PLANAR;
		a.samples_per_sec = SR;
		a.timestamp = t0 + (uint64_t)blk * 10000000ULL;
		obs_source_output_audio(src, &a);
	}
	long N = cap_pos;
	int bad = 0; long first = -1;
	for (long i = 0; i < N; i++) {
		if (!isfinite(capL[i]) || !isfinite(capR[i])) bad++;
		if (first < 0 && (fabsf(capL[i]) > 1e-4f || fabsf(capR[i]) > 1e-4f)) first = i;
	}
	printf("Audio filtrado capturado en OBS: %ld muestras\n", N);
	CHECK(bad == 0, "sin NaN/Inf");
	/* impulso en 4800 + pre-retardo 50 ms (2400) + primera reflexion temprana (~6 ms con tamano 45 %) */
	CHECK(first >= 4800 + 2400 && first <= 4800 + 2400 + 480, "la reverb empieza tras el pre-retardo de 50 ms + primera reflexion (muestra %ld = %+.1f ms tras el impulso)",
	      first, (first - 4800) * 1000.0 / SR);
	double e1 = win_rms(capL, 9600, 14400) + win_rms(capR, 9600, 14400);
	double e2 = win_rms(capL, 9600 + 24000, 14400 + 24000) + win_rms(capR, 9600 + 24000, 14400 + 24000);
	double drop = 20 * log10(e2 / e1);
	CHECK(drop < -20 && drop > -40, "decae %.1f dB en 0,5 s con decay 1 s (esperado ~ -30 dB)", drop);
	double sll = 0, srr = 0, slr = 0;
	for (long i = 9600; i < 9600 + 48000 && i < N; i++) { sll += capL[i] * capL[i]; srr += capR[i] * capR[i]; slr += capL[i] * capR[i]; }
	double c = slr / sqrt(sll * srr + 1e-30);
	CHECK(fabs(c) < 0.3, "fuente MONO -> reverb estereo decorrelada (correlacion L/R %+.3f)", c);

	obs_source_remove_audio_capture_callback(src, on_audio, NULL);
	obs_set_output_source(0, NULL);
	obs_source_release(src);
	obs_source_release(filter);
	obs_shutdown();
	printf("\nRESULTADO INTEGRACION: %s\n", fails ? "FALLOS" : "OK");
	return fails ? 1 : 0;
}
