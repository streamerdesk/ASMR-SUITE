/* Pruebas offline del nucleo DSP de la reverb (sin OBS). */
#include "../src/reverb_dsp.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { failures++; printf("  FALLO: "); } else printf("  ok: "); printf(__VA_ARGS__); printf("\n"); } while (0)

static void base_params(rvb_params *p)
{
	rvb_default_params(p);
	p->mix = 1.0f;          /* solo humedo */
	p->duck = 0.0f;
	p->lowcut_hz = 20.0f;   /* filtros de salida fuera */
	p->highcut_hz = 20000.0f;
	p->damping = 0.0f;
	p->modulation = 0.2f;
	p->er_late = 1.0f;      /* solo cola para medir RT60 */
	p->predelay_ms = 0.0f;
}

static int count_bad(const float *x, size_t n)
{
	int bad = 0;
	for (size_t i = 0; i < n; i++)
		if (!isfinite(x[i]) || (x[i] != 0.0f && fabsf(x[i]) < 1.17549435e-38f)) bad++;
	return bad;
}

/* RT60 por integracion de Schroeder, ajuste entre -5 y -25 dB (T20 x 3) */
static double measure_rt60(float sr, float decay, float size)
{
	rvb_state s; rvb_init(&s, sr);
	rvb_params p; base_params(&p); p.decay_s = decay; p.size = size;
	rvb_set_params(&s, &p);
	size_t N = (size_t)(sr * (decay * 1.6f + 0.5f));
	float *L = calloc(N, 4), *R = calloc(N, 4);
	L[0] = R[0] = 1.0f;
	for (size_t o = 0; o < N; o += 512) rvb_process(&s, L + o, R + o, N - o < 512 ? N - o : 512);
	double *edc = malloc(N * sizeof(double)), acc = 0;
	for (size_t i = N; i-- > 0;) { acc += (double)L[i] * L[i] + (double)R[i] * R[i]; edc[i] = acc; }
	double t5 = -1, t25 = -1;
	for (size_t i = 0; i < N; i++) {
		double db = 10 * log10(edc[i] / edc[0] + 1e-30);
		if (t5 < 0 && db <= -5) t5 = i / (double)sr;
		if (t25 < 0 && db <= -25) { t25 = i / (double)sr; break; }
	}
	int bad = count_bad(L, N) + count_bad(R, N);
	free(L); free(R); free(edc); rvb_free(&s);
	if (bad) return -1;
	return (t25 - t5) * 3.0;
}

static void test_rt60(void)
{
	printf("== RT60 medido vs ajustado (impulso, cola, sin amortiguacion)\n");
	float cases[][3] = {{48000, 0.5f, 0.3f}, {48000, 2.0f, 0.5f}, {48000, 5.0f, 0.8f}, {44100, 1.2f, 0.45f}, {96000, 3.0f, 1.0f}};
	for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		double rt = measure_rt60(cases[i][0], cases[i][1], cases[i][2]);
		double err = (rt - cases[i][1]) / cases[i][1] * 100.0;
		CHECK(rt > 0 && fabs(err) < 20.0, "sr=%.0f decay=%.1fs size=%.0f%% -> RT60 medido %.2f s (%+.1f%%)",
		      cases[i][0], cases[i][1], cases[i][2] * 100, rt, err);
	}
}

static void test_decorrelation(void)
{
	printf("== Decorrelacion estereo (entrada mono, cola)\n");
	float widths[] = {1.0f, 0.0f};
	for (int k = 0; k < 2; k++) {
		rvb_state s; rvb_init(&s, 48000);
		rvb_params p; base_params(&p); p.decay_s = 2.0f; p.width = widths[k]; p.er_late = 0.5f;
		rvb_set_params(&s, &p);
		size_t N = 48000 * 2;
		float *L = calloc(N, 4), *R = calloc(N, 4);
		L[0] = R[0] = 1.0f;
		rvb_process(&s, L, R, N);
		double sll = 0, srr = 0, slr = 0;
		for (size_t i = 2400; i < N; i++) { sll += L[i] * L[i]; srr += R[i] * R[i]; slr += L[i] * R[i]; }
		double c = slr / sqrt(sll * srr + 1e-30);
		if (k == 0)
			CHECK(fabs(c) < 0.25, "ancho 100%%: correlacion L/R = %+.3f (|c| < 0.25), balance L/R %.2f dB", c, 10 * log10(sll / srr));
		else
			CHECK(c > 0.999, "ancho 0%%: correlacion L/R = %+.4f (mono)", c);
		free(L); free(R); rvb_free(&s);
	}
}

static void test_denormals_silence(void)
{
	printf("== Sin NaN / denormales tras 60 s de silencio (decay 10 s, amortiguacion maxima)\n");
	rvb_state s; rvb_init(&s, 48000);
	rvb_params p; base_params(&p); p.decay_s = 10.0f; p.damping = 1.0f; p.lowcut_hz = 180; p.highcut_hz = 9000;
	rvb_set_params(&s, &p);
	float L[480], R[480]; int bad = 0; float last = 0;
	for (int b = 0; b < 6000; b++) {
		memset(L, 0, sizeof(L)); memset(R, 0, sizeof(R));
		if (b == 0) L[0] = R[0] = 1.0f;
		rvb_process(&s, L, R, 480);
		bad += count_bad(L, 480) + count_bad(R, 480);
		last = fmaxf(fabsf(L[479]), fabsf(R[479]));
	}
	int st_bad = 0;
	for (int i = 0; i < RVB_LINES; i++) {
		float v = s.line[i].lp; if (v != 0.0f && fabsf(v) < 1.17549435e-38f) st_bad++;
		for (size_t j = 0; j <= s.line[i].mask; j++) { float x = s.line[i].buf[j]; if (!isfinite(x) || (x != 0.0f && fabsf(x) < 1.17549435e-38f)) st_bad++; }
	}
	CHECK(bad == 0 && st_bad == 0, "muestras malas en salida: %d, en estado interno: %d, ultimo valor %.2e", bad, st_bad, last);
	rvb_free(&s);
}

static double rms(const float *x, size_t n) { double a = 0; for (size_t i = 0; i < n; i++) a += x[i] * x[i]; return sqrt(a / n); }

static void test_freeze(void)
{
	printf("== Congelar: la cola se mantiene y no explota\n");
	rvb_state s; rvb_init(&s, 48000);
	rvb_params p; base_params(&p); p.decay_s = 1.0f; p.damping = 0.5f;
	rvb_set_params(&s, &p);
	size_t blk = 4800; float L[4800], R[4800]; unsigned seed = 7;
	for (int b = 0; b < 5; b++) { /* 0.5 s de ruido */
		for (size_t i = 0; i < blk; i++) { seed = seed * 1664525u + 1013904223u; L[i] = R[i] = ((seed >> 9) / 4194304.0f - 1.0f) * 0.3f; }
		rvb_process(&s, L, R, blk);
	}
	p.freeze = 1; rvb_set_params(&s, &p);
	double first = 0, lastv = 0; int bad = 0;
	for (int b = 0; b < 60; b++) { /* 6 s congelado, entrando ruido (no debe entrar) */
		for (size_t i = 0; i < blk; i++) { seed = seed * 1664525u + 1013904223u; L[i] = R[i] = b < 30 ? ((seed >> 9) / 4194304.0f - 1.0f) * 0.3f : 0.0f; }
		p.mix = 1.0f; rvb_process(&s, L, R, blk);
		bad += count_bad(L, blk);
		double v = rms(L, blk);
		if (b == 2) first = v;
		lastv = v;
	}
	double drop = 20 * log10(lastv / first);
	CHECK(bad == 0 && drop > -6.0 && drop < 1.0, "nivel tras 6 s congelado: %+.2f dB respecto al inicio", drop);
	p.freeze = 0; rvb_set_params(&s, &p);
	for (int b = 0; b < 30; b++) { memset(L, 0, sizeof(L)); memset(R, 0, sizeof(R)); rvb_process(&s, L, R, blk); }
	CHECK(rms(L, blk) < lastv * 0.01, "al descongelar la cola decae (%.2e)", rms(L, blk));
	rvb_free(&s);
}

static void test_ducking(void)
{
	printf("== Ducking: la reverb baja mientras hay voz y vuelve despues\n");
	float res[2];
	for (int k = 0; k < 2; k++) {
		rvb_state s; rvb_init(&s, 48000);
		rvb_params p; base_params(&p); p.decay_s = 3.0f; p.duck = k ? 0.8f : 0.0f; p.mix = 0.5f;
		rvb_set_params(&s, &p);
		size_t N = 48000; float *L = calloc(N, 4), *R = calloc(N, 4), *in = calloc(N, 4);
		for (size_t i = 0; i < N; i++) in[i] = 0.1f * sinf(2 * 3.14159265f * 300 * i / 48000.0f);
		memcpy(L, in, N * 4); memcpy(R, in, N * 4);
		rvb_process(&s, L, R, N);
		double wet = 0; for (size_t i = N / 2; i < N; i++) { double d = L[i] - in[i]; wet += d * d; }
		res[k] = (float)sqrt(wet / (N / 2));
		free(L); free(R); free(in); rvb_free(&s);
	}
	double red = 20 * log10(res[1] / res[0]);
	CHECK(red < -10.0 && red > -20.0, "con voz presente, ducking 80%% reduce la reverb %.1f dB (esperado ~ -14 dB)", red);
}

static void test_clicks(void)
{
	printf("== Sin clics al mover parametros bruscamente (seno 220 Hz, mezcla 50%%)\n");
	double maxd[2];
	for (int sweep = 0; sweep < 2; sweep++) {
		rvb_state s; rvb_init(&s, 48000);
		rvb_params p; rvb_default_params(&p); p.mix = 0.5f; p.duck = 0.0f; p.highcut_hz = 6000;
		rvb_set_params(&s, &p);
		float L[256], R[256]; double ph = 0, md = 0; float prev[2] = {0, 0};
		for (int b = 0; b < 1500; b++) { /* 8 s */
			for (int i = 0; i < 256; i++) { L[i] = R[i] = 0.3f * (float)sin(ph); ph += 2 * 3.14159265358979 * 220 / 48000; }
			if (sweep && b % 25 == 0 && b > 100) {
				int k = b / 25;
				p.size = (k % 2) ? 1.0f : 0.0f;
				p.decay_s = (k % 3) ? 10.0f : 0.2f;
				p.predelay_ms = (k % 2) ? 200.0f : 0.0f;
				p.damping = (k % 4) / 3.0f;
				p.diffusion = (k % 2) ? 1.0f : 0.0f;
				p.er_late = (k % 3) / 2.0f;
				p.width = (k % 2) ? 1.5f : 0.0f;
				p.modulation = (k % 2) ? 1.0f : 0.0f;
				p.lowcut_hz = (k % 2) ? 1000.0f : 20.0f;
				p.mix = (k % 2) ? 1.0f : 0.0f;
				p.freeze = (k % 5) == 0;
				rvb_set_params(&s, &p);
			}
			rvb_process(&s, L, R, 256);
			if (b > 200) /* tras el transitorio inicial */
				for (int i = 0; i < 256; i++) {
					double d = fabs(L[i] - prev[0]); if (d > md) md = d;
					prev[0] = L[i];
				}
			else prev[0] = L[255];
		}
		maxd[sweep] = md;
		rvb_free(&s);
	}
	/* un seno de 0.3 a 220 Hz cambia como mucho 0.3*2*pi*220/48000 = 0.0086 por muestra */
	CHECK(maxd[1] < 0.05, "salto maximo entre muestras: estable %.4f, con barridos bruscos %.4f (< 0.05; un clic seria > 0.1)", maxd[0], maxd[1]);
}

static void test_cpu(void)
{
	rvb_state s; rvb_init(&s, 48000);
	rvb_params p; rvb_default_params(&p); rvb_set_params(&s, &p);
	float L[1024], R[1024]; for (int i = 0; i < 1024; i++) L[i] = R[i] = 0.01f * (i % 7);
	struct timespec a, b; clock_gettime(CLOCK_MONOTONIC, &a);
	int blocks = 48000 * 20 / 1024; /* 20 s de audio */
	for (int k = 0; k < blocks; k++) rvb_process(&s, L, R, 1024);
	clock_gettime(CLOCK_MONOTONIC, &b);
	double t = (b.tv_sec - a.tv_sec) + (b.tv_nsec - a.tv_nsec) * 1e-9;
	printf("== CPU: 20 s de audio estereo 48 kHz en %.3f s (%.2f%% de un nucleo)\n", t, t / 20.0 * 100);
	CHECK(t / 20.0 < 0.05, "uso de CPU < 5%% de un nucleo");
	rvb_free(&s);
}

int main(void)
{
	test_rt60();
	test_decorrelation();
	test_denormals_silence();
	test_freeze();
	test_ducking();
	test_clicks();
	test_cpu();
	printf(failures ? "\nRESULTADO: %d FALLOS\n" : "\nRESULTADO: TODAS LAS PRUEBAS OK\n", failures);
	return failures ? 1 : 0;
}
