/*
 * Pruebas offline del nucleo DSP de "Susurro de oido a oido".
 * Compila: cc -O2 -I../../common/dsp test/test_ear.c src/ear_dsp.c -lm
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "../src/ear_dsp.h"

static int fails = 0;
#define CHECK(cond, ...)                          \
	do {                                      \
		if (cond) {                       \
			printf("  OK   ");        \
		} else {                          \
			printf("  FAIL ");        \
			fails++;                  \
		}                                 \
		printf(__VA_ARGS__);              \
		printf("\n");                     \
	} while (0)

#define SR 48000.0f

static void test_pan_law(void)
{
	printf("[1] Ley de panorama equal-power (gL^2+gR^2 = 2, centro = 1.0)\n");
	float maxerr = 0, gl, gr;
	for (int i = -100; i <= 100; i++) {
		ete_gains(0, i / 100.0f, 0.0f, 0.0f, &gl, &gr);
		float e = fabsf(gl * gl + gr * gr - 2.0f);
		if (e > maxerr)
			maxerr = e;
	}
	CHECK(maxerr < 1e-5f, "potencia constante, error max %.2e", maxerr);
	ete_gains(0, 0.0f, 0.0f, 0.0f, &gl, &gr);
	CHECK(fabsf(gl - 1) < 1e-5f && fabsf(gr - 1) < 1e-5f, "centro: L=%.4f R=%.4f", gl, gr);
	ete_gains(0, -1.0f, 0.0f, 0.0f, &gl, &gr);
	CHECK(gr < 1e-5f && fabsf(gl - 1.41421f) < 1e-4f, "extremo izq: L=%.4f R=%.6f", gl, gr);
	ete_gains(0, 1.0f, 0.0f, 0.0f, &gl, &gr);
	CHECK(gl < 1e-5f, "extremo der: L=%.6f R=%.4f", gl, gr);

	printf("[2] Ley binaural (ILD) con potencia constante y simetria\n");
	maxerr = 0;
	float symerr = 0, prev = -1;
	int mono = 1;
	for (int i = -100; i <= 100; i++) {
		float p = i / 100.0f, gl2, gr2;
		ete_gains(0, p, 1.0f, 0.6f, &gl, &gr);
		ete_gains(0, -p, 1.0f, 0.6f, &gl2, &gr2);
		maxerr = fmaxf(maxerr, fabsf(gl * gl + gr * gr - 2.0f));
		symerr = fmaxf(symerr, fabsf(gl - gr2) + fabsf(gr - gl2));
		if (prev >= 0 && gr < prev - 1e-6f)
			mono = 0;
		prev = gr;
	}
	CHECK(maxerr < 1e-4f, "potencia constante binaural, error max %.2e", maxerr);
	CHECK(symerr < 1e-5f, "simetria L/R, error max %.2e", symerr);
	CHECK(mono, "ganancia derecha monotona al moverse a la derecha");
	ete_gains(0, 1.0f, 1.0f, 0.6f, &gl, &gr);
	float ild = 20.0f * log10f(gr / gl);
	CHECK(fabsf(ild - 13.6f) < 0.05f, "ILD en extremo con intensidad 60%%: %.2f dB (esperado 13.6)", ild);
	CHECK(gl > 0.05f, "en binaural la oreja lejana sigue oyendo (L=%.3f)", gl);
}

static void test_itd(void)
{
	printf("[3] ITD (retardo interaural)\n");
	float d = ete_itd_samples(1.0f, 1.0f, SR, 1);
	CHECK(fabsf(d - 33.6f) < 0.01f, "ITD max a 48 kHz = %.2f muestras (0.70 ms)", d);
	CHECK(ete_itd_samples(1.0f, 1.0f, SR, 0) == 0.0f, "la oreja cercana no se retrasa");
	CHECK(ete_itd_samples(0.0f, 1.0f, SR, 1) == 0.0f && ete_itd_samples(0.0f, 1.0f, SR, 0) == 0.0f,
	      "en el centro ITD = 0 en ambas orejas");
	d = ete_itd_samples(-1.0f, 0.5f, SR, 0);
	CHECK(fabsf(d - 16.8f) < 0.01f, "intensidad 50%% -> %.2f muestras", d);
	CHECK(ete_itd_samples(1.0f, 1.0f, 192000.0f, 1) < ETE_RING - 2, "cabe en el buffer a 192 kHz");

	/* medida real: impulso con posicion fija a la derecha, binaural 100% */
	static ete_state s;
	ete_params p;
	ete_default_params(&p);
	p.mode = ETE_MODE_MANUAL;
	p.position = 1.0f;
	p.binaural = 1.0f;
	p.proximity = 0.0f;
	ete_init(&s, SR, &p);
	static float L[4800], R[4800];
	for (int i = 0; i < 4800; i++)
		L[i] = R[i] = 0;
	ete_process(&s, L, R, 2400); /* asentar suavizadores */
	for (int i = 0; i < 4800; i++)
		L[i] = R[i] = (i == 100) ? 1.0f : 0.0f;
	ete_process(&s, L, R, 4800);
	/* centroide de energia de cada oreja */
	double cl = 0, el = 0, cr = 0, er = 0;
	for (int i = 0; i < 4800; i++) {
		cl += i * (double)L[i] * L[i];
		el += (double)L[i] * L[i];
		cr += i * (double)R[i] * R[i];
		er += (double)R[i] * R[i];
	}
	double lag = cl / el - cr / er;
	printf("       retardo medido oreja izq (lejana) - der: %.2f muestras = %.3f ms\n", lag, lag / SR * 1000);
	CHECK(lag > 30.0 && lag < 45.0, "la oreja lejana llega ~0.7 ms tarde (incluye la sombra de cabeza)");
	CHECK(er > el * 4, "la oreja cercana tiene mas energia (%.1f dB)", 10 * log10(er / el));
}

static void test_curve(void)
{
	printf("[4] Curvas de movimiento\n");
	CHECK(fabsf(ete_curve(0.0, ETE_SHAPE_SINE, 0) + 1) < 1e-6f && fabsf(ete_curve(0.5, ETE_SHAPE_SINE, 0) - 1) < 1e-6f,
	      "seno: empieza en la izquierda y llega a la derecha a mitad de ciclo");
	int held = 0;
	for (int i = 0; i < 1000; i++)
		if (fabsf(ete_curve(i / 1000.0, ETE_SHAPE_HOLD, 0.4f)) > 0.999f)
			held++;
	CHECK(held > 350 && held < 450, "pausa en las orejas 40%%: %.1f%% del tiempo en el extremo", held / 10.0);
	float maxd = 0;
	for (int i = 0; i < 100000; i++) {
		float a = ete_curve(i / 100000.0, ETE_SHAPE_HOLD, 0.4f);
		float b = ete_curve((i + 1) / 100000.0, ETE_SHAPE_HOLD, 0.4f);
		maxd = fmaxf(maxd, fabsf(b - a));
	}
	CHECK(maxd < 5e-4f, "curva con pausa continua (salto max %.2e)", maxd);
}

/* recorre muchos escenarios buscando clics: diferencia de primer orden de la salida
 * con una senal suave (seno 220 Hz) comparada con la maxima esperable */
static void run_click_scenario(const char *name, ete_params *p, int change_every, void (*mut)(ete_params *, int))
{
	static ete_state s;
	ete_init(&s, SR, p);
	const int N = (int)(SR * 20);
	const int B = 480;
	static float L[480], R[480];
	float prevl = 0, prevr = 0, maxjump = 0, peak = 0, prevpos = s.last_pos, maxpj = 0;
	int nan = 0;
	double ph = 0;
	for (int n = 0, blk = 0; n < N; n += B, blk++) {
		if (mut && change_every && blk % change_every == 0) {
			mut(p, blk / change_every);
			ete_set_params(&s, p);
		}
		for (int i = 0; i < B; i++) {
			float x = 0.3f * sinf((float)ph);
			ph += 2 * SD_PI * 220.0 / SR;
			L[i] = x;
			R[i] = x;
		}
		ete_process(&s, L, R, B);
		for (int i = 0; i < B; i++) {
			if (!isfinite(L[i]) || !isfinite(R[i]))
				nan++;
			maxjump = fmaxf(maxjump, fmaxf(fabsf(L[i] - prevl), fabsf(R[i] - prevr)));
			peak = fmaxf(peak, fmaxf(fabsf(L[i]), fabsf(R[i])));
			prevl = L[i];
			prevr = R[i];
		}
		maxpj = fmaxf(maxpj, fabsf(s.last_pos - prevpos));
		prevpos = s.last_pos;
	}
	/* un seno 0.3 a 220 Hz con ganancia max 1.6 cambia como mucho 0.3*1.6*2*pi*220/48000 = 0.0138 por muestra */
	CHECK(nan == 0 && maxjump < 0.02f, "%-34s salto max %.4f (limite 0.02), pico %.3f", name, maxjump, peak);
	(void)maxpj;
}

static void mut_manual_jumps(ete_params *p, int k)
{
	p->position = (k % 2) ? 1.0f : -1.0f; /* saltos bruscos del deslizador */
}
static void mut_modes(ete_params *p, int k)
{
	p->mode = k % 4;
	p->pan_mode = (k / 2) % 2;
	p->start_right = (k / 3) % 2;
	p->sweep_s = 0.3f + (k % 5);
}
static void mut_everything(ete_params *p, int k)
{
	uint32_t h = (uint32_t)k * 2654435761u;
	p->mode = h % 4;
	p->shape = (h >> 3) % 2;
	p->dwell = ((h >> 5) % 10) / 11.0f;
	p->depth = ((h >> 9) % 11) / 10.0f;
	p->binaural = ((h >> 13) % 11) / 10.0f;
	p->proximity = ((h >> 17) % 11) / 10.0f;
	p->center_keep = ((h >> 21) % 11) / 10.0f;
	p->center_dip_db = (float)((h >> 25) % 7);
	p->pan_mode = (h >> 28) % 2;
	p->position = (((h >> 7) % 21) - 10) / 10.0f;
	p->sweep_s = 0.1f + ((h >> 11) % 8);
}

static void test_clicks(void)
{
	printf("[5] Sin discontinuidades (20 s por escenario, seno 220 Hz)\n");
	ete_params p;
	ete_default_params(&p);
	run_click_scenario("por defecto (auto, binaural)", &p, 0, NULL);
	ete_default_params(&p);
	p.sweep_s = 0.1f;
	p.depth = 1.0f;
	p.binaural = 1.0f;
	p.dwell = 0.0f;
	run_click_scenario("barrido extremo 0.1 s, binaural 100%", &p, 0, NULL);
	ete_default_params(&p);
	p.mode = ETE_MODE_RANDOM;
	p.sweep_s = 0.5f;
	run_click_scenario("aleatorio organico", &p, 0, NULL);
	ete_default_params(&p);
	p.mode = ETE_MODE_MANUAL;
	run_click_scenario("manual con saltos L<->R", &p, 10, mut_manual_jumps);
	ete_default_params(&p);
	run_click_scenario("cambio de modo/ley/lado", &p, 25, mut_modes);
	ete_default_params(&p);
	run_click_scenario("todos los parametros al azar", &p, 7, mut_everything);
}

static void test_mono_center(void)
{
	printf("[6] Centro transparente y suma mono\n");
	static ete_state s;
	ete_params p;
	ete_default_params(&p);
	p.mode = ETE_MODE_MANUAL;
	p.position = 0.0f;
	p.proximity = 0.0f;
	ete_init(&s, SR, &p);
	static float L[9600], R[9600];
	for (int i = 0; i < 9600; i++) {
		L[i] = 0.5f * sinf(i * 0.05f);
		R[i] = 0.0f; /* estereo desequilibrado: se suma a mono */
	}
	ete_process(&s, L, R, 9600);
	float err = 0;
	for (int i = 4800; i < 9600; i++)
		err = fmaxf(err, fabsf(L[i] - 0.25f * sinf(i * 0.05f)) + fabsf(R[i] - 0.25f * sinf(i * 0.05f)));
	CHECK(err < 1e-3f, "en el centro la salida es la suma mono intacta (error %.2e)", err);
}

int main(void)
{
	printf("Streamerdesk ASMR - Susurro de oido a oido: pruebas DSP\n");
	test_pan_law();
	test_itd();
	test_curve();
	test_clicks();
	test_mono_center();
	printf(fails ? "\nRESULTADO: %d FALLOS\n" : "\nRESULTADO: todas las pruebas OK\n", fails);
	return fails ? 1 : 0;
}
