/*
 * Streamerdesk ASMR - Reverb - nucleo DSP
 * Copyright (c) 2026 Streamerdesk. GPL-2.0-or-later. https://streamerdesk.com
 */
#include "reverb_dsp.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* longitudes base de la FDN (ms a tamano 1.0): mutuamente "primas" para evitar resonancias */
static const float line_ms[RVB_LINES] = {31.71f, 37.11f, 41.13f, 43.67f, 47.53f, 53.39f, 59.17f, 63.83f};
/* signos de salida L/R (ortogonales -> L y R decorrelados) */
static const float out_l[RVB_LINES] = {1, -1, 1, -1, 1, -1, 1, -1};
static const float out_r[RVB_LINES] = {1, 1, -1, -1, 1, 1, -1, -1};
/* frecuencias LFO por linea (Hz) */
static const float lfo_hz[RVB_LINES] = {0.31f, 0.43f, 0.53f, 0.61f, 0.71f, 0.83f, 0.97f, 1.07f};
/* difusores de entrada (ms) */
static const float ap_ms[2][RVB_DIFF] = {{4.77f, 3.59f, 12.73f, 9.30f}, {4.41f, 3.79f, 12.09f, 9.81f}};
/* reflexiones tempranas: tiempo relativo (x 80 ms x escala), ganancia, lado (-1 L, +1 R) */
static const float er_t[RVB_ER_TAPS] = {0.090f, 0.155f, 0.215f, 0.268f, 0.320f, 0.398f,
					0.458f, 0.530f, 0.612f, 0.707f, 0.830f, 0.967f};
static const float er_g[RVB_ER_TAPS] = {0.84f, 0.80f, 0.72f, 0.68f, 0.62f, 0.55f,
					0.50f, 0.44f, 0.38f, 0.33f, 0.26f, 0.20f};
static const float er_side[RVB_ER_TAPS] = {-1, 1, 1, -1, -1, 1, -1, 1, 1, -1, 1, -1};

#define SIZE_MIN 0.35f
#define SIZE_RANGE 1.15f
#define MOD_MAX_SAMPLES_48K 14.0f

void rvb_default_params(rvb_params *p)
{
	p->size = 0.45f;
	p->decay_s = 1.6f;
	p->predelay_ms = 20.0f;
	p->damping = 0.5f;
	p->lowcut_hz = 180.0f;
	p->highcut_hz = 9000.0f;
	p->er_late = 0.55f;
	p->width = 1.0f;
	p->diffusion = 0.7f;
	p->modulation = 0.35f;
	p->freeze = 0;
	p->duck = 0.3f;
	p->duck_release_ms = 400.0f;
	p->mix = 0.25f;
	p->out_gain = 1.0f;
	p->mono_in = 1;
}

static size_t pow2_for(float samples)
{
	return sd_next_pow2((size_t)samples + 8);
}

int rvb_init(rvb_state *s, float sr)
{
	memset(s, 0, sizeof(*s));
	if (sr < 8000.0f)
		sr = 48000.0f;
	s->sr = sr;
	float scale_max = SIZE_MIN + SIZE_RANGE;
	float mod_max = MOD_MAX_SAMPLES_48K * sr / 48000.0f;
	size_t pre_n = pow2_for(RVB_MAX_PREDELAY_MS * 0.001f * sr + 4);
	size_t er_n = pow2_for(0.080f * scale_max * sr + 4);
	size_t line_n[RVB_LINES], total = 2 * pre_n + er_n;
	for (int i = 0; i < RVB_LINES; i++) {
		line_n[i] = pow2_for(line_ms[i] * 0.001f * scale_max * sr + mod_max + 4);
		total += line_n[i];
	}
	size_t ap_n[2][RVB_DIFF];
	for (int c = 0; c < 2; c++)
		for (int k = 0; k < RVB_DIFF; k++) {
			ap_n[c][k] = (size_t)(ap_ms[c][k] * 0.001f * sr) + 1;
			total += ap_n[c][k];
		}
	s->mem = (float *)calloc(total, sizeof(float));
	if (!s->mem)
		return -1;
	float *m = s->mem;
	s->pre = m; m += pre_n;
	s->pre_r = m; m += pre_n;
	s->pre_mask = pre_n - 1;
	s->er = m; m += er_n;
	s->er_mask = er_n - 1;
	for (int i = 0; i < RVB_LINES; i++) {
		rvb_line *ln = &s->line[i];
		ln->buf = m; m += line_n[i];
		ln->mask = line_n[i] - 1;
		ln->base_len = line_ms[i] * 0.001f * sr;
		float w = 2.0f * (float)SD_PI * lfo_hz[i] / sr;
		ln->lfo_cw = cosf(w);
		ln->lfo_sw = sinf(w);
		float ph = (float)i * 0.785398f; /* fases repartidas */
		ln->lfo_c = cosf(ph);
		ln->lfo_s = sinf(ph);
	}
	for (int c = 0; c < 2; c++)
		for (int k = 0; k < RVB_DIFF; k++) {
			s->ap[c][k].buf = m;
			s->ap[c][k].len = ap_n[c][k];
			m += ap_n[c][k];
		}
	s->rng = 22222u;
	rvb_default_params(&s->target);
	return 0;
}

void rvb_free(rvb_state *s)
{
	free(s->mem);
	s->mem = NULL;
}

void rvb_reset(rvb_state *s)
{
	/* recalcula el tamano total a partir de los punteros (contiguos) */
	if (!s->mem)
		return;
	float *end = s->ap[1][RVB_DIFF - 1].buf + s->ap[1][RVB_DIFF - 1].len;
	memset(s->mem, 0, (size_t)(end - s->mem) * sizeof(float));
	for (int i = 0; i < RVB_LINES; i++)
		s->line[i].lp = 0.0f;
	memset(s->lc_st, 0, sizeof(s->lc_st));
	memset(s->hc_st, 0, sizeof(s->hc_st));
	s->duck_env = 0.0f;
	s->duck_gain = 1.0f;
}

static void snap(rvb_state *s)
{
	const rvb_params *p = &s->target;
	s->size_s = p->size;
	s->decay_s = p->decay_s;
	s->pre_s = p->predelay_ms * 0.001f * s->sr;
	s->damp_s = p->damping;
	s->erl_s = p->er_late;
	s->width_s = p->width;
	s->diff_s = p->diffusion;
	s->mod_s = p->modulation;
	s->freeze_s = p->freeze ? 1.0f : 0.0f;
	s->duck_s = p->duck;
	s->mix_s = p->mix;
	s->gain_s = p->out_gain;
	s->lowcut_s = p->lowcut_hz;
	s->highcut_s = p->highcut_hz;
	s->duck_gain = 1.0f;
	float scale = SIZE_MIN + SIZE_RANGE * sd_clampf(p->size, 0.0f, 1.0f);
	for (int i = 0; i < RVB_LINES; i++)
		s->line[i].len_s = s->line[i].base_len * scale;
	s->counter = 0;
}

void rvb_set_params(rvb_state *s, const rvb_params *p)
{
	s->target = *p;
	if (!s->initialized) {
		snap(s);
		s->initialized = 1;
	}
}

static inline float ap_tick(rvb_ap *a, float x, float g)
{
	float d = a->buf[a->pos];
	float v = x + g * d;
	a->buf[a->pos] = v;
	if (++a->pos >= a->len)
		a->pos = 0;
	return d - g * v;
}

static inline float read_lin(const float *buf, size_t mask, size_t w, float d)
{
	return sd_delay_read(buf, mask, w, d);
}

/* Hadamard 8x8 rapida, normalizada (ortonormal => conserva energia) */
static inline void hadamard8(float *x)
{
	for (int h = 1; h < 8; h <<= 1)
		for (int i = 0; i < 8; i += h << 1)
			for (int j = i; j < i + h; j++) {
				float a = x[j], b = x[j + h];
				x[j] = a + b;
				x[j + h] = a - b;
			}
	for (int i = 0; i < 8; i++)
		x[i] *= 0.35355339f;
}

static void block_update(rvb_state *s, float a_blk)
{
	const rvb_params *p = &s->target;
	s->decay_s += (sd_clampf(p->decay_s, 0.2f, 10.0f) - s->decay_s) * a_blk;
	s->damp_s += (sd_clampf(p->damping, 0.0f, 1.0f) - s->damp_s) * a_blk;
	s->diff_s += (sd_clampf(p->diffusion, 0.0f, 1.0f) - s->diff_s) * a_blk;
	s->mod_s += (sd_clampf(p->modulation, 0.0f, 1.0f) - s->mod_s) * a_blk;
	s->lowcut_s += (sd_clampf(p->lowcut_hz, 20.0f, 1000.0f) - s->lowcut_s) * a_blk;
	s->highcut_s += (sd_clampf(p->highcut_hz, 1000.0f, 20000.0f) - s->highcut_s) * a_blk;
	s->lc_on = s->lowcut_s > 20.5f;
	s->hc_on = s->highcut_s < 19999.0f;
	sd_svf_set(&s->lc, s->sr, s->lowcut_s, 1.41421356f);
	sd_svf_set(&s->hc, s->sr, s->highcut_s, 1.41421356f);
	for (int i = 0; i < RVB_LINES; i++) {
		rvb_line *ln = &s->line[i];
		/* RT60: cada paso por una linea de L muestras debe atenuar 60 dB en decay*sr muestras */
		ln->g = powf(10.0f, -3.0f * ln->len_s / (s->decay_s * s->sr));
		/* renormaliza el oscilador para que no derive */
		float n = 1.5f - 0.5f * (ln->lfo_c * ln->lfo_c + ln->lfo_s * ln->lfo_s);
		ln->lfo_c *= n;
		ln->lfo_s *= n;
	}
}

void rvb_process(rvb_state *s, float *l, float *r, size_t frames)
{
	if (!s->mem)
		return;
	if (!s->initialized) {
		snap(s);
		s->initialized = 1;
	}
	const rvb_params *p = &s->target;
	const float sr = s->sr;
	const float a_fast = sd_smooth_coef(sr, 0.015f, 1.0f);
	const float a_size = sd_smooth_coef(sr, 0.200f, 1.0f);
	const float a_blk = sd_smooth_coef(sr, 0.030f, 32.0f);
	const float a_frz = sd_smooth_coef(sr, 0.050f, 1.0f);
	const float duck_att = sd_smooth_coef(sr, 0.005f, 1.0f);
	const float duck_rel = sd_smooth_coef(sr, sd_clampf(p->duck_release_ms, 20.0f, 2000.0f) * 0.001f, 1.0f);
	const float t_size = sd_clampf(p->size, 0.0f, 1.0f);
	const float t_pre = sd_clampf(p->predelay_ms, 0.0f, RVB_MAX_PREDELAY_MS) * 0.001f * sr;
	const float t_erl = sd_clampf(p->er_late, 0.0f, 1.0f);
	const float t_width = sd_clampf(p->width, 0.0f, 1.5f);
	const float t_frz = p->freeze ? 1.0f : 0.0f;
	const float t_duck = sd_clampf(p->duck, 0.0f, 1.0f);
	const float t_mix = sd_clampf(p->mix, 0.0f, 1.0f);
	const float t_gain = sd_clampf(p->out_gain, 0.0f, 8.0f);
	const float mod_max = MOD_MAX_SAMPLES_48K * sr / 48000.0f;
	const float er_base = 0.080f * sr;

	for (size_t n = 0; n < frames; n++) {
		if (s->counter-- <= 0) {
			block_update(s, a_blk);
			s->counter = 31;
		}
		s->size_s += (t_size - s->size_s) * a_size;
		s->pre_s += (t_pre - s->pre_s) * a_size;
		s->erl_s += (t_erl - s->erl_s) * a_fast;
		s->width_s += (t_width - s->width_s) * a_fast;
		s->freeze_s += (t_frz - s->freeze_s) * a_frz;
		s->duck_s += (t_duck - s->duck_s) * a_fast;
		s->mix_s += (t_mix - s->mix_s) * a_fast;
		s->gain_s += (t_gain - s->gain_s) * a_fast;
		const float scale = SIZE_MIN + SIZE_RANGE * s->size_s;

		float inL = l[n];
		float inR = r ? r[n] : inL;
		float xL = inL, xR = inR;
		if (p->mono_in) {
			xL = xR = 0.5f * (inL + inR);
		}

		/* ---- pre-delay ---- */
		const size_t w = s->wl;
		s->pre[w & s->pre_mask] = xL;
		s->pre_r[w & s->pre_mask] = xR;
		float pd = s->pre_s < 0.0f ? 0.0f : s->pre_s;
		float pL = read_lin(s->pre, s->pre_mask, w & s->pre_mask, pd);
		float pR = read_lin(s->pre_r, s->pre_mask, w & s->pre_mask, pd);

		/* entrada a la sala se apaga suavemente al congelar */
		const float in_g = 1.0f - s->freeze_s;
		pL *= in_g;
		pR *= in_g;

		/* ---- reflexiones tempranas (multitap desde la senal pre-retardada mono) ---- */
		s->er[w & s->er_mask] = 0.5f * (pL + pR);
		float erL = 0.0f, erR = 0.0f;
		const float er_len = er_base * scale;
		for (int k = 0; k < RVB_ER_TAPS; k++) {
			float v = er_g[k] * read_lin(s->er, s->er_mask, w & s->er_mask, er_t[k] * er_len + 1.0f);
			if (er_side[k] < 0.0f) {
				erL += v;
				erR += 0.35f * v;
			} else {
				erR += v;
				erL += 0.35f * v;
			}
		}
		erL *= 0.30f;
		erR *= 0.30f;

		/* ---- difusion de entrada ---- */
		const float gd = 0.75f * s->diff_s;
		float dL = pL, dR = pR;
		for (int k = 0; k < RVB_DIFF; k++) {
			dL = ap_tick(&s->ap[0][k], dL, gd);
			dR = ap_tick(&s->ap[1][k], dR, gd);
		}

		/* ---- FDN 8 lineas ---- */
		float y[RVB_LINES];
		const float mod_d = mod_max * s->mod_s;
		const float damp = 0.85f * s->damp_s * (1.0f - s->freeze_s);
		for (int i = 0; i < RVB_LINES; i++) {
			rvb_line *ln = &s->line[i];
			ln->len_s += (ln->base_len * scale - ln->len_s) * a_size;
			/* LFO por fasor */
			float c = ln->lfo_c * ln->lfo_cw - ln->lfo_s * ln->lfo_sw;
			ln->lfo_s = ln->lfo_s * ln->lfo_cw + ln->lfo_c * ln->lfo_sw;
			ln->lfo_c = c;
			float d = ln->len_s + mod_d * (1.0f + ln->lfo_s);
			float v = sd_delay_read_hermite(ln->buf, ln->mask, w & ln->mask, d);
			/* amortiguacion (paso-bajo de un polo dentro del bucle) */
			ln->lp = v + damp * (ln->lp - v) + 1e-20f;
			float g = ln->g + (1.0f - ln->g) * s->freeze_s;
			y[i] = ln->lp * g;
		}
		float outL = 0.0f, outR = 0.0f;
		for (int i = 0; i < RVB_LINES; i++) {
			outL += out_l[i] * y[i];
			outR += out_r[i] * y[i];
		}
		hadamard8(y);
		for (int i = 0; i < RVB_LINES; i++) {
			float inj = (i & 1) ? dR : dL;
			float v = y[i] + 0.5f * inj;
			/* proteccion: limite suave muy alto (nunca actua en uso normal) */
			if (v > 8.0f || v < -8.0f)
				v = sd_soft_sat(v * 0.125f) * 8.0f;
			s->line[i].buf[w & s->line[i].mask] = v;
		}
		s->wl = w + 1;
		outL *= 0.5f;
		outR *= 0.5f;

		/* ---- balance tempranas / cola ---- */
		const float b = s->erl_s;
		const float erG = b <= 0.5f ? 1.0f : 2.0f * (1.0f - b);
		const float ltG = b >= 0.5f ? 1.0f : 2.0f * b;
		float wL = erG * erL + ltG * outL;
		float wR = erG * erR + ltG * outR;

		/* ---- ancho estereo (M/S) ---- */
		float mid = 0.5f * (wL + wR), side = 0.5f * (wL - wR) * s->width_s;
		wL = mid + side;
		wR = mid - side;

		/* ---- corte de graves / agudos sobre la senal humeda ---- */
		if (s->lc_on) {
			wL = sd_svf_hp(&s->lc_st[0], &s->lc, wL);
			wR = sd_svf_hp(&s->lc_st[1], &s->lc, wR);
		}
		if (s->hc_on) {
			wL = sd_svf_lp(&s->hc_st[0], &s->hc, wL);
			wR = sd_svf_lp(&s->hc_st[1], &s->hc, wR);
		}

		/* ---- ducking: baja la reverb mientras hay voz seca ---- */
		float lvl = fabsf(0.5f * (inL + inR));
		s->duck_env += (lvl - s->duck_env) * (lvl > s->duck_env ? duck_att : duck_rel);
		float x = s->duck_env * 33.0f; /* ~ -30 dBFS = efecto completo */
		if (x > 1.0f)
			x = 1.0f;
		float tg = 1.0f - s->duck_s * x;
		s->duck_gain += (tg - s->duck_gain) * (tg < s->duck_gain ? duck_att : duck_rel);
		wL *= s->duck_gain;
		wR *= s->duck_gain;

		/* ---- mezcla ---- */
		const float mix = s->mix_s;
		const float dryG = mix <= 0.5f ? 1.0f : 2.0f * (1.0f - mix);
		const float wetG = mix >= 0.5f ? 1.0f : 2.0f * mix;
		float oL = (dryG * inL + wetG * wL) * s->gain_s;
		float oR = (dryG * inR + wetG * wR) * s->gain_s;
		if (r) {
			l[n] = oL;
			r[n] = oR;
		} else {
			l[n] = 0.5f * (oL + oR);
		}
	}
	/* evita desbordar el indice (todas las mascaras son potencias de 2 <= 2^31) */
	s->wl &= 0x7fffffffu;
	for (int i = 0; i < RVB_LINES; i++) {
		s->line[i].lp = sd_flush(s->line[i].lp);
	}
	for (int c = 0; c < 2; c++) {
		s->lc_st[c].ic1 = sd_flush(s->lc_st[c].ic1);
		s->lc_st[c].ic2 = sd_flush(s->lc_st[c].ic2);
		s->hc_st[c].ic1 = sd_flush(s->hc_st[c].ic1);
		s->hc_st[c].ic2 = sd_flush(s->hc_st[c].ic2);
	}
}
