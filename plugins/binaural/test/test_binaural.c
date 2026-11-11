/*
 * Streamerdesk ASMR - Espacialidad binaural: pruebas offline del motor DSP (sin OBS).
 * cc -O2 -Isrc -Icommon/dsp test/test_binaural.c src/binaural_dsp.c -lm
 */
#include "binaural_dsp.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SR 48000.0f
#define PI 3.14159265358979f
static int failures = 0;
/* seno preciso (en float, 2*pi*f*i pierde precision para i grande y mete ruido falso) */
static float sine(double f, size_t i) { return (float)sin(2.0 * 3.14159265358979 * fmod(f * (double)i / SR, 1.0)); }
#define CHECK(c, ...) do { printf("  [%s] ", (c) ? "OK" : "FALLO"); printf(__VA_ARGS__); printf("\n"); if (!(c)) failures++; } while (0)

static void clean(bin_params *p)
{
	bin_default_params(p);
	p->room = 0.0f;
	p->prox = 0.0f;
	p->air_loss = 0.0f;
	p->level_loss = 0.0f;
	p->glide_s = 0.0f;
}
static double db(double x) { return 20.0 * log10(x + 1e-30); }

/* RMS L/R (dB re. entrada) de un seno */
static void sine_db(const bin_params *p, float f, double *dl, double *dr)
{
	bin_state s;
	bin_init(&s, SR);
	bin_set_params(&s, p);
	bin_snap(&s);
	const size_t N = 36000, skip = 12000;
	float *l = malloc(N * 4), *r = malloc(N * 4);
	for (size_t i = 0; i < N; i++)
		l[i] = r[i] = 0.1f * sine(f, i);
	bin_process(&s, l, r, N);
	double al = 0, ar = 0;
	for (size_t i = skip; i < N; i++) { al += l[i] * l[i]; ar += r[i] * r[i]; }
	*dl = db(sqrt(al / (N - skip)) / 0.0707107);
	*dr = db(sqrt(ar / (N - skip)) / 0.0707107);
	free(l); free(r); bin_free(&s);
}

/* picos de la respuesta al impulso (L, R) */
static void impulse_peaks(const bin_params *p, int *pl, int *pr)
{
	bin_state s;
	bin_init(&s, SR);
	bin_set_params(&s, p);
	bin_snap(&s);
	float L[256] = {0}, R[256] = {0};
	L[20] = R[20] = 1.0f;
	bin_process(&s, L, R, 256);
	*pl = *pr = 0;
	for (int i = 0; i < 256; i++) {
		if (fabsf(L[i]) > fabsf(L[*pl])) *pl = i;
		if (fabsf(R[i]) > fabsf(R[*pr])) *pr = i;
	}
	bin_free(&s);
}

static void test_lateral(void)
{
	printf("\n1) Lateral: ITD + ILD + sombra de la cabeza\n");
	bin_params p;
	clean(&p);
	double l, r, l8, r8;
	sine_db(&p, 1000, &l, &r);
	CHECK(fabs(l - r) < 0.01, "delante: izquierda = derecha (%.2f / %.2f dB)", l, r);
	p.azimuth = 90; p.distance = 0.5f;
	sine_db(&p, 500, &l, &r);
	sine_db(&p, 6000, &l8, &r8);
	printf("     derecha (90 grados, media distancia): 500 Hz L %+.1f R %+.1f | 6 kHz L %+.1f R %+.1f dB\n", l, r, l8, r8);
	CHECK(r8 - l8 > 15.0, "agudos: la oreja lejana queda en sombra (ILD %.1f dB)", r8 - l8);
	CHECK(r - l < r8 - l8, "la sombra es mayor en agudos que en graves (como una cabeza real)");
	int pl, pr;
	impulse_peaks(&p, &pl, &pr);
	double itd = (pl - pr) / SR * 1000;
	printf("     ITD medido %.3f ms (teorico %.3f ms)\n", itd, bin_itd_seconds(90, 0) * 1000);
	CHECK(itd > 0.55 && itd < 0.75, "la oreja izquierda recibe la voz ~0,66 ms tarde");
	p.azimuth = 270;
	sine_db(&p, 6000, &l8, &r8);
	CHECK(l8 - r8 > 15.0, "izquierda (270 grados): simetrico (ILD %.1f dB)", l8 - r8);
	/* campo cercano: pegado a la oreja la diferencia crece */
	double lf, rf;
	p.azimuth = 90; p.distance = 0.0f;
	sine_db(&p, 500, &lf, &rf);
	p.distance = 1.0f;
	double ln, rn;
	sine_db(&p, 500, &ln, &rn);
	printf("     500 Hz a 90 grados: pegado ILD %.1f dB, lejos ILD %.1f dB\n", rf - lf, rn - ln);
	CHECK((rf - lf) > (rn - ln) + 8.0, "ILD de campo cercano: pegado a la oreja, el otro oido oye mucho menos");
}

static double band_db(const bin_params *p, float f)
{
	double l, r;
	sine_db(p, f, &l, &r);
	return 0.5 * (l + r);
}

static void test_front_back_elev(void)
{
	printf("\n2) Delante / detras / encima (pistas espectrales)\n");
	bin_params p;
	clean(&p);
	float fr[] = {500, 1100, 3800, 6000, 8000, 10000, 12000};
	double F[7], B[7], U[7];
	for (int i = 0; i < 7; i++) {
		p.azimuth = 0; p.elevation = 0; F[i] = band_db(&p, fr[i]);
		p.azimuth = 180; B[i] = band_db(&p, fr[i]);
		p.azimuth = 0; p.elevation = 90; U[i] = band_db(&p, fr[i]);
	}
	printf("     Hz:      "); for (int i = 0; i < 7; i++) printf("%7.0f", fr[i]); printf("\n");
	printf("     delante: "); for (int i = 0; i < 7; i++) printf("%+7.1f", F[i]); printf("\n");
	printf("     detras:  "); for (int i = 0; i < 7; i++) printf("%+7.1f", B[i]); printf("\n");
	printf("     encima:  "); for (int i = 0; i < 7; i++) printf("%+7.1f", U[i]); printf("\n");
	CHECK(F[2] - B[2] > 4.0, "3,8 kHz: delante mas presente que detras (%+.1f dB)", F[2] - B[2]);
	CHECK(B[1] > F[1] + 1.0, "1,1 kHz: banda de 'detras' realzada");
	CHECK(F[6] - B[6] > 4.0, "agudos altos: detras mas apagado (%+.1f dB)", F[6] - B[6]);
	CHECK(U[4] - F[4] > 6.0, "8 kHz: banda de 'encima' (%+.1f dB)", U[4] - F[4]);
	CHECK(fabs(F[0]) < 1.0 && fabs(B[0]) < 1.0, "graves sin tocar por las pistas de direccion");
	p.elevation = 90;
	double l, r;
	sine_db(&p, 6000, &l, &r);
	int pl, pr;
	impulse_peaks(&p, &pl, &pr);
	CHECK(fabs(l - r) < 0.01 && pl == pr, "encima: mismo nivel y mismo tiempo en los dos oidos");
	/* la muesca sube con la elevacion */
	p.azimuth = 0; p.cues = 1.0f;
	double lo6, hi6, lo10, hi10;
	p.elevation = -45; lo6 = band_db(&p, 4000 + 2000); lo10 = band_db(&p, 10000);
	p.elevation = 45; hi6 = band_db(&p, 6000); hi10 = band_db(&p, 10000);
	printf("     muesca: elev -45 -> 6k %+.1f / 10k %+.1f dB | elev +45 -> 6k %+.1f / 10k %+.1f dB\n", lo6, lo10, hi6, hi10);
	CHECK((lo6 - lo10) < (hi6 - hi10) - 6.0, "muesca del pabellon: ~6 kHz abajo, sube a ~10 kHz arriba");
	p.elevation = 0; p.azimuth = 180; p.cues = 0.0f;
	double c0 = band_db(&p, 3800);
	p.azimuth = 0;
	CHECK(fabs(band_db(&p, 3800) - c0) < 0.1, "intensidad de pistas 0 %%: delante = detras");
}

static void test_distance(void)
{
	printf("\n3) Distancia: volumen, aire y graves de cercania\n");
	bin_params p;
	clean(&p);
	p.level_loss = 1.0f;
	p.distance = 0.0f;
	double v0 = band_db(&p, 1000);
	p.distance = 1.0f;
	double v = band_db(&p, 1000) - v0;
	printf("     1 kHz al otro lado de la habitacion: %+.2f dB\n", v);
	CHECK(fabs(v + 18.06) < 0.6, "ley inversa: -18 dB a distancia maxima");
	clean(&p);
	p.air_loss = 1.0f;
	p.distance = 1.0f;
	CHECK(band_db(&p, 9000) < -12.0, "perdida de agudos por el aire a distancia maxima");
	clean(&p);
	p.prox = 1.0f;
	p.distance = 0.0f;
	double b0 = band_db(&p, 80);
	p.distance = 0.6f;
	double b6 = band_db(&p, 80);
	printf("     80 Hz: pegado %+.1f dB, a 60 %% %+.1f dB\n", b0, b6);
	CHECK(b0 > 7.0 && b6 < 2.0, "graves de campo cercano solo cuando esta muy cerca");
}

static double max_d2(const float *x, size_t n, size_t skip)
{
	double m = 0;
	for (size_t i = skip + 2; i < n; i++) {
		double d = fabs(x[i] - 2.0 * x[i - 1] + x[i - 2]);
		if (d > m) m = d;
	}
	return m;
}

static void test_glide(void)
{
	printf("\n4) Deslizamiento (glide) entre posiciones\n");
	bin_state s;
	bin_params p;
	clean(&p);
	p.glide_s = 1.0f;
	bin_init(&s, SR);
	bin_set_params(&s, &p);
	bin_snap(&s);
	float buf[480], buf2[480];
	p.azimuth = 180;
	bin_set_params(&s, &p);
	float az, el, d;
	int t_half = -1, t_arrive = -1;
	for (int b = 0; b < 300; b++) {
		memset(buf, 0, sizeof buf); memset(buf2, 0, sizeof buf2);
		bin_process(&s, buf, buf2, 480);
		bin_get_position(&s, &az, &el, &d);
		if (t_half < 0 && fminf(az, 360.0f - az) >= 90.0f) t_half = b;
		if (t_arrive < 0 && fabsf(az - 180.0f) < 1.0f) t_arrive = b;
	}
	printf("     0 -> 180 grados con glide 1 s: mitad a %.2f s, llega a %.2f s\n", t_half / 100.0, t_arrive / 100.0);
	CHECK(t_half >= 45 && t_half <= 56, "a mitad de camino hacia los 0,5 s (glide lineal)");
	CHECK(t_arrive >= 98 && t_arrive <= 112, "llega en ~1 s");
	/* camino corto: 350 -> 10 pasa por 0, no por 180 */
	p.glide_s = 0.0f; p.azimuth = 350;
	bin_set_params(&s, &p);
	bin_snap(&s);
	p.glide_s = 0.5f; p.azimuth = 10;
	bin_set_params(&s, &p);
	float maxdev = 0;
	for (int b = 0; b < 80; b++) {
		bin_process(&s, buf, buf2, 480);
		bin_get_position(&s, &az, &el, &d);
		float dev = fminf(az, 360.0f - az);
		if (dev > maxdev) maxdev = dev;
	}
	CHECK(maxdev <= 10.5f && fabsf(az - 10.0f) < 0.5f, "350 -> 10 grados va por delante (desvio max %.1f grados)", maxdev);
	bin_free(&s);

	/* sin clics: vuelta completa + subida/bajada + distancia, con seno 220 Hz, bloques de OBS */
	const size_t N = 48000 * 8;
	float *L = malloc(N * 4), *R = malloc(N * 4);
	for (size_t i = 0; i < N; i++) L[i] = R[i] = 0.2f * sine(220, i);
	bin_init(&s, SR);
	bin_default_params(&p);
	p.glide_s = 0.4f;
	bin_set_params(&s, &p);
	bin_snap(&s);
	const float seq_az[] = {90, 270, 180, 0, 45, 300};
	const float seq_el[] = {0, 60, -30, 90, 0, 20};
	const float seq_d[] = {0, 0.3f, 1, 0.1f, 0.6f, 0};
	for (size_t off = 0, k = 0; off < N; off += 480) {
		if (off % 24000 == 0) {
			p.azimuth = seq_az[k % 6]; p.elevation = seq_el[k % 6]; p.distance = seq_d[k % 6];
			p.glide_s = (k % 3 == 2) ? 0.0f : 0.4f; /* tambien saltos "inmediatos" */
			k++;
			bin_set_params(&s, &p);
		}
		bin_process(&s, L + off, R + off, 480);
	}
	bin_free(&s);
	double sw = fmax(max_d2(L, N, 4800), max_d2(R, N, 4800));
	/* referencia: estado fijo en cada posicion */
	double ref = 0;
	float *a = malloc(48000 * 4), *b2 = malloc(48000 * 4);
	for (int k = 0; k < 6; k++) {
		bin_init(&s, SR);
		p.azimuth = seq_az[k]; p.elevation = seq_el[k]; p.distance = seq_d[k];
		bin_set_params(&s, &p);
		bin_snap(&s);
		for (size_t i = 0; i < 48000; i++) a[i] = b2[i] = 0.2f * sine(220, i);
		bin_process(&s, a, b2, 48000);
		ref = fmax(ref, fmax(max_d2(a, 48000, 4800), max_d2(b2, 48000, 4800)));
		bin_free(&s);
	}
	float save = L[N / 2];
	L[N / 2] += 0.01f * 0.3f;
	double neg = max_d2(L, N, 4800);
	L[N / 2] = save;
	printf("     2a derivada max: movimientos %.5f | estado fijo %.5f | clic artificial del 1 %% %.5f\n", sw, ref, neg);
	CHECK(neg > ref * 4, "el detector ve un clic minimo (control negativo)");
	CHECK(sw < ref * 2.0, "moverse por todas las posiciones (incluido glide 0) sin clics");
	free(L); free(R); free(a); free(b2);
}

static unsigned rng = 777;
static float frand(void) { rng = rng * 1664525u + 1013904223u; return (rng >> 8) / 16777216.0f; }

static void test_random(void)
{
	printf("\n5) Cambios aleatorios de todos los controles + estabilidad\n");
	bin_state s;
	bin_params p;
	bin_init(&s, SR);
	bin_default_params(&p);
	const size_t N = 48000 * 6;
	float *L = malloc(N * 4), *R = malloc(N * 4);
	for (size_t i = 0; i < N; i++) L[i] = R[i] = 0.2f * sine(220, i);
	for (size_t off = 0; off < N; off += 480) {
		p.azimuth = 360 * frand(); p.elevation = -45 + 135 * frand(); p.distance = frand();
		p.glide_s = frand() < 0.3f ? 0.0f : 2 * frand(); p.cues = 2 * frand(); p.prox = 2 * frand();
		p.air_loss = frand(); p.level_loss = frand(); p.room = frand(); p.mono_in = frand() > 0.5f;
		p.mix = frand(); p.out_gain = 1.0f;
		bin_set_params(&s, &p);
		bin_process(&s, L + off, R + off, 480);
	}
	int bad = 0; float mstep = 0, peak = 0;
	for (size_t i = 4800; i < N; i++) {
		if (!isfinite(L[i]) || !isfinite(R[i])) bad++;
		mstep = fmaxf(mstep, fmaxf(fabsf(L[i] - L[i - 1]), fabsf(R[i] - R[i - 1])));
		peak = fmaxf(peak, fmaxf(fabsf(L[i]), fabsf(R[i])));
	}
	printf("     salto max %.4f, pico %.3f, no finitos %d\n", mstep, peak, bad);
	CHECK(bad == 0, "sin NaN/Inf");
	CHECK(mstep < 0.1f, "sin saltos bruscos");
	/* silencio tras ruido */
	for (size_t i = 0; i < 48000; i++) L[i] = R[i] = 2 * (frand() - 0.5f);
	bin_process(&s, L, R, 48000);
	memset(L, 0, 48000 * 4); memset(R, 0, 48000 * 4);
	bin_process(&s, L, R, 48000);
	float tail = 0;
	for (size_t i = 24000; i < 48000; i++) tail = fmaxf(tail, fmaxf(fabsf(L[i]), fabsf(R[i])));
	CHECK(tail < 1e-6f, "el silencio vuelve a ser silencio (%.1e)", tail);
	bin_free(&s);
	/* mezcla 0 = seco con 2 muestras de latencia exacta */
	bin_init(&s, SR);
	bin_default_params(&p); p.mix = 0; p.azimuth = 90;
	bin_set_params(&s, &p); bin_snap(&s);
	float x[512], o[512], o2[512];
	for (int i = 0; i < 512; i++) x[i] = o[i] = o2[i] = 0.3f * sinf(i * 0.07f);
	bin_process(&s, o, o2, 512);
	float md = 0;
	for (int i = 2; i < 512; i++) md = fmaxf(md, fmaxf(fabsf(o[i] - x[i - 2]), fabsf(o2[i] - x[i - 2])));
	CHECK(md < 1e-6f, "intensidad 0 %% = sonido original (latencia fija de 2 muestras = 0,04 ms)");
	bin_free(&s);
	free(L); free(R);
}

int main(void)
{
	printf("Streamerdesk ASMR - Espacialidad binaural: pruebas del motor DSP (%.0f Hz)\n", SR);
	test_lateral();
	test_front_back_elev();
	test_distance();
	test_glide();
	test_random();
	printf("\n%s (%d fallos)\n", failures ? "HAY FALLOS" : "TODO OK", failures);
	return failures ? 1 : 0;
}
