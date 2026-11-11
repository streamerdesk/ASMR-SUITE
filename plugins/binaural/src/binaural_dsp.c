/*
 * Streamerdesk ASMR Suite - Espacialidad binaural: motor DSP.
 * Copyright (c) 2026 Streamerdesk. GPL-2.0-or-later.
 */
#include "binaural_dsp.h"
#include "sd_dsp.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define CTRL_BLOCK 16
#define HEAD_R 0.0875f
#define SOUND_C 343.0f
#define DEG (3.14159265358979f / 180.0f)

static const float er_ms[BIN_MAX_TAPS] = {6.7f, 10.9f, 14.3f, 18.9f, 23.9f, 28.7f, 35.3f, 42.1f};

void bin_default_params(bin_params *p)
{
	p->azimuth = 0.0f;
	p->elevation = 0.0f;
	p->distance = 0.25f;
	p->glide_s = 1.5f;
	p->cues = 1.0f;
	p->prox = 1.0f;
	p->air_loss = 0.5f;
	p->level_loss = 0.5f;
	p->room = 0.15f;
	p->mono_in = 1;
	p->mix = 1.0f;
	p->out_gain = 1.0f;
}

float bin_itd_seconds(float azimuth, float elevation)
{
	/* angulo lateral respecto al plano medio: sin(lat) = componente hacia la derecha */
	float x = cosf(elevation * DEG) * sinf(azimuth * DEG);
	float lat = asinf(sd_clampf(x, -1.0f, 1.0f));
	return HEAD_R / SOUND_C * (lat + sinf(lat));
}

float bin_level_db(float distance, float level_loss)
{
	float d = sd_clampf(distance, 0.0f, 1.0f);
	return -level_loss * 20.0f * log10f(1.0f + 7.0f * d);
}

/* ---------- SVF de Simper ---------- */
static float svf_g(float sr, float fc)
{
	fc = sd_clampf(fc, 10.0f, sr * 0.45f);
	return (float)tan(SD_PI * fc / sr);
}
static void svf_base(bin_svf *f, float g, float k)
{
	f->a1 = 1.0f / (1.0f + g * (g + k));
	f->a2 = g * f->a1;
	f->a3 = g * f->a2;
}
static void svf_lp(bin_svf *f, float sr, float fc)
{
	svf_base(f, svf_g(sr, fc), 1.4142f);
	f->m0 = 0.0f;
	f->m1 = 0.0f;
	f->m2 = 1.0f;
}
static void svf_lowshelf(bin_svf *f, float sr, float fc, float k, float db)
{
	float A = powf(10.0f, db / 40.0f);
	svf_base(f, svf_g(sr, fc) / sqrtf(A), k);
	f->m0 = 1.0f;
	f->m1 = k * (A - 1.0f);
	f->m2 = A * A - 1.0f;
}
static void svf_highshelf(bin_svf *f, float sr, float fc, float k, float db)
{
	float A = powf(10.0f, db / 40.0f);
	svf_base(f, svf_g(sr, fc) * sqrtf(A), k);
	f->m0 = A * A;
	f->m1 = k * (1.0f - A) * A;
	f->m2 = 1.0f - A * A;
}
static void svf_bell(bin_svf *f, float sr, float fc, float q, float db)
{
	float A = powf(10.0f, db / 40.0f);
	float k = 1.0f / (q * A);
	svf_base(f, svf_g(sr, fc), k);
	f->m0 = 1.0f;
	f->m1 = k * (A * A - 1.0f);
	f->m2 = 0.0f;
}
static inline float svf_run(bin_svf *f, float v0)
{
	float v3 = v0 - f->ic2;
	float v1 = f->a1 * f->ic1 + f->a2 * v3;
	float v2 = f->ic2 + f->a2 * f->ic1 + f->a3 * v3;
	f->ic1 = 2.0f * v1 - f->ic1;
	f->ic2 = 2.0f * v2 - f->ic2;
	return f->m0 * v0 + f->m1 * v1 + f->m2 * v2;
}
static void copy2(bin_state *s, size_t off, const bin_svf *src)
{
	for (int e = 0; e < 2; e++) {
		bin_svf *d = (bin_svf *)((char *)&s->ear[e] + off);
		d->a1 = src->a1;
		d->a2 = src->a2;
		d->a3 = src->a3;
		d->m0 = src->m0;
		d->m1 = src->m1;
		d->m2 = src->m2;
	}
}

/* ---------- ciclo de vida ---------- */
int bin_init(bin_state *s, float sr)
{
	memset(s, 0, sizeof(*s));
	if (sr < 8000.0f)
		sr = 48000.0f;
	s->sr = sr;
	size_t len = sd_next_pow2((size_t)(0.0015f * sr) + 8);
	size_t er_len = sd_next_pow2((size_t)(0.060f * sr) + 4);
	int ok = 1;
	for (int e = 0; e < 2; e++) {
		s->ear[e].buf = calloc(len, sizeof(float));
		s->ear[e].dry = calloc(len, sizeof(float));
		ok &= s->ear[e].buf && s->ear[e].dry;
	}
	s->er_buf = calloc(er_len, sizeof(float));
	if (!ok || !s->er_buf) {
		bin_free(s);
		return -1;
	}
	s->mask = len - 1;
	s->er_mask = er_len - 1;
	s->ctrl_coef = sd_smooth_coef(sr, 0.030f, (float)CTRL_BLOCK);
	s->pos_coef = sd_smooth_coef(sr, 0.020f, (float)CTRL_BLOCK);
	/* polo del modelo de Brown-Duda en w = 2c/a (~1250 Hz), filtro TPT de 1er orden */
	float fp = 2.0f * SOUND_C / HEAD_R / (2.0f * (float)SD_PI);
	float g = (float)tan(SD_PI * fp / sr);
	s->shadow_g = g / (1.0f + g);
	s->er_lp_c = 1.0f - expf(-2.0f * (float)SD_PI * 4500.0f / sr);
	s->er_hp_c = 1.0f - expf(-2.0f * (float)SD_PI * 200.0f / sr);
	float sum = 0.0f;
	for (int i = 0; i < BIN_MAX_TAPS; i++) {
		float gg = powf(0.82f, (float)i);
		s->tap_delay[i] = er_ms[i] * 0.001f * sr;
		s->tap_l[i] = (i & 1) ? gg * 0.45f : gg;
		s->tap_r[i] = (i & 1) ? gg : gg * 0.45f;
		sum += gg;
	}
	for (int i = 0; i < BIN_MAX_TAPS; i++) {
		s->tap_l[i] /= sum * 0.5f;
		s->tap_r[i] /= sum * 0.5f;
	}
	bin_default_params(&s->target);
	s->initialized = 1;
	bin_snap(s);
	return 0;
}

void bin_free(bin_state *s)
{
	for (int e = 0; e < 2; e++) {
		free(s->ear[e].buf);
		free(s->ear[e].dry);
		s->ear[e].buf = s->ear[e].dry = NULL;
	}
	free(s->er_buf);
	s->er_buf = NULL;
	s->initialized = 0;
}

static float wrap360(float a)
{
	a = fmodf(a, 360.0f);
	return a < 0.0f ? a + 360.0f : a;
}

void bin_set_params(bin_state *s, const bin_params *p)
{
	bin_params t = *p;
	t.azimuth = wrap360(t.azimuth);
	t.elevation = sd_clampf(t.elevation, -45.0f, 90.0f);
	t.distance = sd_clampf(t.distance, 0.0f, 1.0f);
	t.glide_s = sd_clampf(t.glide_s, 0.0f, 30.0f);
	t.cues = sd_clampf(t.cues, 0.0f, 2.0f);
	t.prox = sd_clampf(t.prox, 0.0f, 2.0f);
	t.air_loss = sd_clampf(t.air_loss, 0.0f, 1.0f);
	t.level_loss = sd_clampf(t.level_loss, 0.0f, 1.0f);
	t.room = sd_clampf(t.room, 0.0f, 1.0f);
	t.mix = sd_clampf(t.mix, 0.0f, 1.0f);
	t.out_gain = sd_clampf(t.out_gain, 0.0f, 8.0f);
	s->target = t;
	/* nueva posicion: glide lineal (todas las coordenadas llegan a la vez), camino corto en azimut */
	if (t.azimuth != s->last_t_az || t.elevation != s->last_t_el || t.distance != s->last_t_dist) {
		s->last_t_az = t.azimuth;
		s->last_t_el = t.elevation;
		s->last_t_dist = t.distance;
		float daz = fmodf(t.azimuth - wrap360(s->g_az) + 540.0f, 360.0f) - 180.0f;
		long nb = (long)(t.glide_s * s->sr / CTRL_BLOCK);
		if (nb < 1)
			nb = 1;
		s->st_az = daz / (float)nb;
		s->st_el = (t.elevation - s->g_el) / (float)nb;
		s->st_dist = (t.distance - s->g_dist) / (float)nb;
		s->glide_blocks = nb;
	}
}

void bin_get_position(const bin_state *s, float *az, float *el, float *dist)
{
	*az = wrap360(s->az);
	*el = s->el;
	*dist = s->dist;
}

/* ---------- control ---------- */
static void controls(bin_state *s, int snap)
{
	const bin_params *t = &s->target;
	if (snap) {
		s->g_az = s->az1 = s->az = t->azimuth;
		s->g_el = s->el1 = s->el = t->elevation;
		s->g_dist = s->dist1 = s->dist = t->distance;
		s->last_t_az = t->azimuth;
		s->last_t_el = t->elevation;
		s->last_t_dist = t->distance;
		s->glide_blocks = 0;
	} else if (s->glide_blocks > 0) {
		s->g_az += s->st_az;
		s->g_el += s->st_el;
		s->g_dist += s->st_dist;
		if (--s->glide_blocks == 0) {
			/* llegar exacto (azimut en la misma vuelta que el valor deslizado) */
			s->g_az += fmodf(t->azimuth - wrap360(s->g_az) + 540.0f, 360.0f) - 180.0f;
			s->g_el = t->elevation;
			s->g_dist = t->distance;
		}
	}
	/* el azimut deslizado puede dar vueltas; mantenerlo acotado sin saltos para el suavizado */
	if (s->g_az > 720.0f || s->g_az < -720.0f) {
		float k = 360.0f * floorf(s->g_az / 360.0f);
		s->g_az -= k;
		s->az1 -= k;
		s->az -= k;
	}
	float a = snap ? 1.0f : s->ctrl_coef, pa = snap ? 1.0f : s->pos_coef;
	/* dos polos en cascada (critico): la velocidad del movimiento empieza y acaba en cero,
	 * asi el ITD nunca cambia de golpe de ritmo (sin "tirones" de tono ni clics) */
	s->az1 += pa * (s->g_az - s->az1);
	s->el1 += pa * (s->g_el - s->el1);
	s->dist1 += pa * (s->g_dist - s->dist1);
	s->az += pa * (s->az1 - s->az);
	s->el += pa * (s->el1 - s->el);
	s->dist += pa * (s->dist1 - s->dist);
#define SM(d, v) (d) += a * ((v) - (d))
	SM(s->s_cues, t->cues);
	SM(s->s_prox, t->prox);
	SM(s->s_air, t->air_loss);
	SM(s->s_level, t->level_loss);
	SM(s->s_room, t->room);
	SM(s->s_mono, t->mono_in ? 1.0f : 0.0f);
	SM(s->s_mix, t->mix);
	SM(s->s_gain, t->out_gain);
#undef SM

	const float sr = s->sr, d = s->dist, near = 1.0f - d, cue = s->s_cues;
	const float ce = cosf(s->el * DEG), se = sinf(s->el * DEG);
	const float x = ce * sinf(s->az * DEG); /* derecha */
	const float y = ce * cosf(s->az * DEG); /* delante */
	const float front = y > 0.0f ? y : 0.0f, back = y < 0.0f ? -y : 0.0f;
	const float up = se > 0.0f ? se : 0.0f, down = se < 0.0f ? -se : 0.0f;

	/* filtros espectrales (iguales en los dos oidos) */
	bin_svf f;
	svf_lowshelf(&f, sr, 150.0f, 1.2f, s->s_prox * 9.0f * near * near);
	copy2(s, offsetof(bin_ear, prox), &f);
	/* detras: el pabellon tapa los agudos; abajo: sombra del torso */
	svf_highshelf(&f, sr, 4000.0f, 0.9f, cue * (-6.0f * back - 3.0f * down));
	copy2(s, offsetof(bin_ear, hs), &f);
	svf_bell(&f, sr, 3800.0f, 1.0f, cue * 2.5f * front);
	copy2(s, offsetof(bin_ear, front), &f);
	svf_bell(&f, sr, 1100.0f, 1.2f, cue * 2.0f * back);
	copy2(s, offsetof(bin_ear, back), &f);
	svf_bell(&f, sr, 8000.0f, 1.4f, cue * 5.0f * powf(up, 1.5f));
	copy2(s, offsetof(bin_ear, up), &f);
	/* muesca del pabellon: ~6 kHz abajo, ~10 kHz a +45 grados, desaparece encima */
	float el45 = sd_clampf(s->el / 45.0f, -1.0f, 1.0f);
	svf_bell(&f, sr, 8000.0f + 2000.0f * el45, 3.0f, cue * -7.0f * (1.0f - up * up));
	copy2(s, offsetof(bin_ear, notch), &f);
	svf_lp(&f, sr, 20000.0f * powf(3000.0f / 20000.0f, powf(d, 1.2f) * s->s_air));
	copy2(s, offsetof(bin_ear, air), &f);

	/* por oido: ITD, sombra (Brown-Duda) e ILD de campo cercano */
	float itd = bin_itd_seconds(s->az, s->el) * sr; /* >0: fuente a la derecha -> izquierda retrasada */
	float lvl = sd_db_to_lin(bin_level_db(d, s->s_level));
	float nf = near * near * near;
	float n = snap ? 1.0f : (float)CTRL_BLOCK;
	for (int e = 0; e < 2; e++) {
		float side = e == 0 ? -1.0f : 1.0f;
		float facing = side * x; /* 1 = la fuente mira a esta oreja */
		float th = acosf(sd_clampf(facing, -1.0f, 1.0f)) / DEG; /* 0..180 */
		float alpha = 1.05f + 0.95f * cosf(th / 150.0f * 180.0f * DEG);
		float ild_db = nf * (facing > 0.0f ? 3.0f * facing : 12.0f * facing);
		float tgt_itd = BIN_BASE_DELAY + ((side < 0.0f) ? (itd > 0.0f ? itd : 0.0f) : (itd < 0.0f ? -itd : 0.0f));
		float tgt_gain = lvl * sd_db_to_lin(ild_db);
		if (snap) {
			s->itd[e] = tgt_itd;
			s->gain[e] = tgt_gain;
			s->shadow_a[e] = alpha;
			s->itd_step[e] = s->gain_step[e] = s->shadow_a_step[e] = 0.0f;
		} else {
			s->itd_step[e] = (tgt_itd - s->itd[e]) / n;
			s->gain_step[e] = (tgt_gain - s->gain[e]) / n;
			s->shadow_a_step[e] = (alpha - s->shadow_a[e]) / n;
		}
	}
	float ger = s->s_room * (0.08f + 0.5f * d);
	if (snap) {
		s->g_er = ger;
		s->g_er_step = 0.0f;
	} else {
		s->g_er_step = (ger - s->g_er) / n;
	}
}

void bin_snap(bin_state *s)
{
	controls(s, 1);
	s->ctrl_counter = 0;
}

void bin_process(bin_state *s, float *l, float *r, size_t frames)
{
	if (!s->initialized || !l)
		return;
	const size_t mask = s->mask, er_mask = s->er_mask;
	for (size_t i = 0; i < frames; i++) {
		if (s->ctrl_counter == 0) {
			controls(s, 0);
			s->ctrl_counter = CTRL_BLOCK;
		}
		s->ctrl_counter--;
		float in[2];
		in[0] = l[i];
		in[1] = r ? r[i] : in[0];
		float m = 0.5f * (in[0] + in[1]);
		size_t w = s->w;
		float y[2], er_in = 0.0f;
		for (int e = 0; e < 2; e++) {
			bin_ear *ear = &s->ear[e];
			float x = in[e] + s->s_mono * (m - in[e]);
			x = svf_run(&ear->prox, x);
			x = svf_run(&ear->hs, x);
			x = svf_run(&ear->front, x);
			x = svf_run(&ear->back, x);
			x = svf_run(&ear->up, x);
			x = svf_run(&ear->notch, x);
			x = svf_run(&ear->air, x);
			er_in += 0.5f * x;
			ear->buf[w] = x;
			ear->dry[w] = in[e];
			s->itd[e] += s->itd_step[e];
			s->gain[e] += s->gain_step[e];
			s->shadow_a[e] += s->shadow_a_step[e];
			float v = sd_delay_read_hermite(ear->buf, mask, w, s->itd[e]);
			/* sombra de la cabeza: y = LP + alpha * HP (un polo, un cero) */
			float vv = s->shadow_g * (v - ear->shadow_s);
			float lp = vv + ear->shadow_s;
			ear->shadow_s = sd_flush(lp + vv);
			float hp = v - lp;
			y[e] = (lp + s->shadow_a[e] * hp) * s->gain[e];
		}
		s->w = (w + 1) & mask;

		/* sala */
		s->er_lp += s->er_lp_c * (er_in - s->er_lp);
		s->er_hp += s->er_hp_c * (s->er_lp - s->er_hp);
		s->er_lp = sd_flush(s->er_lp);
		s->er_hp = sd_flush(s->er_hp);
		size_t ew = s->er_w;
		s->er_buf[ew] = s->er_lp - s->er_hp;
		s->g_er += s->g_er_step;
		if (s->g_er > 1e-6f) {
			float el = 0.0f, err = 0.0f;
			for (int t = 0; t < BIN_MAX_TAPS; t++) {
				float v = s->er_buf[(ew + er_mask + 1 - (size_t)s->tap_delay[t]) & er_mask];
				el += v * s->tap_l[t];
				err += v * s->tap_r[t];
			}
			y[0] += s->g_er * el;
			y[1] += s->g_er * err;
		}
		s->er_w = (ew + 1) & er_mask;

		/* mezcla con el seco retrasado la misma latencia fija (sin filtro peine) */
		size_t di = (w + mask + 1 - (size_t)BIN_BASE_DELAY) & mask;
		float o[2];
		for (int e = 0; e < 2; e++) {
			float dry = s->ear[e].dry[di];
			o[e] = (dry + s->s_mix * (y[e] - dry)) * s->s_gain;
		}
		if (r) {
			l[i] = sd_flush(o[0]);
			r[i] = sd_flush(o[1]);
		} else {
			l[i] = sd_flush(0.5f * (o[0] + o[1]));
		}
	}
}
