/* Prueba offline del nucleo DSP (sin OBS). */
#include "../src/pingpong_dsp.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { failures++; printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static size_t argmax_abs(const float *x, size_t a, size_t b, float *val)
{
	size_t best = a; float bv = 0;
	for (size_t i = a; i < b; i++) if (fabsf(x[i]) > bv) { bv = fabsf(x[i]); best = i; }
	*val = bv; return best;
}

static void run_impulse(const char *name, float sr, float delay_ms, float fb, int start_right, int stereo_input)
{
	printf("== %s (sr=%.0f, delay=%.0f ms, fb=%.2f, start=%s, input=%s)\n", name, sr, delay_ms, fb,
	       start_right ? "R" : "L", stereo_input ? "impulso solo en L, modo mono-sum" : "impulso dual-mono");
	ppe_state s;
	if (ppe_init(&s, sr) != 0) { printf("init failed\n"); failures++; return; }
	ppe_params p; ppe_default_params(&p);
	p.delay_l_ms = p.delay_r_ms = delay_ms;
	p.fb_l = p.fb_r = fb;
	p.mix = 1.0f;            /* solo humedo */
	p.damp_hz = 20000.0f;    /* filtros fuera para medir posiciones exactas */
	p.lowcut_hz = 10.0f;
	p.resonance = 0.0f;
	p.saturate = 0;
	p.start_right = start_right;
	ppe_set_params(&s, &p);

	size_t D = (size_t)lroundf(delay_ms * 0.001f * sr);
	size_t N = D * 7;
	float *L = calloc(N, sizeof(float)), *R = calloc(N, sizeof(float));
	L[0] = 1.0f; if (!stereo_input) R[0] = 1.0f;
	/* procesar en bloques de 1024 como OBS */
	for (size_t off = 0; off < N; off += 1024) {
		size_t n = (N - off) < 1024 ? (N - off) : 1024;
		ppe_process(&s, L + off, R + off, n);
	}
	float expected_amp = stereo_input ? 0.5f : 1.0f;
	float prev = 1e9f;
	for (int k = 1; k <= 6; k++) {
		size_t c = D * k, a = c - D / 2, b = c + D / 2 < N ? c + D / 2 : N;
		float vl, vr;
		size_t il = argmax_abs(L, a, b, &vl), ir = argmax_abs(R, a, b, &vr);
		int left_expected = ((k % 2) == 1) != (start_right != 0);
		const char *side = vl > vr ? "L" : "R";
		size_t pos = vl > vr ? il : ir;
		float amp = vl > vr ? vl : vr, other = vl > vr ? vr : vl;
		printf("  repeticion %d: muestra %zu (esperada %zu) lado %s amp %.4f (esperada %.4f) fuga lado opuesto %.2e\n",
		       k, pos, c, side, amp, expected_amp, other);
		CHECK(pos == c, "posicion %zu != %zu", pos, c);
		CHECK((left_expected && vl > vr) || (!left_expected && vr > vl), "lado incorrecto en repeticion %d", k);
		CHECK(fabsf(amp - expected_amp) < 1e-3f, "amplitud %.5f != %.5f", amp, expected_amp);
		CHECK(other < 1e-6f, "fuga al lado opuesto %.3e", other);
		CHECK(amp < prev, "no decae");
		prev = amp; expected_amp *= fb;
	}
	free(L); free(R); ppe_free(&s);
}

static void run_plain_stereo(void)
{
	printf("== pingpong=0 (eco estereo normal): impulso en L debe repetirse solo en L\n");
	ppe_state s; ppe_init(&s, 48000);
	ppe_params p; ppe_default_params(&p);
	p.pingpong = 0; p.mono_in = 0; p.mix = 1; p.damp_hz = 20000; p.lowcut_hz = 10; p.saturate = 0;
	p.delay_l_ms = p.delay_r_ms = 100; p.fb_l = p.fb_r = 0.5f;
	ppe_set_params(&s, &p);
	size_t N = 4800 * 4; float *L = calloc(N, 4), *R = calloc(N, 4); L[0] = 1;
	ppe_process(&s, L, R, N);
	float er = 0; for (size_t i = 0; i < N; i++) er += R[i] * R[i];
	printf("  L[4800]=%.3f L[9600]=%.3f L[14400]=%.3f energia R=%.2e\n", L[4800], L[9600], L[14400], er);
	CHECK(fabsf(L[4800] - 1) < 1e-4 && fabsf(L[9600] - .5f) < 1e-4 && er < 1e-10, "eco estereo normal incorrecto");
	free(L); free(R); ppe_free(&s);
}

static void run_stability(void)
{
	printf("== estabilidad: ruido, fb 120%%, saturacion on, filtros resonantes, cambios bruscos de parametros\n");
	ppe_state s; ppe_init(&s, 44100);
	ppe_params p; ppe_default_params(&p);
	p.fb_l = p.fb_r = 1.2f; p.resonance = 1.0f; p.damp_hz = 1200; p.mix = 1;
	ppe_set_params(&s, &p);
	float L[512], R[512]; float peak = 0; int bad = 0; unsigned seed = 1;
	for (int blk = 0; blk < 2000; blk++) {
		for (int i = 0; i < 512; i++) { seed = seed * 1664525u + 1013904223u; L[i] = R[i] = ((seed >> 9) / 4194304.0f - 1.0f) * 0.5f; }
		if (blk % 100 == 0) { p.delay_l_ms = 1 + (blk * 37 % 4000); p.delay_r_ms = 4000 - (blk * 13 % 3999); ppe_set_params(&s, &p); }
		ppe_process(&s, L, R, 512);
		for (int i = 0; i < 512; i++) { if (!isfinite(L[i]) || !isfinite(R[i])) bad++; if (fabsf(L[i]) > peak) peak = fabsf(L[i]); }
	}
	printf("  muestras no finitas: %d, pico de salida: %.3f\n", bad, peak);
	CHECK(bad == 0 && peak < 4.0f, "inestable");
	ppe_free(&s);
}

int main(void)
{
	run_impulse("ping-pong mono-sum dual-mono", 48000, 100, 0.5f, 0, 0);
	run_impulse("ping-pong empieza a la derecha", 48000, 100, 0.5f, 1, 0);
	run_impulse("ping-pong 44.1k, fb 0.7, entrada solo L", 44100, 250, 0.7f, 0, 1);
	run_plain_stereo();
	run_stability();
	printf(failures ? "\nRESULTADO: %d FALLOS\n" : "\nRESULTADO: TODAS LAS PRUEBAS OK\n", failures);
	return failures ? 1 : 0;
}
