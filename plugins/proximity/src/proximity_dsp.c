/*
 * Streamerdesk ASMR Suite - Proximidad: motor DSP.
 * Copyright (c) 2026 Streamerdesk. GPL-2.0-or-later.
 */
#include "proximity_dsp.h"
#include "sd_dsp.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define CTRL_BLOCK 16
#define ITD_MAX_S 0.0012f  /* margen sobre el ITD maximo (~0,66 ms) */
#define ER_MAX_S 0.060f
#define HEAD_RADIUS 0.0875f
#define SOUND_SPEED 343.0f

/* tiempos de las primeras reflexiones (ms) de una habitacion pequena */
static const float er_ms[PROX_MAX_TAPS] = {7.1f, 11.3f, 14.9f, 19.7f, 23.3f, 29.1f, 34.7f, 41.3f};

void prox_default_params(prox_params *p)
{
	p->distance = 0.20f;
	p->prox_amount = 1.0f;
	p->prox_freq = 160.0f;
	p->air_db = 1.0f;
	p->hf_loss = 0.6f;
	p->level_loss = 0.5f;
	p->room = 0.2f;
	p->position = 0.0f;
	p->binaural = 1;
	p->warmth = 0.15f;
	p->deharsh = 1;
	p->deharsh_amt = 0.4f;
	p->lowcut_hz = 80.0f;
	p->mono_in = 1;
	p->mix = 1.0f;
	p->out_gain = 1.0f;
}

/* ---------------- mapeos ---------------- */

float prox_shelf_db(float distance, float amount)
{
	float d = sd_clampf(distance, 0.0f, 1.0f);
	float c = 1.0f - d;
	/* cerca: realce de graves (efecto proximidad); lejos: la voz adelgaza un poco */
	return amount * (10.0f * c * c - 3.0f * d);
}

float prox_level_db(float distance, float level_loss)
{
	float d = sd_clampf(distance, 0.0f, 1.0f);
	/* ley de la inversa de la distancia: de r0 (pegado) a 8*r0 (lejos) = -18 dB */
	return -level_loss * 20.0f * log10f(1.0f + 7.0f * d);
}

float prox_lowpass_hz(float distance, float hf_loss)
{
	float d = sd_clampf(distance, 0.0f, 1.0f);
	float e = powf(d, 1.2f) * sd_clampf(hf_loss, 0.0f, 1.0f);
	return 20000.0f * powf(3000.0f / 20000.0f, e);
}

/* ---------------- SVF (Simper) ---------------- */

static void svf_base(prox_svf *f, float g, float k)
{
	f->a1 = 1.0f / (1.0f + g * (g + k));
	f->a2 = g * f->a1;
	f->a3 = g * f->a2;
}

static float svf_g(float sr, float fc)
{
	fc = sd_clampf(fc, 10.0f, sr * 0.45f);
	return (float)tan(SD_PI * fc / sr);
}

static void svf_lp(prox_svf *f, float sr, float fc, float k)
{
	svf_base(f, svf_g(sr, fc), k);
	f->m0 = 0.0f;
	f->m1 = 0.0f;
	f->m2 = 1.0f;
}

static void svf_hp(prox_svf *f, float sr, float fc, float k)
{
	svf_base(f, svf_g(sr, fc), k);
	f->m0 = 1.0f;
	f->m1 = -k;
	f->m2 = -1.0f;
}

static void svf_lowshelf(prox_svf *f, float sr, float fc, float k, float db)
{
	float A = powf(10.0f, db / 40.0f);
	svf_base(f, svf_g(sr, fc) / sqrtf(A), k);
	f->m0 = 1.0f;
	f->m1 = k * (A - 1.0f);
	f->m2 = A * A - 1.0f;
}

static void svf_highshelf(prox_svf *f, float sr, float fc, float k, float db)
{
	float A = powf(10.0f, db / 40.0f);
	svf_base(f, svf_g(sr, fc) * sqrtf(A), k);
	f->m0 = A * A;
	f->m1 = k * (1.0f - A) * A;
	f->m2 = 1.0f - A * A;
}

static void svf_bell(prox_svf *f, float sr, float fc, float q, float db)
{
	float A = powf(10.0f, db / 40.0f);
	float k = 1.0f / (q * A);
	svf_base(f, svf_g(sr, fc), k);
	f->m0 = 1.0f;
	f->m1 = k * (A * A - 1.0f);
	f->m2 = 0.0f;
}

static inline float svf_run(prox_svf *f, float v0)
{
	float v3 = v0 - f->ic2;
	float v1 = f->a1 * f->ic1 + f->a2 * v3;
	float v2 = f->ic2 + f->a2 * f->ic1 + f->a3 * v3;
	f->ic1 = 2.0f * v1 - f->ic1;
	f->ic2 = 2.0f * v2 - f->ic2;
	return f->m0 * v0 + f->m1 * v1 + f->m2 * v2;
}

static void svf_copy_coefs(prox_svf *dst, const prox_svf *src)
{
	dst->a1 = src->a1;
	dst->a2 = src->a2;
	dst->a3 = src->a3;
	dst->m0 = src->m0;
	dst->m1 = src->m1;
	dst->m2 = src->m2;
}

/* ---------------- ciclo de vida ---------------- */

int prox_init(prox_state *s, float sample_rate)
{
	memset(s, 0, sizeof(*s));
	if (sample_rate < 8000.0f)
		sample_rate = 48000.0f;
	s->sr = sample_rate;

	size_t itd_len = sd_next_pow2((size_t)(ITD_MAX_S * sample_rate) + 4);
	size_t er_len = sd_next_pow2((size_t)(ER_MAX_S * sample_rate) + 4);
	s->itd_buf[0] = calloc(itd_len, sizeof(float));
	s->itd_buf[1] = calloc(itd_len, sizeof(float));
	s->er_buf = calloc(er_len, sizeof(float));
	if (!s->itd_buf[0] || !s->itd_buf[1] || !s->er_buf) {
		prox_free(s);
		return -1;
	}
	s->itd_mask = itd_len - 1;
	s->er_mask = er_len - 1;

	/* suavizado: ~35 ms en los parametros, a ritmo de bloque de control */
	s->ctrl_coef = sd_smooth_coef(sample_rate, 0.035f, (float)CTRL_BLOCK);
	s->env_att = sd_smooth_coef(sample_rate, 0.0008f, 1.0f);
	s->env_rel = sd_smooth_coef(sample_rate, 0.080f, 1.0f);
	s->er_lp_coef = 1.0f - expf(-2.0f * (float)SD_PI * 4500.0f / sample_rate);
	s->er_hp_coef = 1.0f - expf(-2.0f * (float)SD_PI * 200.0f / sample_rate);

	/* reflexiones: alternan oidos, decaen y se normalizan */
	s->ntaps = PROX_MAX_TAPS;
	float sum = 0.0f;
	for (int i = 0; i < PROX_MAX_TAPS; i++) {
		float g = powf(0.82f, (float)i);
		s->tap_delay[i] = er_ms[i] * 0.001f * sample_rate;
		s->tap_gain_l[i] = (i & 1) ? g * 0.45f : g;
		s->tap_gain_r[i] = (i & 1) ? g : g * 0.45f;
		sum += g;
	}
	for (int i = 0; i < PROX_MAX_TAPS; i++) {
		s->tap_gain_l[i] /= sum * 0.5f;
		s->tap_gain_r[i] /= sum * 0.5f;
	}

	prox_default_params(&s->target);
	s->initialized = 1;
	prox_snap(s);
	return 0;
}

void prox_free(prox_state *s)
{
	free(s->itd_buf[0]);
	free(s->itd_buf[1]);
	free(s->er_buf);
	s->itd_buf[0] = s->itd_buf[1] = s->er_buf = NULL;
	s->initialized = 0;
}

void prox_reset(prox_state *s)
{
	if (!s->initialized)
		return;
	memset(s->itd_buf[0], 0, (s->itd_mask + 1) * sizeof(float));
	memset(s->itd_buf[1], 0, (s->itd_mask + 1) * sizeof(float));
	memset(s->er_buf, 0, (s->er_mask + 1) * sizeof(float));
	for (int c = 0; c < 2; c++) {
		prox_chan *ch = &s->ch[c];
		ch->lowcut.ic1 = ch->lowcut.ic2 = 0.0f;
		ch->shelf.ic1 = ch->shelf.ic2 = 0.0f;
		ch->air.ic1 = ch->air.ic2 = 0.0f;
		ch->presence.ic1 = ch->presence.ic2 = 0.0f;
		ch->lowpass.ic1 = ch->lowpass.ic2 = 0.0f;
		ch->ds_split.ic1 = ch->ds_split.ic2 = 0.0f;
		ch->dc_x1 = ch->dc_y1 = 0.0f;
		ch->ds_env_hf = ch->ds_env_all = 0.0f;
		ch->shadow_lp = 0.0f;
		ch->ds_gain = 1.0f;
	}
	s->er_lp = s->er_hp = 0.0f;
}

void prox_set_params(prox_state *s, const prox_params *p)
{
	prox_params t = *p;
	t.distance = sd_clampf(t.distance, 0.0f, 1.0f);
	t.prox_amount = sd_clampf(t.prox_amount, 0.0f, 2.0f);
	t.prox_freq = sd_clampf(t.prox_freq, 60.0f, 400.0f);
	t.air_db = sd_clampf(t.air_db, -12.0f, 12.0f);
	t.hf_loss = sd_clampf(t.hf_loss, 0.0f, 1.0f);
	t.level_loss = sd_clampf(t.level_loss, 0.0f, 1.0f);
	t.room = sd_clampf(t.room, 0.0f, 1.0f);
	t.position = sd_clampf(t.position, -1.0f, 1.0f);
	t.warmth = sd_clampf(t.warmth, 0.0f, 1.0f);
	t.deharsh_amt = sd_clampf(t.deharsh_amt, 0.0f, 1.0f);
	t.lowcut_hz = sd_clampf(t.lowcut_hz, 20.0f, 400.0f);
	t.mix = sd_clampf(t.mix, 0.0f, 1.0f);
	t.out_gain = sd_clampf(t.out_gain, 0.0f, 8.0f);
	s->target = t;
}

/* ---------------- control (cada CTRL_BLOCK muestras) ---------------- */

static void compute_controls(prox_state *s, int snap)
{
	const prox_params *t = &s->target;
	float a = snap ? 1.0f : s->ctrl_coef;
#define SM(dst, val) (dst) += a * ((val) - (dst))
	SM(s->s_distance, t->distance);
	SM(s->s_prox_amount, t->prox_amount);
	SM(s->s_prox_freq, t->prox_freq);
	SM(s->s_air_db, t->air_db);
	SM(s->s_hf_loss, t->hf_loss);
	SM(s->s_level_loss, t->level_loss);
	SM(s->s_room, t->room);
	SM(s->s_position, t->position);
	SM(s->s_binaural, t->binaural ? 1.0f : 0.0f);
	SM(s->s_warmth, t->warmth);
	SM(s->s_deharsh, t->deharsh ? t->deharsh_amt : 0.0f);
	SM(s->s_lowcut, t->lowcut_hz);
	SM(s->s_mono, t->mono_in ? 1.0f : 0.0f);
	SM(s->s_mix, t->mix);
	SM(s->s_out_gain, t->out_gain);
#undef SM
	const float sr = s->sr;
	const float d = s->s_distance;
	const float c = 1.0f - d;

	/* filtros (los SVF de Simper admiten cambios de coeficientes sin clics) */
	prox_svf f;
	svf_hp(&f, sr, s->s_lowcut, 1.4142f);
	svf_copy_coefs(&s->ch[0].lowcut, &f);
	svf_copy_coefs(&s->ch[1].lowcut, &f);
	svf_lowshelf(&f, sr, s->s_prox_freq, 1.2f, prox_shelf_db(d, s->s_prox_amount));
	svf_copy_coefs(&s->ch[0].shelf, &f);
	svf_copy_coefs(&s->ch[1].shelf, &f);
	svf_highshelf(&f, sr, 7000.0f, 1.0f, s->s_air_db);
	svf_copy_coefs(&s->ch[0].air, &f);
	svf_copy_coefs(&s->ch[1].air, &f);
	/* la presencia (2-4 kHz) se pierde al alejarse */
	svf_bell(&f, sr, 3200.0f, 0.8f, -5.0f * d * s->s_hf_loss);
	svf_copy_coefs(&s->ch[0].presence, &f);
	svf_copy_coefs(&s->ch[1].presence, &f);
	svf_lp(&f, sr, prox_lowpass_hz(d, s->s_hf_loss), 1.4142f);
	svf_copy_coefs(&s->ch[0].lowpass, &f);
	svf_copy_coefs(&s->ch[1].lowpass, &f);
	svf_lp(&f, sr, 5000.0f, 1.4142f);
	svf_copy_coefs(&s->ch[0].ds_split, &f);
	svf_copy_coefs(&s->ch[1].ds_split, &f);

	/* suavizado de eses: actua mas cuanto mas cerca (las eses molestan mas pegadas al oido) */
	float ds = s->s_deharsh;
	s->ds_thresh = 0.55f - 0.15f * ds;
	s->ds_depth = ds * (0.6f + 0.4f * c) * 2.0f;

	/* objetivos derivados con rampa lineal por muestra */
	float n = snap ? 0.0f : (float)CTRL_BLOCK;
	float tgt;
#define RAMP(cur, step, val)                         \
	do {                                         \
		tgt = (val);                         \
		if (snap) {                          \
			(cur) = tgt;                 \
			(step) = 0.0f;               \
		} else {                             \
			(step) = (tgt - (cur)) / n; \
		}                                    \
	} while (0)

	RAMP(s->g_level, s->g_level_step, sd_db_to_lin(prox_level_db(d, s->s_level_loss)));
	/* las reflexiones no bajan con la distancia (campo reverberante): crecen respecto a la voz directa */
	RAMP(s->g_er, s->g_er_step, s->s_room * (0.10f + 0.50f * d));

	float pos = s->s_position;
	float ap = fabsf(pos);
	float bin = s->s_binaural;
	/* ITD (Woodworth): (r/c)(theta + sin theta) */
	float theta = ap * (float)SD_PI * 0.5f;
	float itd = HEAD_RADIUS / SOUND_SPEED * (theta + sinf(theta)) * sr * bin;
	/* ILD de campo cercano: con la fuente pegada a una oreja la otra oye mucho menos */
	float g_far = bin * sd_db_to_lin(-ap * (4.0f + 10.0f * c)) + (1.0f - bin) * (1.0f - ap);
	float g_near = bin * sd_db_to_lin(ap * (1.0f + 2.0f * c)) + (1.0f - bin);
	for (int e = 0; e < 2; e++) {
		/* e = 0 izquierda: es el oido lejano si la fuente esta a la derecha (pos > 0) */
		int far = e == 0 ? pos > 0.0f : pos < 0.0f;
		RAMP(s->g_ear[e], s->g_ear_step[e], far ? g_far : g_near);
		RAMP(s->itd_ear[e], s->itd_ear_step[e], far ? itd : 0.0f);
		RAMP(s->shadow_amt[e], s->shadow_amt_step[e], far ? ap * bin : 0.0f);
	}
	s->shadow_coef = 1.0f - expf(-2.0f * (float)SD_PI * (1200.0f + 3000.0f * d) / sr);

	float w = s->s_warmth;
	RAMP(s->drive, s->drive_step, 1.0f + 15.0f * w);
	RAMP(s->wet_warm, s->wet_warm_step, sd_clampf(w * 1.5f, 0.0f, 1.0f));
#undef RAMP
}

void prox_snap(prox_state *s)
{
	compute_controls(s, 1);
	s->ctrl_counter = 0;
	for (int c = 0; c < 2; c++)
		s->ch[c].ds_gain = 1.0f;
}

/* ---------------- audio ---------------- */

static inline float chain(prox_state *s, prox_chan *ch, float x)
{
	x = svf_run(&ch->lowcut, x);
	x = svf_run(&ch->shelf, x);
	x = svf_run(&ch->air, x);
	x = svf_run(&ch->presence, x);
	x = svf_run(&ch->lowpass, x);

	/* suavizado de eses dinamico: baja solo la banda >5 kHz cuando domina.
	 * Division complementaria (hf = x - lp) para que lp + hf reconstruya x exactamente. */
	float hf = x - svf_run(&ch->ds_split, x);
	float ahf = fabsf(hf), aall = fabsf(x);
	ch->ds_env_hf += (ahf > ch->ds_env_hf ? s->env_att : s->env_rel) * (ahf - ch->ds_env_hf);
	ch->ds_env_all += (aall > ch->ds_env_all ? s->env_att : s->env_rel) * (aall - ch->ds_env_all);
	float target_g = 1.0f;
	if (s->ds_depth > 0.0f && ch->ds_env_all > 1e-5f) {
		float r = ch->ds_env_hf / ch->ds_env_all;
		if (r > s->ds_thresh) {
			target_g = 1.0f / (1.0f + s->ds_depth * (r / s->ds_thresh - 1.0f) * 4.0f);
			if (target_g < 0.25f)
				target_g = 0.25f;
		}
	}
	ch->ds_gain += (target_g < ch->ds_gain ? s->env_att : s->env_rel) * (target_g - ch->ds_gain);
	x -= hf * (1.0f - ch->ds_gain);

	/* calidez: saturacion suave con un poco de asimetria (armonicos pares) */
	float b = 0.15f * s->wet_warm;
	float sat = (sd_soft_sat(x * s->drive + b) - sd_soft_sat(b)) / s->drive;
	x += s->wet_warm * (sat - x);

	/* bloqueo de continua */
	float y = x - ch->dc_x1 + 0.9995f * ch->dc_y1;
	ch->dc_x1 = x;
	ch->dc_y1 = sd_flush(y);
	return y;
}

void prox_process(prox_state *s, float *l, float *r, size_t frames)
{
	if (!s->initialized || !l)
		return;
	const size_t itd_mask = s->itd_mask, er_mask = s->er_mask;
	for (size_t i = 0; i < frames; i++) {
		if (s->ctrl_counter == 0) {
			compute_controls(s, 0);
			s->ctrl_counter = CTRL_BLOCK;
		}
		s->ctrl_counter--;

		float in_l = l[i];
		float in_r = r ? r[i] : in_l;
		float m = 0.5f * (in_l + in_r);
		float xl = in_l + s->s_mono * (m - in_l);
		float xr = in_r + s->s_mono * (m - in_r);

		float yl = chain(s, &s->ch[0], xl);
		float yr = chain(s, &s->ch[1], xr);
		float er_in = 0.5f * (yl + yr);

		s->g_level += s->g_level_step;
		yl *= s->g_level;
		yr *= s->g_level;

		/* ---- posicion binaural ---- */
		size_t w = s->itd_w;
		s->itd_buf[0][w] = yl;
		s->itd_buf[1][w] = yr;
		float o[2];
		for (int e = 0; e < 2; e++) {
			prox_chan *ch = &s->ch[e];
			s->itd_ear[e] += s->itd_ear_step[e];
			s->g_ear[e] += s->g_ear_step[e];
			s->shadow_amt[e] += s->shadow_amt_step[e];
			float dly = s->itd_ear[e];
			float v = dly > 1e-4f ? sd_delay_read(s->itd_buf[e], itd_mask, w, dly) : s->itd_buf[e][w];
			ch->shadow_lp = sd_flush(ch->shadow_lp + s->shadow_coef * (v - ch->shadow_lp));
			v += s->shadow_amt[e] * (ch->shadow_lp - v);
			o[e] = v * s->g_ear[e];
		}
		s->itd_w = (w + 1) & itd_mask;
		float ol = o[0], or_ = o[1];

		/* ---- primeras reflexiones ---- */
		s->er_lp += s->er_lp_coef * (er_in - s->er_lp);
		s->er_hp += s->er_hp_coef * (s->er_lp - s->er_hp);
		s->er_lp = sd_flush(s->er_lp);
		s->er_hp = sd_flush(s->er_hp);
		size_t ew = s->er_w;
		s->er_buf[ew] = s->er_lp - s->er_hp;
		s->g_er += s->g_er_step;
		if (s->g_er > 1e-6f) {
			float el = 0.0f, erv = 0.0f;
			for (int t = 0; t < s->ntaps; t++) {
				size_t idx = (ew + er_mask + 1 - (size_t)s->tap_delay[t]) & er_mask;
				float v = s->er_buf[idx];
				el += v * s->tap_gain_l[t];
				erv += v * s->tap_gain_r[t];
			}
			ol += s->g_er * el;
			or_ += s->g_er * erv;
		}
		s->er_w = (ew + 1) & er_mask;

		/* ---- mezcla y salida ---- */
		s->drive += s->drive_step;
		s->wet_warm += s->wet_warm_step;
		float mix = s->s_mix, g = s->s_out_gain;
		ol = (in_l + mix * (ol - in_l)) * g;
		or_ = (in_r + mix * (or_ - in_r)) * g;
		if (r) {
			l[i] = sd_flush(ol);
			r[i] = sd_flush(or_);
		} else {
			l[i] = sd_flush(0.5f * (ol + or_));
		}
	}
}
