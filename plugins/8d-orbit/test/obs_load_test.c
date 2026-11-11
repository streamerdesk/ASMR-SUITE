/*
 * Prueba de integracion de 8D / Orbita con libobs REAL (sin interfaz): carga el plugin como OBS,
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

static unsigned rs = 1;
static float noise(void) { rs = rs * 1664525u + 1013904223u; return ((float)(rs >> 8) / 8388608.0f) - 1.0f; }

static bool visible(obs_properties_t *props, const char *k) { return obs_property_visible(obs_properties_get(props, k)); }
static void set_mode(obs_properties_t *props, obs_data_t *s, int mode, int unit)
{
	obs_data_set_int(s, "mode", mode);
	obs_data_set_int(s, "speed_unit", unit);
	obs_property_modified(obs_properties_get(props, "mode"), s);
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

	const char *id = "streamerdesk_asmr_8d_orbit";
	const char *disp = obs_source_get_display_name(id);
	CHECK(disp && strcmp(disp, "Streamerdesk ASMR - 8D / Órbita") == 0, "filtro registrado: %s", disp ? disp : "(null)");
	obs_source_t *filter = obs_source_create(id, "8D", NULL, NULL);
	CHECK(filter != NULL, "obs_source_create del filtro");

	printf("Propiedades:\n");
	obs_properties_t *props = obs_source_properties(filter);
	int help = 0, n = count_props(props, &help, 1);
	CHECK(n >= 24 && help == n, "%d controles, %d con ayuda (?)", n, help);

	obs_data_t *s = obs_source_get_settings(filter);
	set_mode(props, s, 0, 0);
	CHECK(visible(props, "period_s") && !visible(props, "rate_hz") && !visible(props, "bpm") && !visible(props, "bars") &&
	      !visible(props, "arc_deg") && visible(props, "start_deg"), "orbita: duracion y angulo inicial visibles; Hz, BPM, compases y arco ocultos");
	set_mode(props, s, 0, 1);
	CHECK(!visible(props, "period_s") && visible(props, "rate_hz"), "velocidad en Hz: se muestra Hz y se oculta segundos");
	set_mode(props, s, 1, 0);
	CHECK(visible(props, "arc_deg") && visible(props, "start_deg"), "vaiven en arco: anchura del arco visible");
	set_mode(props, s, 2, 0);
	CHECK(!visible(props, "start_deg") && !visible(props, "arc_deg") && visible(props, "period_s"), "deriva aleatoria: sin angulo inicial ni arco");
	set_mode(props, s, 3, 0);
	CHECK(visible(props, "bpm") && visible(props, "bars") && !visible(props, "period_s") && !visible(props, "speed_unit"),
	      "al ritmo: BPM y compases visibles; duracion/unidad ocultas");
	set_mode(props, s, 0, 0);

	/* presets */
	obs_data_set_double(s, "period_s", 33.5);
	obs_data_set_string(s, "sd_preset_name", "Mi órbita");
	obs_source_update(filter, s);
	obs_property_button_clicked(obs_properties_get(props, "sd_preset_save"), filter);
	char path[512];
	snprintf(path, sizeof(path), "%s/streamerdesk-asmr-8d-orbit/presets.json", argv[3]);
	char *json = os_quick_read_utf8_file(path);
	CHECK(json && strstr(json, "Mi órbita") && strstr(json, "33.5"), "preset de usuario guardado en presets.json");
	bfree(json);
	obs_properties_destroy(props);
	props = obs_source_properties(filter);
	obs_property_t *list = obs_properties_get(props, "sd_preset");
	CHECK(obs_property_list_item_count(list) == 6, "desplegable: ninguno + 4 integrados + 1 de usuario");
	obs_data_set_string(s, "sd_preset", "b:ear_to_ear");
	obs_property_modified(list, s);
	CHECK(obs_data_get_int(s, "mode") == 1 && fabs(obs_data_get_double(s, "arc_deg") - 180.0) < 1e-6 &&
	      fabs(obs_data_get_double(s, "distance_m") - 0.3) < 1e-6 && visible(props, "arc_deg"),
	      "preset 'Susurro de oido a oido' aplicado (modo arco, 180 grados, 0,30 m) y arco visible");
	obs_data_set_string(s, "sd_preset", "b:hypnotic_pulse");
	obs_property_modified(list, s);
	CHECK(obs_data_get_int(s, "mode") == 3 && fabs(obs_data_get_double(s, "bpm") - 80.0) < 1e-6 && visible(props, "bpm"),
	      "preset 'Pulso hipnotico' aplicado (al ritmo, 80 BPM) y BPM visible");
	obs_property_button_clicked(obs_properties_get(props, "sd_preset_reset"), filter);
	obs_data_release(s);
	s = obs_source_get_settings(filter);
	CHECK(fabs(obs_data_get_double(s, "period_s") - 12.0) < 1e-6 && obs_data_get_int(s, "mode") == 0,
	      "Restablecer -> defecto (orbita de %.0f s)", obs_data_get_double(s, "period_s"));
	obs_data_set_string(s, "sd_preset", "u:Mi órbita");
	obs_property_button_clicked(obs_properties_get(props, "sd_preset_delete"), filter);
	json = os_quick_read_utf8_file(path);
	CHECK(json && !strstr(json, "Mi órbita"), "Borrar preset");
	bfree(json);
	obs_properties_destroy(props);

	/* audio real: ruido MONO (micro USB) -> voz situada junto al oido derecho, orbita muy lenta */
	obs_data_set_int(s, "mode", 0);
	obs_data_set_double(s, "period_s", 120.0);
	obs_data_set_double(s, "start_deg", 90.0);
	obs_data_set_double(s, "room", 0.0);
	obs_data_set_double(s, "elevation", 0.0);
	obs_data_set_double(s, "mix", 100.0);
	obs_data_set_double(s, "distance_m", 1.0);
	obs_source_update(filter, s);
	obs_data_release(s);

	obs_register_source(&test_src);
	obs_source_t *src = obs_source_create("test_mono_src", "mic", NULL, NULL);
	obs_source_filter_add(src, filter);
	obs_set_output_source(0, src);
	obs_source_add_audio_capture_callback(src, on_audio, NULL);
	float buf[480];
	uint64_t t0 = os_gettime_ns();
	for (int blk = 0; blk < 300; blk++) {
		for (int i = 0; i < 480; i++) buf[i] = 0.3f * noise();
		if (blk == 10) capturing = 1;
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
	int bad = 0;
	for (long i = 0; i < N; i++)
		if (!isfinite(capL[i]) || !isfinite(capR[i])) bad++;
	printf("Audio filtrado capturado en OBS: %ld muestras\n", N);
	CHECK(N > SR && bad == 0, "sin NaN/Inf");
	double el = win_rms(capL, 4800, 4800 + 48000), er = win_rms(capR, 4800, 4800 + 48000);
	double ild = 20 * log10(er / el);
	CHECK(ild > 6.0, "fuente MONO -> voz junto al oido derecho: derecha %.1f dB mas fuerte", ild);
	double best = -1e30; int blag = 0;
	for (int lag = -40; lag <= 40; lag++) {
		double c = 0;
		for (long i = 4800; i < 4800 + 48000; i++) c += (double)capL[i] * capR[i - lag];
		if (c > best) { best = c; blag = lag; }
	}
	CHECK(blag >= 25 && blag <= 35, "diferencia de tiempo entre oidos: el izquierdo llega %d muestras (%.2f ms) tarde", blag, blag / 48.0);

	obs_source_remove_audio_capture_callback(src, on_audio, NULL);
	obs_set_output_source(0, NULL);
	obs_source_release(src);
	obs_source_release(filter);
	obs_shutdown();
	printf("\nRESULTADO INTEGRACION: %s\n", fails ? "FALLOS" : "OK");
	return fails ? 1 : 0;
}
