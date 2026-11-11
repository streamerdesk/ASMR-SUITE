/*
 * Streamerdesk ASMR Suite - Susurro de oido a oido: nucleo DSP (sin OBS).
 * Copyright (c) 2026 Streamerdesk. GPL-2.0-or-later.
 *
 * Mueve una voz (normalmente mono: Blue Yeti, Rode...) de una oreja a otra.
 *  - Ley equal-power normalizada (gL^2 + gR^2 = 2, centro = nivel original).
 *  - Modo binaural: ILD (diferencia de nivel), ITD (retardo interaural hasta
 *    0.7 ms en la oreja lejana) y sombra de cabeza (paso bajo en la oreja lejana).
 *  - Todo suavizado por muestra: sin clics. Sin reservas de memoria en el audio.
 */
#include "ear_dsp.h"
#include <string.h>

#define HALF_PI 1.57079632679489661923f

void ete_default_params(ete_params *p)
{
	p->mode = ETE_MODE_AUTO;
	p->position = 0.0f;
	p->sweep_s = 4.0f;
	p->depth = 0.7f;
	p->shape = ETE_SHAPE_HOLD;
	p->dwell = 0.3f;
	p->pan_mode = ETE_PAN_BINAURAL;
	p->binaural = 0.6f;
	p->proximity = 0.3f;
	p->center_keep = 0.0f;
	p->center_dip_db = 0.0f;
	p->out_gain = 1.0f;
	p->start_right = 0;
}

static float clampf(float v, float lo, float hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

static void sanitize(ete_params *p)
{
	if (p->mode < 0 || p->mode > 3)
		p->mode = ETE_MODE_AUTO;
	p->position = clampf(p->position, -1.0f, 1.0f);
	p->sweep_s = clampf(p->sweep_s, 0.1f, 120.0f);
	p->depth = clampf(p->depth, 0.0f, 1.0f);
	p->dwell = clampf(p->dwell, 0.0f, 0.9f);
	p->binaural = clampf(p->binaural, 0.0f, 1.0f);
	p->proximity = clampf(p->proximity, 0.0f, 1.0f);
	p->center_keep = clampf(p->center_keep, 0.0f, 1.0f);
	p->center_dip_db = clampf(p->center_dip_db, 0.0f, 6.0f);
	p->out_gain = clampf(p->out_gain, 0.0f, 4.0f);
}

/* curva de movimiento: fase 0..1 (ida y vuelta) -> posicion -1..+1, empieza en -1 */
float ete_curve(double phase, int shape, float dwell)
{
	phase -= (double)(long long)phase;
	if (shape == ETE_SHAPE_SINE)
		return -cosf((float)(2.0 * SD_PI * phase));
	/* triangulo -1..+1 escalado y recortado; sin() hace que la llegada y la
	 * salida de cada oreja sean suaves (derivada 0 en el recorte, C1) */
	float tri = (float)(phase < 0.5 ? -1.0 + 4.0 * phase : 3.0 - 4.0 * phase);
	float a = 1.0f - clampf(dwell, 0.0f, 0.9f);
	float v = clampf(tri / a, -1.0f, 1.0f);
	return sinf(HALF_PI * v);
}

/* ganancias por oreja. pos -1..+1. ep_mix 0 = equal-power, 1 = binaural (ILD). */
void ete_gains(int unused, float pos, float ep_mix, float bin, float *gl, float *gr)
{
	(void)unused;
	pos = clampf(pos, -1.0f, 1.0f);
	/* equal-power normalizada: centro = 1.0 en cada oreja */
	float th = (pos + 1.0f) * (HALF_PI * 0.5f);
	float el = 1.41421356f * cosf(th);
	float er = 1.41421356f * sinf(th);
	/* binaural: atenuacion ILD de la oreja lejana, potencia total constante */
	float ild_db = 4.0f + 16.0f * bin; /* 4..20 dB en el extremo */
	float fl = pos > 0.0f ? pos : 0.0f; /* lejania de la oreja izquierda */
	float fr = pos < 0.0f ? -pos : 0.0f;
	float rl = sd_db_to_lin(-ild_db * fl);
	float rr = sd_db_to_lin(-ild_db * fr);
	float n = sqrtf(2.0f / (rl * rl + rr * rr));
	float bl = rl * n, br = rr * n;
	*gl = el + (bl - el) * ep_mix;
	*gr = er + (br - er) * ep_mix;
}

/* retardo interaural (muestras) de una oreja: solo la oreja lejana se retrasa */
float ete_itd_samples(float pos, float bin, float sr, int left_ear)
{
	float far = left_ear ? (pos > 0.0f ? pos : 0.0f) : (pos < 0.0f ? -pos : 0.0f);
	return ETE_MAX_ITD_MS * 0.001f * sr * bin * sinf(HALF_PI * clampf(far, 0.0f, 1.0f));
}

static uint32_t xrand(uint32_t *s)
{
	uint32_t x = *s;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	return *s = x ? x : 0x9E3779B9u;
}

static float frand(uint32_t *s) /* 0..1 */
{
	return (float)(xrand(s) >> 8) * (1.0f / 16777216.0f);
}

static void rnd_next(ete_state *s)
{
	s->rnd_from = s->rnd_to;
	/* nuevo destino, alejado del actual para que siempre haya movimiento */
	float t = 0.0f;
	for (int i = 0; i < 4; i++) {
		t = frand(&s->rng) * 2.0f - 1.0f;
		if (fabsf(t - s->rnd_from) > 0.6f)
			break;
	}
	s->rnd_to = t;
	s->rnd_t = 0.0f;
	/* duracion organica: 60%..140% del tiempo de barrido */
	s->rnd_len = s->p.sweep_s * (0.6f + 0.8f * frand(&s->rng));
}

void ete_set_params(ete_state *s, const ete_params *p)
{
	ete_params q = *p;
	sanitize(&q);
	int side_changed = q.start_right != s->p.start_right;
	s->p = q;
	if (side_changed)
		s->phase = 0.0; /* la posicion suavizada evita el salto */
}

void ete_init(ete_state *s, float sample_rate, const ete_params *p)
{
	memset(s, 0, sizeof(*s));
	s->sr = sample_rate > 1000.0f ? sample_rate : 48000.0f;
	s->p = *p;
	sanitize(&s->p);
	s->rng = 0x5D3E2A71u;
	s->pos_coef = sd_smooth_coef(s->sr, 0.015f, 1.0f);
	s->par_coef = sd_smooth_coef(s->sr, 0.030f, 1.0f);
	sd_svf_set(&s->prox_c, s->sr, 220.0f, 1.2f);
	float start = s->p.mode == ETE_MODE_MANUAL ? s->p.position : (s->p.start_right ? 1.0f : -1.0f) * s->p.depth;
	s->pos_s1 = s->pos_s2 = s->last_pos = start;
	s->rnd_from = s->rnd_to = start;
	s->rnd_len = s->p.sweep_s;
	s->depth_s = s->p.depth;
	s->bin_s = s->p.binaural;
	s->prox_s = s->p.proximity;
	s->keep_s = s->p.center_keep;
	s->dip_s = s->p.center_dip_db;
	s->gain_s = s->p.out_gain;
	s->ep_mix_s = s->p.pan_mode == ETE_PAN_BINAURAL ? 1.0f : 0.0f;
}

static float ring_read(const ete_state *s, float d)
{
	float pos = (float)s->w - d;
	if (pos < 0.0f)
		pos += (float)ETE_RING;
	unsigned i0 = (unsigned)pos;
	float fr = pos - (float)i0;
	i0 &= ETE_RING - 1;
	unsigned i1 = (i0 + 1) & (ETE_RING - 1);
	return s->ring[i0] + (s->ring[i1] - s->ring[i0]) * fr;
}

void ete_process(ete_state *s, float *l, float *r, size_t frames)
{
	const ete_params *p = &s->p;
	const float sr = s->sr;
	const float pc = s->pos_coef, kc = s->par_coef;
	const float ep_target = p->pan_mode == ETE_PAN_BINAURAL ? 1.0f : 0.0f;
	const double dphase = 1.0 / (2.0 * (double)p->sweep_s * (double)sr);
	const float side = p->start_right ? -1.0f : 1.0f; /* curva empieza en -1 (izq) */
	const float two_pi_over_sr = 6.28318530718f / sr;
	const float prox_boost_max = sd_db_to_lin(4.5f) - 1.0f; /* hasta +4.5 dB en 220 Hz */

	for (size_t i = 0; i < frames; i++) {
		/* --- suavizado de parametros --- */
		s->depth_s += (p->depth - s->depth_s) * kc;
		s->bin_s += (p->binaural - s->bin_s) * kc;
		s->prox_s += (p->proximity - s->prox_s) * kc;
		s->keep_s += (p->center_keep - s->keep_s) * kc;
		s->dip_s += (p->center_dip_db - s->dip_s) * kc;
		s->gain_s += (p->out_gain - s->gain_s) * kc;
		s->ep_mix_s += (ep_target - s->ep_mix_s) * kc;

		/* --- posicion objetivo --- */
		float target;
		switch (p->mode) {
		case ETE_MODE_MANUAL:
			target = p->position;
			break;
		case ETE_MODE_RANDOM: {
			s->rnd_t += 1.0f / sr;
			if (s->rnd_t >= s->rnd_len)
				rnd_next(s);
			float u = s->rnd_t / s->rnd_len;
			float e = 0.5f - 0.5f * cosf((float)SD_PI * u); /* entrada/salida suaves */
			target = (s->rnd_from + (s->rnd_to - s->rnd_from) * e) * s->depth_s;
			break;
		}
		default: /* AUTO y TEMPO (el plugin convierte BPM a sweep_s) */
			s->phase += dphase;
			if (s->phase >= 1.0)
				s->phase -= 1.0;
			target = side * ete_curve(s->phase, p->shape, p->dwell) * s->depth_s;
			break;
		}
		if (p->mode != ETE_MODE_RANDOM) {
			s->rnd_from = s->rnd_to = target / (s->depth_s > 1e-3f ? s->depth_s : 1.0f);
			s->rnd_t = 0.0f;
			s->rnd_len = p->sweep_s;
		}
		s->pos_s1 += (target - s->pos_s1) * pc;
		s->pos_s2 += (s->pos_s1 - s->pos_s2) * pc;
		const float pos = s->pos_s2;

		/* --- entrada mono (Blue Yeti / Rode suelen ser mono) --- */
		float x = r ? 0.5f * (l[i] + r[i]) : l[i];
		x = sd_flush(x);
		s->ring[s->w] = x;

		/* --- ITD: retrasa la oreja lejana --- */
		float bin_eff = s->bin_s * s->ep_mix_s;
		float xl = ring_read(s, ete_itd_samples(pos, bin_eff, sr, 1));
		float xr = ring_read(s, ete_itd_samples(pos, bin_eff, sr, 0));
		s->w = (s->w + 1) & (ETE_RING - 1);

		/* --- sombra de cabeza: paso bajo en la oreja lejana --- */
		float farl = pos > 0.0f ? pos : 0.0f, farr = pos < 0.0f ? -pos : 0.0f;
		float fcl = 20000.0f * powf(1500.0f / 20000.0f, farl * bin_eff);
		float fcr = 20000.0f * powf(1500.0f / 20000.0f, farr * bin_eff);
		float al = 1.0f - expf(-two_pi_over_sr * fminf(fcl, sr * 0.45f));
		float ar = 1.0f - expf(-two_pi_over_sr * fminf(fcr, sr * 0.45f));
		s->lp_l = sd_flush(s->lp_l + (xl - s->lp_l) * al);
		s->lp_r = sd_flush(s->lp_r + (xr - s->lp_r) * ar);
		/* mezcla: con sombra 0 el filtro queda transparente */
		float sh_l = farl * bin_eff, sh_r = farr * bin_eff;
		xl = xl + (s->lp_l - xl) * clampf(sh_l * 4.0f, 0.0f, 1.0f);
		xr = xr + (s->lp_r - xr) * clampf(sh_r * 4.0f, 0.0f, 1.0f);

		/* --- proximidad: calidez en graves-medios de la oreja cercana --- */
		float bp, lp;
		sd_svf_tick(&s->prox_f, &s->prox_c, x, &lp, &bp);
		bp = sd_flush(bp) * s->prox_c.k; /* ganancia unidad en el pico */
		float nl = farr * farr, nr = farl * farl; /* cercania al extremo, suave */
		xl += bp * prox_boost_max * s->prox_s * nl;
		xr += bp * prox_boost_max * s->prox_s * nr;

		/* --- panorama --- */
		float gl, gr;
		ete_gains(0, pos, s->ep_mix_s, s->bin_s, &gl, &gr);
		float ol = xl * gl, orr = xr * gr;

		/* --- centro: atenuacion al cruzar y parte fija en el centro --- */
		float c = cosf(HALF_PI * pos);
		float dip = sd_db_to_lin(-s->dip_s * c * c);
		ol = (ol * (1.0f - s->keep_s) + x * s->keep_s) * dip * s->gain_s;
		orr = (orr * (1.0f - s->keep_s) + x * s->keep_s) * dip * s->gain_s;

		if (r) {
			l[i] = ol;
			r[i] = orr;
		} else {
			l[i] = 0.5f * (ol + orr);
		}
		s->last_pos = pos;
	}
}
