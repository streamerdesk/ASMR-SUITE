/* Pruebas offline del nucleo DSP de 8D / Orbita (sin OBS). */
#include "../src/orbit_dsp.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SR 48000.0f
static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { failures++; printf("  FALLO: "); } else printf("  ok: "); printf(__VA_ARGS__); printf("\n"); } while (0)

static unsigned seed = 12345u;
static float noise(void) { seed = seed * 1664525u + 1013904223u; return ((float)(seed >> 8) / 8388608.0f) - 1.0f; }

static void static_params(orb_params *p, float az)
{
	orb_default_params(p);
	p->mode = ORB_ORBIT; p->period_s = 1e6f; p->start_deg = az;
	p->elevation = 0; p->room = 0; p->near_ears = 0; p->binaural = 1; p->front_back = 1; p->mix = 1;
	p->distance_m = 1.0f;
}

static int count_bad(const float *x, size_t n)
{
	int bad = 0;
	for (size_t i = 0; i < n; i++)
		if (!isfinite(x[i]) || (x[i] != 0.0f && fabsf(x[i]) < 1.17549435e-38f)) bad++;
	return bad;
}

typedef struct { int lag; double ild_db, hf_db, rms; } meas;

/* ruido mono 1 s a traves de una posicion fija; lag > 0 = el oido izquierdo llega tarde */
static meas measure_static(const orb_params *p)
{
	orb_state s; orb_init(&s, SR); orb_set_params(&s, p);
	size_t N = (size_t)SR;
	float *L = malloc(N * 4), *R = malloc(N * 4);
	for (size_t i = 0; i < N; i++) L[i] = R[i] = 0.3f * noise();
	for (size_t o = 0; o < N; o += 480) orb_process(&s, L + o, R + o, 480);
	meas m = {0};
	size_t a = 9600;
	double best = -1e30, el = 0, er = 0;
	for (int lag = -40; lag <= 40; lag++) {
		double c = 0;
		for (size_t i = a + 40; i < N - 40; i++) c += (double)L[i] * R[i - lag];
		if (c > best) { best = c; m.lag = lag; }
	}
	sd_svf hp[2] = {{0}}, lp[2] = {{0}}; sd_svf_coefs c, cl;
	sd_svf_set(&c, SR, 5000.0f, 1.414f); sd_svf_set(&cl, SR, 1000.0f, 1.414f);
	double hf = 0, lf = 0;
	for (size_t i = 0; i < N; i++) {
		float hl = sd_svf_hp(&hp[0], &c, L[i]), hr = sd_svf_hp(&hp[1], &c, R[i]);
		float ll = sd_svf_lp(&lp[0], &cl, L[i]), lr = sd_svf_lp(&lp[1], &cl, R[i]);
		if (i >= a) { el += (double)L[i] * L[i]; er += (double)R[i] * R[i]; hf += hl * hl + hr * hr; lf += ll * ll + lr * lr; }
	}
	m.ild_db = 10 * log10(er / el);
	m.hf_db = 10 * log10(hf / lf); /* agudos (>5 kHz) relativos a graves (<1 kHz) */
	m.rms = sqrt((el + er) / (2.0 * (N - a)));
	free(L); free(R); orb_free(&s);
	return m;
}

static void test_binaural(void)
{
	printf("== Modelo binaural (ITD, ILD, sombra de la cabeza, delante/detras)\n");
	orb_params p;
	static_params(&p, 90); meas r90 = measure_static(&p);
	static_params(&p, -90); meas l90 = measure_static(&p);
	static_params(&p, 0); meas f0 = measure_static(&p);
	static_params(&p, 180); meas b180 = measure_static(&p);
	static_params(&p, 30); meas r30 = measure_static(&p);
	static_params(&p, 60); meas r60 = measure_static(&p);
	CHECK(r90.lag >= 28 && r90.lag <= 34, "fuente a la derecha (90): el oido izquierdo llega %d muestras tarde (%.2f ms; esperado ~0,65 ms)", r90.lag, r90.lag / 48.0);
	CHECK(r90.ild_db > 6.0, "fuente a la derecha: oido derecho %.1f dB mas fuerte", r90.ild_db);
	CHECK(l90.lag == -r90.lag && fabs(l90.ild_db + r90.ild_db) < 0.3, "izquierda simetrica (lag %d, ILD %.1f dB)", l90.lag, l90.ild_db);
	CHECK(abs(f0.lag) <= 1 && fabs(f0.ild_db) < 0.3, "delante (0): sin ITD (%d) ni ILD (%.2f dB)", f0.lag, f0.ild_db);
	CHECK(r30.lag > 0 && r30.lag < r60.lag && r60.lag < r90.lag, "ITD crece con el angulo: 30->%d, 60->%d, 90->%d muestras", r30.lag, r60.lag, r90.lag);
	CHECK(abs(b180.lag) <= 1 && fabs(b180.ild_db) < 0.3, "detras (180): sin ITD (%d) ni ILD (%.2f dB)", b180.lag, b180.ild_db);
	CHECK(f0.hf_db - b180.hf_db > 4.0, "detras suena mas oscuro: agudos >5 kHz (relativos a <1 kHz) %.1f dB menos que delante", f0.hf_db - b180.hf_db);
	static_params(&p, 0); p.front_back = 0; meas f0n = measure_static(&p);
	static_params(&p, 180); p.front_back = 0; meas b180n = measure_static(&p);
	CHECK(fabs(f0n.hf_db - b180n.hf_db) < 0.5, "con delante/detras = 0 la diferencia desaparece (%.2f dB)", f0n.hf_db - b180n.hf_db);
	static_params(&p, 90); p.binaural = 0; meas flat = measure_static(&p);
	CHECK(flat.lag == 0 && fabs(flat.ild_db) < 0.1, "intensidad binaural 0 -> centrado (lag %d, ILD %.2f dB)", flat.lag, flat.ild_db);
	static_params(&p, 90); p.distance_m = 0.25f; meas near = measure_static(&p);
	CHECK(near.ild_db > r90.ild_db + 2.0, "campo cercano (25 cm): ILD mayor (%.1f dB vs %.1f dB a 1 m)", near.ild_db, r90.ild_db);
	static_params(&p, 0); p.distance_m = 4.0f; meas far = measure_static(&p);
	static_params(&p, 0); p.distance_m = 0.25f; meas nr = measure_static(&p);
	CHECK(20 * log10(nr.rms / far.rms) > 10.0, "distancia: 25 cm suena %.1f dB mas fuerte que 4 m", 20 * log10(nr.rms / far.rms));
}

/* simula y registra el azimut cada 10 ms */
static int run_motion(orb_params *p, float seconds, float *az, int with_audio, double *ild_track)
{
	orb_state s; orb_init(&s, SR); orb_set_params(&s, p);
	int nb = (int)(seconds * 100);
	float L[480], R[480];
	for (int b = 0; b < nb; b++) {
		for (int i = 0; i < 480; i++) L[i] = R[i] = with_audio ? 0.3f * noise() : 0.0f;
		orb_process(&s, L, R, 480);
		az[b] = orb_current_azimuth_deg(&s);
		if (ild_track) {
			double el = 0, er = 0;
			for (int i = 0; i < 480; i++) { el += L[i] * L[i]; er += R[i] * R[i]; }
			ild_track[b] = 10 * log10((er + 1e-12) / (el + 1e-12));
		}
	}
	orb_free(&s);
	return nb;
}

static double unwrap_total(const float *az, int n, float *maxstep)
{
	double tot = 0; *maxstep = 0;
	for (int i = 1; i < n; i++) {
		double d = az[i] - az[i - 1];
		while (d > 180) d -= 360;
		while (d < -180) d += 360;
		tot += d;
		if (fabs(d) > *maxstep) *maxstep = (float)fabs(d);
	}
	return tot;
}

static void test_motion(void)
{
	printf("== Movimiento (orbita, sentido, arco, deriva, tempo, elevacion, sesgo a los oidos)\n");
	static float az[100 * 70];
	static double ild[100 * 70];
	orb_params p; static_params(&p, 0); p.period_s = 4.0f;
	int n = run_motion(&p, 8.0f, az, 1, ild);
	float ms; double tot = unwrap_total(az, n, &ms);
	CHECK(fabs(tot - 720.0) < 25.0, "orbita 4 s/vuelta, horario: %.0f grados en 8 s (esperado 720)", tot);
	CHECK(ild[100] > 4.0 && ild[300] < -4.0, "a 1 s suena a la derecha (ILD %+.1f dB), a 3 s a la izquierda (%+.1f dB)", ild[100], ild[300]);
	p.dir = -1;
	n = run_motion(&p, 8.0f, az, 0, NULL);
	tot = unwrap_total(az, n, &ms);
	CHECK(fabs(tot + 720.0) < 25.0, "antihorario: %.0f grados", tot);

	static_params(&p, -90); p.mode = ORB_ARC; p.arc_deg = 180; p.period_s = 6.0f;
	n = run_motion(&p, 12.0f, az, 0, NULL);
	float mn = 1e9f, mx = -1e9f;
	for (int i = 0; i < n; i++) { if (az[i] < mn) mn = az[i]; if (az[i] > mx) mx = az[i]; }
	CHECK(mn > -92 && mn < -85 && mx > 85 && mx < 92, "arco de -90 a +90 pasando por delante: rango %.0f..%.0f grados", mn, mx);

	CHECK(fabsf(orb_tempo_period(120, 2) - 4.0f) < 1e-5f && fabsf(orb_tempo_period(90, 4) - 10.6667f) < 1e-3f,
	      "tempo: 120 BPM x 2 compases = %.2f s/vuelta; 90 BPM x 4 = %.2f s", orb_tempo_period(120, 2), orb_tempo_period(90, 4));

	static_params(&p, 0); p.mode = ORB_WANDER; p.period_s = 10.0f;
	n = run_motion(&p, 60.0f, az, 0, NULL);
	double travel = 0; for (int i = 1; i < n; i++) { double d = az[i] - az[i - 1]; while (d > 180) d -= 360; while (d < -180) d += 360; travel += fabs(d); }
	tot = unwrap_total(az, n, &ms);
	CHECK(travel > 720 && ms < 3.0f, "deriva aleatoria: recorre %.0f grados en 60 s, paso maximo %.2f grados/10 ms (continuo)", travel, ms);

	static_params(&p, 0); p.elevation = 1.0f; p.period_s = 4.0f;
	{
		orb_state s; orb_init(&s, SR); orb_set_params(&s, &p);
		float L[480] = {0}, R[480] = {0}; float emn = 0, emx = 0;
		for (int b = 0; b < 800; b++) { orb_process(&s, L, R, 480); float e = orb_current_elevation_deg(&s); if (e < emn) emn = e; if (e > emx) emx = e; }
		orb_free(&s);
		CHECK(emx > 33 && emn < -33, "elevacion 100 %%: sube y baja %.0f..%.0f grados", emn, emx);
	}

	/* sesgo a los oidos: fraccion del tiempo con |ILD| > 12 dB */
	double frac[2];
	for (int k = 0; k < 2; k++) {
		static_params(&p, 0); p.period_s = 4.0f; p.near_ears = k ? 1.0f : 0.0f;
		n = run_motion(&p, 8.0f, az, 1, ild);
		int c = 0; for (int i = 50; i < n; i++) if (fabs(ild[i]) > 12.0) c++;
		frac[k] = (double)c / (n - 50);
	}
	CHECK(frac[1] > frac[0] * 1.25, "'cerca de los oidos' 100 %%: %.0f %% del tiempo claramente en un oido (vs %.0f %% sin sesgo)", frac[1] * 100, frac[0] * 100);
}

static void test_stereo_input(void)
{
	printf("== Entrada estereo / mono, mezcla y nivel\n");
	orb_params p; static_params(&p, 0); p.mono_in = 0;
	orb_state s; orb_init(&s, SR); orb_set_params(&s, &p);
	size_t N = 48000; float *L = malloc(N * 4), *R = malloc(N * 4);
	for (size_t i = 0; i < N; i++) { L[i] = 0.3f * noise(); R[i] = 0; }
	orb_process(&s, L, R, N);
	double el = 0, er = 0; for (size_t i = 9600; i < N; i++) { el += L[i] * L[i]; er += R[i] * R[i]; }
	CHECK(10 * log10(el / er) > 2.0, "'mantener estereo': el canal izquierdo orbita a -30 grados (L %.1f dB sobre R)", 10 * log10(el / er));
	orb_free(&s);

	orb_default_params(&p); p.mix = 0.0f;
	orb_init(&s, SR); orb_set_params(&s, &p);
	float a[480], b[480], a0[480];
	for (int i = 0; i < 480; i++) a0[i] = a[i] = b[i] = 0.3f * noise();
	orb_process(&s, a, b, 480);
	int same = 1; for (int i = 0; i < 480; i++) if (a[i] != a0[i]) same = 0;
	CHECK(same, "mezcla 0 %% = senal original intacta");
	orb_free(&s);

	orb_default_params(&p);
	orb_init(&s, SR); orb_set_params(&s, &p);
	double ei = 0, eo = 0;
	for (size_t i = 0; i < N; i++) { L[i] = R[i] = 0.3f * noise(); ei += 2.0 * L[i] * L[i]; }
	orb_process(&s, L, R, N);
	for (size_t i = 0; i < N; i++) eo += L[i] * L[i] + R[i] * R[i];
	double g = 10 * log10(eo / ei);
	CHECK(g > -6 && g < 3, "nivel con ajustes por defecto: %+.1f dB respecto a la entrada", g);
	orb_free(&s); free(L); free(R);
}

static void random_params(orb_params *p, unsigned *rs)
{
#define RND() ((*rs = *rs * 1103515245u + 12345u), (float)((*rs >> 8) & 0xffff) / 65535.0f)
	p->mode = (int)(RND() * 3.99f);
	p->period_s = 2.0f + RND() * 30.0f;
	p->dir = RND() < 0.5f ? 1 : -1;
	p->start_deg = -180 + 360 * RND();
	p->arc_deg = 10 + 350 * RND();
	p->distance_m = 0.25f + 3.75f * RND();
	p->front_back = RND(); p->elevation = RND(); p->binaural = RND();
	p->room = RND(); p->near_ears = RND(); p->mono_in = RND() < 0.5f;
	p->mix = RND(); p->out_gain = sd_db_to_lin(-12 + 18 * RND());
#undef RND
}

static void test_stability_and_clicks(void)
{
	printf("== Estabilidad (NaN/denormales) y clics al mover controles\n");
	orb_params p; orb_default_params(&p);
	orb_state s; orb_init(&s, SR); orb_set_params(&s, &p);
	unsigned rs = 777;
	size_t N = 48000 * 5;
	float *L = malloc(N * 4), *R = malloc(N * 4);
	for (size_t i = 0; i < N; i++) { L[i] = 0.5f * noise(); R[i] = 0.5f * noise(); }
	for (size_t o = 0; o < N; o += 480) { if ((o / 480) % 2 == 0) { random_params(&p, &rs); orb_set_params(&s, &p); } orb_process(&s, L + o, R + o, 480); }
	int bad = count_bad(L, N) + count_bad(R, N);
	memset(L, 0, N * 4); memset(R, 0, N * 4);
	for (int k = 0; k < 6; k++) for (size_t o = 0; o < N; o += 480) orb_process(&s, L + o, R + o, 480);
	bad += count_bad(L, N) + count_bad(R, N);
	float tail = 0; for (size_t i = N - 4800; i < N; i++) { if (fabsf(L[i]) > tail) tail = fabsf(L[i]); }
	CHECK(bad == 0, "ruido + 250 cambios aleatorios de TODOS los controles (modo, entrada...) + 30 s de silencio: %d muestras NaN/Inf/denormales", bad);
	CHECK(tail < 1e-5f, "el silencio se queda en silencio (pico final %.1e)", tail);
	orb_free(&s);

	/* clics: seno 300 Hz; max |segunda diferencia| con cambios bruscos vs orbita estable */
	double res[2];
	for (int sweep = 0; sweep < 2; sweep++) {
		orb_default_params(&p); p.period_s = 2.0f; p.room = 0.0f;
		orb_init(&s, SR); orb_set_params(&s, &p);
		rs = 4242; double mx = 0; float prev1 = 0, prev2 = 0; size_t t = 0;
		for (int b = 0; b < 1000; b++) {
			if (sweep && b % 5 == 0 && b > 10) { random_params(&p, &rs); p.room = 0.0f; p.out_gain = 1.0f; p.mix = 1.0f; orb_set_params(&s, &p); }
			float a[480], c[480];
			for (int i = 0; i < 480; i++, t++) a[i] = c[i] = 0.5f * (float)sin(2.0 * 3.14159265358979 * 300.0 * (double)(t % 160) / 48000.0);
			orb_process(&s, a, c, 480);
			for (int i = 0; i < 480; i++) {
				if (b > 10) { double d2 = fabs(a[i] - 2 * prev1 + prev2); if (d2 > mx) mx = d2; }
				prev2 = prev1; prev1 = a[i];
			}
		}
		res[sweep] = mx;
		orb_free(&s);
	}
	CHECK(res[1] < 1.5 * res[0] && res[0] < 0.0012, "sin clics: 2a diferencia maxima %.5f con 200 saltos bruscos de controles vs %.5f orbitando estable (seno puro: 0.00084)", res[1], res[0]);
	free(L); free(R);
}

static void test_cpu(void)
{
	printf("== Rendimiento\n");
	orb_params p; orb_default_params(&p); p.mono_in = 0; p.room = 0.5f;
	orb_state s; orb_init(&s, SR); orb_set_params(&s, &p);
	float L[480], R[480];
	for (int i = 0; i < 480; i++) L[i] = R[i] = 0.1f * noise();
	clock_t c0 = clock();
	for (int b = 0; b < 100 * 60; b++) orb_process(&s, L, R, 480);
	double sec = (double)(clock() - c0) / CLOCKS_PER_SEC;
	CHECK(sec < 3.0, "60 s de audio estereo 48 kHz en %.3f s de CPU (%.2f %% de un nucleo)", sec, sec / 60.0 * 100.0);
	orb_free(&s);
}

int main(void)
{
	printf("Streamerdesk ASMR - 8D / Orbita: pruebas offline del DSP\n");
	test_binaural();
	test_motion();
	test_stereo_input();
	test_stability_and_clicks();
	test_cpu();
	printf("\nRESULTADO: %s\n", failures ? "HAY FALLOS" : "TODAS LAS PRUEBAS OK");
	return failures ? 1 : 0;
}
