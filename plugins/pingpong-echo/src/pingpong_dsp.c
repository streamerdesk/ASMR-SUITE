/*
 * Streamerdesk ASMR - Eco Ping-Pong - nucleo DSP
 * Copyright (c) 2026 Streamerdesk. GPL-2.0-or-later.
 *
 * Topologia ping-pong:
 *   entrada (mono o estereo) -> linea L ; salida L -> (fb R) -> linea R ;
 *   salida R -> (fb L) -> linea L  => repeticiones L, R, L, R...
 * Filtro de 12 dB/oct resonante (DAMP + RESONANCE) y corte de graves (LOW CUT)
 * en la realimentacion (FILTER FB) o como efecto post (FX), igual que TAL-Dub 1.
 */
#include "pingpong_dsp.h"
#include "sd_dsp.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>


void ppe_default_params(ppe_params *p)
{
	p->delay_l_ms = 375.0f;
	p->delay_r_ms = 375.0f;
	p->fb_l = 0.45f;
	p->fb_r = 0.45f;
	p->damp_hz = 4500.0f;
	p->resonance = 0.15f;
	p->lowcut_hz = 100.0f;
	p->mix = 0.30f;
	p->pingpong = 1.0f;
	p->balance = 0.0f;
	p->out_gain = 1.0f;
	p->filter_fb = 1;
	p->filter_in = 0;
	p->swap_lr = 0;
	p->mono_in = 1;
	p->start_right = 0;
	p->saturate = 1;
}

int ppe_init(ppe_state *s, float sample_rate)
{
	memset(s, 0, sizeof(*s));
	if (sample_rate < 8000.0f)
		sample_rate = 48000.0f;
	s->sample_rate = sample_rate;
	size_t need = (size_t)(sample_rate * (PPE_MAX_DELAY_MS / 1000.0f)) + 8;
	size_t size = sd_next_pow2(need);
	s->size = size;
	s->mask = size - 1;
	s->buf[0] = (float *)calloc(size, sizeof(float));
	s->buf[1] = (float *)calloc(size, sizeof(float));
	if (!s->buf[0] || !s->buf[1]) {
		ppe_free(s);
		return -1;
	}
	ppe_default_params(&s->target);
	return 0;
}

void ppe_free(ppe_state *s)
{
	free(s->buf[0]);
	free(s->buf[1]);
	s->buf[0] = s->buf[1] = NULL;
}

void ppe_reset(ppe_state *s)
{
	if (s->buf[0])
		memset(s->buf[0], 0, s->size * sizeof(float));
	if (s->buf[1])
		memset(s->buf[1], 0, s->size * sizeof(float));
	memset(s->lp_wet, 0, sizeof(s->lp_wet));
	memset(s->hp_wet, 0, sizeof(s->hp_wet));
	memset(s->lp_dry, 0, sizeof(s->lp_dry));
	memset(s->hp_dry, 0, sizeof(s->hp_dry));
}

static inline float ms_to_samples(const ppe_state *s, float ms)
{
	float d = sd_clampf(ms, 1.0f, PPE_MAX_DELAY_MS) * 0.001f * s->sample_rate;
	float maxd = (float)(s->size - 4);
	return sd_clampf(d, 1.0f, maxd);
}

static void snap_smoothers(ppe_state *s)
{
	const ppe_params *p = &s->target;
	s->d_smooth[0] = ms_to_samples(s, p->delay_l_ms);
	s->d_smooth[1] = ms_to_samples(s, p->delay_r_ms);
	s->fb_s[0] = p->fb_l;
	s->fb_s[1] = p->fb_r;
	s->mix_s = p->mix;
	s->pp_s = p->pingpong;
	s->bal_s = p->balance;
	s->gain_s = p->out_gain;
	s->damp_s = p->damp_hz;
	s->res_s = p->resonance;
	s->lowcut_s = p->lowcut_hz;
	s->coef_counter = 0;
}

void ppe_set_params(ppe_state *s, const ppe_params *p)
{
	s->target = *p;
	if (!s->initialized) {
		snap_smoothers(s);
		s->initialized = 1;
	}
}

static void update_coefs(ppe_state *s)
{
	s->lp_on = s->damp_s < 19999.0f;
	s->hp_on = s->lowcut_s > 10.5f;
	/* resonancia 0..1 -> k 1.414 (Butterworth) .. 0.15 (Q~6.7, muy resonante) */
	float k = 1.41421356f - s->res_s * (1.41421356f - 0.15f);
	sd_svf_set(&s->lp_c, s->sample_rate, s->damp_s, k);
	sd_svf_set(&s->hp_c, s->sample_rate, s->lowcut_s, 1.41421356f);
}

static inline float apply_filters(ppe_state *s, sd_svf *lp, sd_svf *hp, float x)
{
	if (s->hp_on)
		x = sd_svf_hp(hp, &s->hp_c, x);
	if (s->lp_on) {
		x = sd_svf_lp(lp, &s->lp_c, x);
		/* compensacion de ganancia de la resonancia, al estilo "vintage" */
		x *= 1.0f / (1.0f + 2.0f * s->res_s);
	}
	return x;
}

void ppe_process(ppe_state *s, float *l, float *r, size_t frames)
{
	if (!s->buf[0] || !s->buf[1])
		return;
	if (!s->initialized) {
		snap_smoothers(s);
		s->initialized = 1;
	}
	const ppe_params *p = &s->target;
	const float sr = s->sample_rate;
	/* constantes de suavizado */
	const float a_fast = sd_smooth_coef(sr, 0.010f, 1.0f);  /* ~10 ms */
	const float a_delay = sd_smooth_coef(sr, 0.060f, 1.0f); /* ~60 ms glide de cinta */
	const float a_filt = sd_smooth_coef(sr, 0.020f, 32.0f);

	const float tgt_d0 = ms_to_samples(s, p->delay_l_ms);
	const float tgt_d1 = ms_to_samples(s, p->delay_r_ms);
	const float tgt_fb0 = sd_clampf(p->fb_l, 0.0f, 1.2f);
	const float tgt_fb1 = sd_clampf(p->fb_r, 0.0f, 1.2f);
	const float tgt_mix = sd_clampf(p->mix, 0.0f, 1.0f);
	const float tgt_pp = sd_clampf(p->pingpong, 0.0f, 1.0f);
	const float tgt_bal = sd_clampf(p->balance, -1.0f, 1.0f);
	const float tgt_gain = sd_clampf(p->out_gain, 0.0f, 8.0f);
	const float tgt_damp = sd_clampf(p->damp_hz, 200.0f, 20000.0f);
	const float tgt_res = sd_clampf(p->resonance, 0.0f, 1.0f);
	const float tgt_lc = sd_clampf(p->lowcut_hz, 10.0f, 2000.0f);
	const int start_r = p->start_right;
	float *bL = s->buf[0], *bR = s->buf[1];
	const size_t mask = s->mask;

	for (size_t n = 0; n < frames; n++) {
		if (s->coef_counter-- <= 0) {
			s->damp_s += (tgt_damp - s->damp_s) * a_filt;
			s->res_s += (tgt_res - s->res_s) * a_filt;
			s->lowcut_s += (tgt_lc - s->lowcut_s) * a_filt;
			update_coefs(s);
			s->coef_counter = 31;
		}
		s->d_smooth[0] += (tgt_d0 - s->d_smooth[0]) * a_delay;
		s->d_smooth[1] += (tgt_d1 - s->d_smooth[1]) * a_delay;
		s->fb_s[0] += (tgt_fb0 - s->fb_s[0]) * a_fast;
		s->fb_s[1] += (tgt_fb1 - s->fb_s[1]) * a_fast;
		s->mix_s += (tgt_mix - s->mix_s) * a_fast;
		s->pp_s += (tgt_pp - s->pp_s) * a_fast;
		s->bal_s += (tgt_bal - s->bal_s) * a_fast;
		s->gain_s += (tgt_gain - s->gain_s) * a_fast;

		float inL = l[n];
		float inR = r ? r[n] : inL;
		if (p->swap_lr && !p->mono_in) {
			float t = inL;
			inL = inR;
			inR = t;
		}
		float xL = inL, xR = inR;
		if (p->mono_in) {
			float m = 0.5f * (inL + inR);
			xL = xR = m;
		}

		/* lectura de las lineas de retardo */
		float yL = sd_delay_read(bL, mask, s->w, s->d_smooth[0]);
		float yR = sd_delay_read(bR, mask, s->w, s->d_smooth[1]);

		/* filtro en la realimentacion o post */
		float fyL = apply_filters(s, &s->lp_wet[0], &s->hp_wet[0], yL);
		float fyR = apply_filters(s, &s->lp_wet[1], &s->hp_wet[1], yR);
		float fbsrcL = p->filter_fb ? fyL : yL;
		float fbsrcR = p->filter_fb ? fyR : yR;

		const float pp = s->pp_s;
		/* inyeccion: con pp=1 toda la entrada (suma) entra por el lado inicial */
		float sum = 0.5f * (xL + xR);
		float injL, injR;
		if (!start_r) {
			injL = (1.0f - pp) * xL + pp * sum;
			injR = (1.0f - pp) * xR;
		} else {
			injL = (1.0f - pp) * xL;
			injR = (1.0f - pp) * xR + pp * sum;
		}
		/* realimentacion cruzada: L -> R, R -> L */
		float fbL = s->fb_s[0] * ((1.0f - pp) * fbsrcL + pp * fbsrcR);
		float fbR = s->fb_s[1] * ((1.0f - pp) * fbsrcR + pp * fbsrcL);

		float wL = injL + fbL;
		float wR = injR + fbR;
		if (p->saturate) {
			wL = sd_soft_sat(wL);
			wR = sd_soft_sat(wR);
		}
		/* limite de seguridad (aunque la saturacion este apagada) */
		wL = sd_clampf(wL, -4.0f, 4.0f);
		wR = sd_clampf(wR, -4.0f, 4.0f);
		/* proteccion contra denormales / NaN */
		wL = sd_flush(wL);
		wR = sd_flush(wR);
		bL[s->w] = wL;
		bR[s->w] = wR;
		s->w = (s->w + 1) & mask;

		/* senal humeda (siempre filtrada: en modo FB ya viene filtrada de la linea) */
		float wetL = fyL, wetR = fyR;
		if (p->saturate) { /* limitador de salida del eco */
			wetL = sd_soft_sat(wetL);
			wetR = sd_soft_sat(wetR);
		}

		/* senal seca */
		float dL = inL, dR = inR;
		if (p->filter_in) {
			dL = apply_filters(s, &s->lp_dry[0], &s->hp_dry[0], dL);
			dR = apply_filters(s, &s->lp_dry[1], &s->hp_dry[1], dR);
		}

		/* DRY/WET: 0..0.5 seco al 100% sube el humedo; 0.5..1 baja el seco */
		float mix = s->mix_s;
		float dryG = mix <= 0.5f ? 1.0f : 2.0f * (1.0f - mix);
		float wetG = mix >= 0.5f ? 1.0f : 2.0f * mix;

		float oL = dryG * dL + wetG * wetL;
		float oR = dryG * dR + wetG * wetR;

		float gL, gR;
		sd_balance_gains(s->bal_s, &gL, &gR);
		oL *= gL * s->gain_s;
		oR *= gR * s->gain_s;

		if (r) {
			l[n] = oL;
			r[n] = oR;
		} else {
			l[n] = 0.5f * (oL + oR);
		}
	}
}
