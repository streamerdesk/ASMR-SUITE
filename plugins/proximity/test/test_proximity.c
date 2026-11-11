/*
 * Streamerdesk ASMR - Proximidad: pruebas offline del motor DSP (sin OBS).
 * Compilar: cc -O2 -Isrc -Icommon/dsp test/test_proximity.c src/proximity_dsp.c -lm
 */
#include "proximity_dsp.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SR 48000.0f
#define PI 3.14159265358979f
static int failures = 0;

#define CHECK(cond, ...)                              \
	do {                                          \
		printf("  [%s] ", (cond) ? "OK" : "FALLO"); \
		printf(__VA_ARGS__);                  \
		printf("\n");                         \
		if (!(cond))                          \
			failures++;                   \
	} while (0)

/* parametros "limpios": solo la parte que se quiere medir */
static void clean_params(prox_params *p)
{
	prox_default_params(p);
	p->prox_amount = 0.0f;
	p->air_db = 0.0f;
	p->hf_loss = 0.0f;
	p->level_loss = 0.0f;
	p->room = 0.0f;
	p->warmth = 0.0f;
	p->deharsh = 0;
	p->lowcut_hz = 20.0f;
	p->position = 0.0f;
}

static double db(double x)
{
	return 20.0 * log10(x + 1e-30);
}

/* RMS de salida (canal izq y der) para un seno a freq */
static void measure_sine(const prox_params *p, float freq, float amp, double *rms_l, double *rms_r)
{
	prox_state s;
	prox_init(&s, SR);
	prox_set_params(&s, p);
	prox_snap(&s);
	const size_t N = 48000, skip = 12000;
	float *l = malloc(N * sizeof(float)), *r = malloc(N * sizeof(float));
	for (size_t i = 0; i < N; i++)
		l[i] = r[i] = amp * sinf(2.0f * PI * freq * (float)i / SR);
	prox_process(&s, l, r, N);
	double al = 0, ar = 0;
	for (size_t i = skip; i < N; i++) {
		al += (double)l[i] * l[i];
		ar += (double)r[i] * r[i];
	}
	*rms_l = sqrt(al / (N - skip));
	*rms_r = sqrt(ar / (N - skip));
	free(l);
	free(r);
	prox_free(&s);
}

static double gain_db(const prox_params *p, float freq)
{
	double l, r;
	measure_sine(p, freq, 0.1f, &l, &r);
	return db(l / (0.1 / sqrt(2.0)));
}

static void test_shelf(void)
{
	printf("\n1) Efecto proximidad (low shelf) segun la distancia\n");
	prox_params p;
	clean_params(&p);
	p.prox_amount = 1.0f;
	p.prox_freq = 160.0f;
	float dists[] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
	double prev = 1e9;
	int mono = 1;
	for (int i = 0; i < 5; i++) {
		p.distance = dists[i];
		double lo = gain_db(&p, 80.0f), mid = gain_db(&p, 1000.0f);
		printf("     distancia %3.0f %%: 80 Hz %+6.2f dB | 1 kHz %+6.2f dB | mapeo %+6.2f dB\n",
		       dists[i] * 100, lo, mid, prox_shelf_db(dists[i], 1.0f));
		if (lo > prev + 0.01)
			mono = 0;
		prev = lo;
		if (i == 0) {
			CHECK(lo > 8.5 && lo < 11.5, "pegado al oido: graves realzados ~+10 dB (%.2f dB)", lo);
			CHECK(fabs(mid) < 1.0, "pegado al oido: 1 kHz casi sin tocar (%.2f dB)", mid);
		}
		if (i == 4)
			CHECK(lo < -2.0 && lo > -4.0, "lejos: graves algo reducidos ~-3 dB (%.2f dB)", lo);
	}
	CHECK(mono, "el realce de graves baja de forma monotona al alejarse");
	p.distance = 0.0f;
	p.prox_amount = 0.0f;
	CHECK(fabs(gain_db(&p, 80.0f)) < 0.3, "cantidad 0 %% = sin efecto proximidad");
}

static void test_attenuation(void)
{
	printf("\n2) Atenuacion por distancia (ley inversa) y perdida de agudos\n");
	prox_params p;
	clean_params(&p);
	p.level_loss = 1.0f;
	p.distance = 0.0f;
	double near = gain_db(&p, 1000.0f);
	p.distance = 1.0f;
	double far = gain_db(&p, 1000.0f);
	p.distance = 0.5f;
	double half = gain_db(&p, 1000.0f);
	printf("     1 kHz: pegado %+6.2f dB, mitad %+6.2f dB, lejos %+6.2f dB\n", near, half, far);
	CHECK(fabs(near) < 0.3, "pegado: sin atenuacion");
	CHECK(fabs(far - (-18.06)) < 0.6, "lejos con 100 %%: ~-18 dB (%.2f dB)", far);
	CHECK(fabs(half - prox_level_db(0.5f, 1.0f)) < 0.4, "mitad: coincide con 20log10(1+7d) (%.2f dB)", half);
	p.level_loss = 0.0f;
	p.distance = 1.0f;
	CHECK(fabs(gain_db(&p, 1000.0f)) < 0.3, "bajada de volumen 0 %% = mismo nivel lejos");

	clean_params(&p);
	p.hf_loss = 1.0f;
	p.distance = 0.0f;
	double hf_near = gain_db(&p, 9000.0f);
	p.distance = 1.0f;
	double hf_far = gain_db(&p, 9000.0f);
	double mid_far = gain_db(&p, 500.0f);
	printf("     9 kHz: pegado %+6.2f dB, lejos %+6.2f dB (500 Hz lejos %+6.2f dB)\n", hf_near, hf_far, mid_far);
	CHECK(hf_near > -1.0, "pegado: agudos intactos");
	CHECK(hf_far < -12.0, "lejos: agudos atenuados (mas oscuro)");
	CHECK(fabs(mid_far) < 1.0, "lejos: medios casi intactos");
}

static void test_binaural(void)
{
	printf("\n3) Posicion lateral binaural (ITD / ILD / sombra de la cabeza)\n");
	prox_params p;
	clean_params(&p);
	double l, r;
	measure_sine(&p, 1000.0f, 0.1f, &l, &r);
	CHECK(fabs(db(l) - db(r)) < 0.01, "centro + entrada mono: izquierda = derecha");

	p.position = -1.0f;
	p.distance = 0.0f;
	measure_sine(&p, 1000.0f, 0.1f, &l, &r);
	printf("     oido izquierdo pegado, 1 kHz: L %+6.2f dB, R %+6.2f dB\n", db(l / 0.0707), db(r / 0.0707));
	CHECK(db(l) - db(r) > 12.0, "oido izquierdo: el derecho oye mucho menos (ILD %.1f dB)", db(l) - db(r));
	p.position = 1.0f;
	measure_sine(&p, 1000.0f, 0.1f, &l, &r);
	CHECK(db(r) - db(l) > 12.0, "oido derecho: simetrico (ILD %.1f dB)", db(r) - db(l));

	/* ITD: impulso y mirar el retardo del oido lejano */
	prox_state s;
	prox_init(&s, SR);
	p.position = -1.0f;
	p.binaural = 1;
	prox_set_params(&s, &p);
	prox_snap(&s);
	float L[512] = {0}, R[512] = {0};
	L[10] = R[10] = 1.0f;
	prox_process(&s, L, R, 512);
	int pl = 0, pr = 0;
	for (int i = 1; i < 512; i++) {
		if (fabsf(L[i]) > fabsf(L[pl]))
			pl = i;
		if (fabsf(R[i]) > fabsf(R[pr]))
			pr = i;
	}
	double itd_ms = (pr - pl) / SR * 1000.0;
	printf("     ITD medido: %.3f ms (pico L %d, pico R %d)\n", itd_ms, pl, pr);
	CHECK(itd_ms > 0.5 && itd_ms < 0.9, "retardo interaural ~0,66 ms en el oido lejano");
	prox_free(&s);
}

static void test_room(void)
{
	printf("\n4) Ambiente de habitacion: crece con la distancia\n");
	prox_params p;
	clean_params(&p);
	p.room = 1.0f;
	double e[2];
	for (int k = 0; k < 2; k++) {
		p.distance = k ? 1.0f : 0.0f;
		prox_state s;
		prox_init(&s, SR);
		prox_set_params(&s, &p);
		prox_snap(&s);
		static float L[4800], R[4800];
		memset(L, 0, sizeof(L));
		memset(R, 0, sizeof(R));
		L[0] = R[0] = 1.0f;
		prox_process(&s, L, R, 4800);
		double acc = 0;
		for (int i = 240; i < 4800; i++) /* despues de 5 ms: solo reflexiones */
			acc += L[i] * L[i] + R[i] * R[i];
		e[k] = acc;
		prox_free(&s);
	}
	printf("     energia de reflexiones: pegado %.2e, lejos %.2e (%+.1f dB)\n", e[0], e[1],
	       10 * log10(e[1] / e[0]));
	CHECK(e[1] > e[0] * 10.0, "lejos hay bastante mas sala que pegado");
}

static void test_deharsh(void)
{
	printf("\n5) Suavizado de eses\n");
	prox_params p;
	clean_params(&p);
	p.distance = 0.0f;
	double off = gain_db(&p, 8000.0f);
	p.deharsh = 1;
	p.deharsh_amt = 1.0f;
	double on = gain_db(&p, 8000.0f);
	double mid = gain_db(&p, 500.0f);
	printf("     8 kHz puro: sin %+6.2f dB, con %+6.2f dB; 500 Hz con %+6.2f dB\n", off, on, mid);
	CHECK(on < off - 4.0, "una 's' fuerte se suaviza");
	CHECK(fabs(mid) < 0.5, "la voz sin eses no se toca");
}

/* --- barridos: buscar discontinuidades (clics) --- */
static size_t max_d2_at;
static float max_d2(const float *x, size_t n, size_t skip)
{
	float m = 0;
	for (size_t i = skip + 2; i < n; i++) {
		float d2 = fabsf(x[i] - 2 * x[i - 1] + x[i - 2]);
		if (d2 > m) {
			m = d2;
			max_d2_at = i;
		}
	}
	return m;
}

static unsigned rng = 12345;
static float frand(void)
{
	rng = rng * 1664525u + 1013904223u;
	return (float)(rng >> 8) / 16777216.0f;
}

static void random_params(prox_params *p)
{
	p->distance = frand();
	p->prox_amount = 2.0f * frand();
	p->prox_freq = 80.0f + 220.0f * frand();
	p->air_db = -6.0f + 12.0f * frand();
	p->hf_loss = frand();
	p->level_loss = frand();
	p->room = frand();
	float pm = frand();
	p->position = pm < 0.3f ? -1.0f : (pm < 0.6f ? 1.0f : (pm < 0.8f ? 0.0f : 2 * frand() - 1));
	p->binaural = frand() > 0.3f;
	p->warmth = frand();
	p->deharsh = frand() > 0.5f;
	p->deharsh_amt = frand();
	p->lowcut_hz = 20.0f + 280.0f * frand();
	p->mono_in = frand() > 0.5f;
	p->mix = frand();
	p->out_gain = 0.25f + 1.75f * frand();
}

static void test_sweeps(void)
{
	printf("\n6) Barridos de parametros sin discontinuidades\n");
	const size_t N = 48000 * 6;
	float *l = malloc(N * sizeof(float)), *r = malloc(N * sizeof(float));
	float *rl = malloc(N * sizeof(float)), *rr = malloc(N * sizeof(float));
	const float f0 = 220.0f, amp = 0.2f;

	/* (a) distancia 0 -> 100 -> 0 rapido (en bloques de 480 muestras como OBS) + posicion L <-> R */
	prox_params p;
	prox_default_params(&p);
	p.warmth = 0.0f; /* sin armonicos para que la segunda derivada sea limpia */
	p.deharsh = 0;
	prox_state s;
	prox_init(&s, SR);
	prox_set_params(&s, &p);
	prox_snap(&s);
	for (size_t i = 0; i < N; i++)
		l[i] = r[i] = amp * sinf(2.0f * PI * f0 * (float)i / SR);
	for (size_t off = 0; off < N; off += 480) {
		float t = (float)off / (float)N;
		p.distance = 0.5f - 0.5f * cosf(2 * PI * 4 * t); /* 4 idas y vueltas */
		/* saltos bruscos de posicion cada 0,5 s: izquierda, centro, derecha */
		int k = (int)(off / 24000) % 3;
		p.position = k == 0 ? -1.0f : (k == 1 ? 0.0f : 1.0f);
		p.binaural = ((off / 72000) & 1) == 0;
		prox_set_params(&s, &p);
		prox_process(&s, l + off, r + off, 480);
	}
	prox_free(&s);

	/* referencia: el mismo seno en estado fijo en las configuraciones extremas */
	float ref = 0.0f;
	for (int k = 0; k < 6; k++) {
		prox_params q = p;
		q.distance = (k & 1) ? 1.0f : 0.0f;
		q.position = (k >> 1) == 0 ? -1.0f : ((k >> 1) == 1 ? 0.0f : 1.0f);
		q.binaural = 1;
		prox_init(&s, SR);
		prox_set_params(&s, &q);
		prox_snap(&s);
		for (size_t i = 0; i < 48000; i++)
			rl[i] = rr[i] = amp * sinf(2.0f * PI * f0 * (float)i / SR);
		prox_process(&s, rl, rr, 48000);
		float m = fmaxf(max_d2(rl, 48000, 4800), max_d2(rr, 48000, 4800));
		if (m > ref)
			ref = m;
		prox_free(&s);
	}
	float swl = max_d2(l, N, 4800);
	size_t at = max_d2_at;
	float swr = max_d2(r, N, 4800);
	if (getenv("PROX_DEBUG"))
		printf("     (max L en muestra %zu = %.3f s)\n", at, at / SR);
	printf("     2a derivada max: barrido L %.5f R %.5f | estado fijo %.5f\n", swl, swr, ref);
	/* la modulacion legitima (volumen/ITD moviendose) sube algo la 2a derivada; un clic
	   la dispara. Control negativo: un salto de solo el 1 %% del nivel en la misma senal. */
	float saved = l[N / 2];
	for (size_t i = N / 2; i < N / 2 + 1; i++)
		l[i] += 0.01f * 0.37f;
	float with_click = max_d2(l, N, 4800);
	l[N / 2] = saved;
	printf("     control negativo (salto artificial del 1 %%): %.5f\n", with_click);
	CHECK(with_click > ref * 4.0f, "el detector si ve un clic minimo (control negativo)");
	CHECK(fmaxf(swl, swr) < ref * 2.0f, "barrido de distancia y posicion: sin clics (<2x estado fijo)");

	/* (b) cambios aleatorios de TODOS los parametros cada 10 ms, comprobando saltos */
	prox_init(&s, SR);
	prox_default_params(&p);
	prox_set_params(&s, &p);
	prox_snap(&s);
	for (size_t i = 0; i < N; i++)
		l[i] = r[i] = amp * sinf(2.0f * PI * f0 * (float)i / SR);
	int bad = 0;
	float maxabs = 0, maxstep = 0;
	for (size_t off = 0; off < N; off += 480) {
		random_params(&p);
		p.warmth = 0.0f;
		p.out_gain = 1.0f;
		prox_set_params(&s, &p);
		prox_process(&s, l + off, r + off, 480);
	}
	for (size_t i = 1; i < N; i++) {
		if (!isfinite(l[i]) || !isfinite(r[i]))
			bad++;
		float st = fmaxf(fabsf(l[i] - l[i - 1]), fabsf(r[i] - r[i - 1]));
		if (i > 4800 && st > maxstep)
			maxstep = st;
		maxabs = fmaxf(maxabs, fmaxf(fabsf(l[i]), fabsf(r[i])));
	}
	/* un seno de 220 Hz con amp 0,2 cambia como maximo 0,0058/muestra; con +20 dB de graves y
	   ganancias, el limite sano es ~0,1. Un clic (salto de estado) daria saltos >> 0,1 */
	printf("     aleatorio: salto max entre muestras %.4f, pico %.3f, no finitos %d\n", maxstep, maxabs, bad);
	CHECK(bad == 0, "sin NaN/Inf con cambios aleatorios");
	CHECK(maxstep < 0.1f, "sin saltos bruscos con cambios aleatorios de todos los controles");
	prox_free(&s);

	/* (c) estabilidad: silencio tras ruido fuerte -> decae a cero (sin denormales ni colas raras) */
	prox_init(&s, SR);
	prox_default_params(&p);
	p.room = 1.0f;
	p.warmth = 1.0f;
	p.prox_amount = 2.0f;
	prox_set_params(&s, &p);
	prox_snap(&s);
	for (size_t i = 0; i < 48000; i++)
		l[i] = r[i] = 2.0f * (frand() - 0.5f);
	prox_process(&s, l, r, 48000);
	memset(l, 0, 48000 * sizeof(float));
	memset(r, 0, 48000 * sizeof(float));
	prox_process(&s, l, r, 48000);
	float tail = 0;
	for (size_t i = 24000; i < 48000; i++)
		tail = fmaxf(tail, fmaxf(fabsf(l[i]), fabsf(r[i])));
	CHECK(tail < 1e-6f, "tras el ruido, el silencio vuelve a ser silencio (%.2e)", tail);
	prox_free(&s);

	free(l);
	free(r);
	free(rl);
	free(rr);
}

static void test_mono_output(void)
{
	printf("\n7) Salida mono (r = NULL) y mezcla 0 %%\n");
	prox_params p;
	prox_default_params(&p);
	p.mix = 0.0f;
	prox_state s;
	prox_init(&s, SR);
	prox_set_params(&s, &p);
	prox_snap(&s);
	float buf[1024], orig[1024];
	for (int i = 0; i < 1024; i++)
		orig[i] = buf[i] = 0.3f * sinf(i * 0.05f);
	prox_process(&s, buf, NULL, 1024);
	float md = 0;
	for (int i = 0; i < 1024; i++)
		md = fmaxf(md, fabsf(buf[i] - orig[i]));
	CHECK(md < 1e-6f, "mezcla 0 %% = sonido original intacto (dif %.2e)", md);
	prox_free(&s);
}

int main(void)
{
	printf("Streamerdesk ASMR - Proximidad: pruebas del motor DSP (%.0f Hz)\n", SR);
	test_shelf();
	test_attenuation();
	test_binaural();
	test_room();
	test_deharsh();
	test_sweeps();
	test_mono_output();
	printf("\n%s (%d fallos)\n", failures ? "HAY FALLOS" : "TODO OK", failures);
	return failures ? 1 : 0;
}
