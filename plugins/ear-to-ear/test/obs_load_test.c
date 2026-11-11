/*
 * Prueba de integracion con libobs REAL (sin interfaz) para "Susurro de oido a oido":
 * carga el plugin como OBS, comprueba registro, nombre localizado, propiedades + ayudas,
 * presets (guardar / aplicar integrado / restablecer / borrar) y pasa audio real de una
 * fuente MONO (tipo micro USB) por el pipeline de audio de OBS con el filtro.
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
#define CAP_SEC 3
static float capL[SR * CAP_SEC], capR[SR * CAP_SEC];
static volatile long cap_pos = 0;
static volatile bool capturing = false;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("  FALLO: " __VA_ARGS__); printf("\n"); } else { printf("  ok: " __VA_ARGS__); printf("\n"); } } while (0)

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

/* pasa 3 s de seno 440 Hz mono por OBS y captura 2.5 s de la salida filtrada */
static void run_audio(obs_source_t *src)
{
	cap_pos = 0;
	float buf[480];
	static double ph = 0;
	uint64_t t0 = os_gettime_ns();
	for (int blk = 0; blk < 350; blk++) {
		for (int i = 0; i < 480; i++) { buf[i] = 0.25f * (float)sin(ph); ph += 2 * M_PI * 440.0 / SR; }
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
}

static void rms_window(long from, long len, double *l, double *r)
{
	double a = 0, b = 0;
	for (long i = from; i < from + len; i++) { a += capL[i] * capL[i]; b += capR[i] * capR[i]; }
	*l = sqrt(a / len); *r = sqrt(b / len);
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

	const char *id = "streamerdesk_asmr_ear_to_ear";
	const char *disp = obs_source_get_display_name(id);
	CHECK(disp && strcmp(disp, "Streamerdesk ASMR - Susurro de oído a oído") == 0, "filtro registrado, nombre en espanol: %s", disp ? disp : "(null)");
	CHECK((obs_get_source_output_flags(id) & OBS_SOURCE_AUDIO) != 0, "flags OBS_SOURCE_AUDIO");

	obs_source_t *filter = obs_source_create(id, "Susurro", NULL, NULL);
	CHECK(filter != NULL, "obs_source_create del filtro");

	printf("Propiedades:\n");
	obs_properties_t *props = obs_source_properties(filter);
	int help = 0, n = count_props(props, &help, 1);
	CHECK(n >= 20 && help == n, "%d controles, %d con ayuda (?)", n, help);

	/* --- presets --- */
	obs_data_t *s = obs_source_get_settings(filter);
	obs_data_set_double(s, "depth", 42.0);
	obs_data_set_string(s, "sd_preset_name", "Mi preset de prueba");
	obs_source_update(filter, s);
	obs_property_button_clicked(obs_properties_get(props, "sd_preset_save"), filter);
	char path[512];
	snprintf(path, sizeof(path), "%s/streamerdesk-asmr-ear-to-ear/presets.json", argv[3]);
	char *json = os_quick_read_utf8_file(path);
	CHECK(json && strstr(json, "Mi preset de prueba") && strstr(json, "42"), "presets.json escrito en %s", path);
	bfree(json);
	obs_properties_destroy(props);
	props = obs_source_properties(filter);
	obs_property_t *list = obs_properties_get(props, "sd_preset");
	bool found = false;
	for (size_t i = 0; i < obs_property_list_item_count(list); i++)
		if (strcmp(obs_property_list_item_string(list, i), "u:Mi preset de prueba") == 0) found = true;
	CHECK(found && obs_property_list_item_count(list) == 10, "desplegable: ninguno + 8 integrados + 1 de usuario (%zu items)", obs_property_list_item_count(list));
	for (size_t i = 0; i < obs_property_list_item_count(list); i++)
		printf("     · %s\n", obs_property_list_item_name(list, i));
	obs_data_set_string(s, "sd_preset", "b:pegado_oreja_izq");
	obs_property_modified(list, s);
	CHECK(obs_data_get_int(s, "mode") == 0 && fabs(obs_data_get_double(s, "position") + 85.0) < 1e-6 &&
	      fabs(obs_data_get_double(s, "depth") - 70.0) < 1e-6,
	      "aplicar preset integrado 'Pegado a la oreja izquierda' (modo=%lld, posicion=%.0f, anchura vuelve a %.0f)",
	      obs_data_get_int(s, "mode"), obs_data_get_double(s, "position"), obs_data_get_double(s, "depth"));
	obs_data_set_string(s, "sd_preset", "u:Mi preset de prueba");
	obs_property_modified(list, s);
	CHECK(fabs(obs_data_get_double(s, "depth") - 42.0) < 1e-6 && obs_data_get_int(s, "mode") == 1,
	      "aplicar preset de usuario (anchura=%.0f, modo=%lld)", obs_data_get_double(s, "depth"), obs_data_get_int(s, "mode"));
	obs_property_button_clicked(obs_properties_get(props, "sd_preset_reset"), filter);
	obs_data_release(s);
	s = obs_source_get_settings(filter);
	CHECK(fabs(obs_data_get_double(s, "depth") - 70.0) < 1e-6, "Restablecer -> valores por defecto (anchura=%.0f)", obs_data_get_double(s, "depth"));
	obs_data_set_string(s, "sd_preset", "u:Mi preset de prueba");
	obs_property_button_clicked(obs_properties_get(props, "sd_preset_delete"), filter);
	json = os_quick_read_utf8_file(path);
	CHECK(json && !strstr(json, "Mi preset de prueba"), "Borrar preset");
	bfree(json);
	obs_properties_destroy(props);

	/* --- audio real: fuente mono -> filtro -> estereo --- */
	obs_register_source(&test_src);
	obs_source_t *src = obs_source_create("test_mono_src", "mic", NULL, NULL);
	obs_source_filter_add(src, filter);
	obs_set_output_source(0, src);
	obs_source_add_audio_capture_callback(src, on_audio, NULL);

	/* 1) manual, totalmente a la derecha, equal-power */
	obs_data_set_int(s, "mode", 0);
	obs_data_set_double(s, "position", 100.0);
	obs_data_set_int(s, "pan_mode", 0);
	obs_data_set_double(s, "proximity", 0.0);
	obs_source_update(filter, s);
	run_audio(src);
	double l, rr;
	rms_window(SR / 2, SR, &l, &rr);
	printf("Audio en OBS real (fuente MONO): %ld muestras capturadas\n", (long)cap_pos);
	CHECK(cap_pos > SR && rr > 0.2 && l < 0.002, "manual +100: oreja derecha RMS %.3f, izquierda %.5f", rr, l);

	/* 2) automatico: barrido de 0.5 s, anchura 100 %, senoidal, binaural */
	obs_data_set_int(s, "mode", 1);
	obs_data_set_int(s, "speed_unit", 1);
	obs_data_set_double(s, "sweep_s", 0.5);
	obs_data_set_double(s, "depth", 100.0);
	obs_data_set_int(s, "shape", 0);
	obs_data_set_int(s, "pan_mode", 1);
	obs_source_update(filter, s);
	run_audio(src);
	int lw = 0, rw = 0, changes = 0; char last = 0;
	float maxjump = 0;
	long N = cap_pos;
	for (long i = 1; i < N; i++) maxjump = fmaxf(maxjump, fmaxf(fabsf(capL[i] - capL[i - 1]), fabsf(capR[i] - capR[i - 1])));
	printf("  balance cada 100 ms (dB D-I):");
	for (long w = SR / 4; w + SR / 20 <= N; w += SR / 10) {
		rms_window(w, SR / 20, &l, &rr);
		double db = 20 * log10((rr + 1e-9) / (l + 1e-9));
		printf(" %+.0f", db);
		char side = db > 6 ? 'R' : (db < -6 ? 'L' : 0);
		if (side == 'L') lw++;
		if (side == 'R') rw++;
		if (side && last && side != last) changes++;
		if (side) last = side;
	}
	printf("\n");
	CHECK(lw >= 3 && rw >= 3 && changes >= 3, "la voz viaja de oreja a oreja (%d ventanas izq, %d der, %d cruces)", lw, rw, changes);
	CHECK(maxjump < 0.06f, "sin clics en OBS real: salto max entre muestras %.4f (seno 0.25 a 440 Hz ~0.02-0.03)", maxjump);

	obs_data_release(s);
	obs_source_remove_audio_capture_callback(src, on_audio, NULL);
	obs_set_output_source(0, NULL);
	obs_source_release(src);
	obs_source_release(filter);
	obs_shutdown();
	printf("\nRESULTADO INTEGRACION: %s\n", fails ? "FALLOS" : "OK");
	return fails ? 1 : 0;
}
