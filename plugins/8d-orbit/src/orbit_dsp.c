/*
 * Streamerdesk ASMR - 8D / Orbita - nucleo DSP
 * Copyright (c) 2026 Streamerdesk. GPL-2.0-or-later. https://streamerdesk.com
 */
#include "orbit_dsp.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define TWO_PI 6.28318530717958647692
#define HEAD_A 0.0875f   /* radio de la cabeza (m) */
#define SOUND_C 343.0f   /* velocidad del sonido (m/s) */
#define SPREAD 0.5235988f /* +-30 grados entre L y R en modo estereo */
#define ELEV_MAX 0.6108652f /* 35 grados */
#define DEG 0.017453293f

void orb_default_params(orb_params *p)
{
	p->mode = ORB_ORBIT;
	p->period_s = 12.0f;
	p->dir = 1;
	p->start_deg = -90.0f;
	p->arc_deg = 180.0f;
	p->distance_m = 0.6f;
	p->front_back = 0.6f;
	p->elevation = 0.2f;
	p->binaural = 0.85f;
	p->room = 0.2f;
	p->near_ears = 0.3f;
	p->mono_in = 1;
	p->mix = 1.0f;
	p->out_gain = 1.0f;
}

static inline float wrapf(float a)
{
	while (a > (float)SD_PI)
		a -= (float)TWO_PI;
	while (a <= -(float)SD_PI)
		a += (float)TWO_PI;
	return a;
}

static inline double wrapd2pi(double a)
{
	if (a >= TWO_PI || a < 0.0)
		a -= TWO_PI * floor(a / TWO_PI);
	return a;
}

static float frand(orb_state *s)
{
	s->rng = s->rng * 1664525u + 1013904223u;
	return (float)(s->rng >> 8) * (1.0f / 16777216.0f);
}

int orb_init(orb_state *s, float sr)
{
	memset(s, 0, sizeof(*s));
	if (sr < 8000.0f)
		sr = 48000.0f;
	s->sr = sr;
	/* ITD maximo ~0,66 ms + margen Hermite */
	size_t n = sd_next_pow2((size_t)(0.0008f * sr) + 16);
	size_t total = ORB_NSRC * n + 2 * ORB_CHUNK;
	s->mem = (float *)calloc(total, sizeof(float));
	if (!s->mem)
		return -1;
	float *m = s->mem;
	for (int k = 0; k < ORB_NSRC; k++) {
		s->src[k].buf = m;
		s->src[k].mask = n - 1;
		m += n;
	}
	s->room_l = m; m += ORB_CHUNK;
	s->room_r = m;
	s->room_ok = rvb_init(&s->room, sr) == 0;
	sd_svf_set(&s->ls_c, sr, 220.0f, 1.41421356f);
	sd_svf_set(&s->hs_c, sr, 4500.0f, 1.41421356f);
	s->rng = 0x5eed8d01u;
	s->room_dn_set = -1.0f;
	s->room_mono_set = -1;
	orb_default_params(&s->target);
	return 0;
}

void orb_free(orb_state *s)
{
	free(s->mem);
	s->mem = NULL;
	rvb_free(&s->room);
}

void orb_set_params(orb_state *s, const orb_params *p)
{
	s->target = *p;
}

float orb_current_azimuth_deg(const orb_state *s)
{
	return s->az / DEG;
}

float orb_current_elevation_deg(const orb_state *s)
{
	return s->el / DEG;
}

/* retardo de llegada a un oido para un angulo theta entre la fuente y el eje del oido (Brown-Duda) */
static inline float ear_delay_s(float theta)
{
	const float ac = HEAD_A / SOUND_C;
	if (theta < (float)SD_PI * 0.5f)
		return ac * (1.0f - cosf(theta));
	return ac * (1.0f + theta - (float)SD_PI * 0.5f);
}

/* filtro de sombra de la cabeza de Brown-Duda: H(s) = (alpha*s + beta) / (s + beta), beta = 2c/a */
static inline void bd_coefs(float *b, float sr, float alpha)
{
	const float beta = 2.0f * SOUND_C / HEAD_A;
	const float K = 2.0f * sr;
	const float inv = 1.0f / (beta + K);
	b[0] = (beta + alpha * K) * inv;
	b[1] = (beta - alpha * K) * inv;
	b[2] = (beta - K) * inv;
}

static inline float bd_alpha(float theta)
{
	/* alpha_min = 0.1 a theta_min = 150 grados: theta * 180/150 */
	return 1.05f + 0.95f * cosf(theta * 1.2f);
}

static void room_params(orb_state *s, float dn, int mono)
{
	rvb_params rp;
	rvb_default_params(&rp);
	rp.size = 0.22f + 0.35f * dn;
	rp.decay_s = 0.45f + 0.9f * dn;
	rp.predelay_ms = 3.0f + 14.0f * dn;
	rp.damping = 0.55f;
	rp.lowcut_hz = 200.0f;
	rp.highcut_hz = 8000.0f;
	rp.er_late = 0.4f;
	rp.width = 1.0f;
	rp.diffusion = 0.75f;
	rp.modulation = 0.3f;
	rp.duck = 0.0f;
	rp.mix = 1.0f; /* solo la sala: se mezcla aqui */
	rp.out_gain = 1.0f;
	rp.mono_in = mono;
	rvb_set_params(&s->room, &rp);
	s->room_dn_set = dn;
	s->room_mono_set = mono;
}

static void block_update(orb_state *s, int snap)
{
	const orb_params *p = &s->target;
	const float sr = s->sr;
	const float dt = (float)ORB_BLK / sr;
	const float a = snap ? 1.0f : sd_smooth_coef(sr, 0.25f, (float)ORB_BLK);
	const float a_az = snap ? 1.0f : sd_smooth_coef(sr, 0.06f, (float)ORB_BLK);

	/* parametros lentos */
	float st = sd_clampf(p->start_deg, -180.0f, 180.0f) * DEG;
	s->start_s = wrapf(s->start_s + wrapf(st - s->start_s) * a);
	s->arc_s += (sd_clampf(p->arc_deg, 10.0f, 360.0f) * DEG - s->arc_s) * a;
	s->fb_s += (sd_clampf(p->front_back, 0.0f, 1.0f) - s->fb_s) * a;
	s->elev_s += (sd_clampf(p->elevation, 0.0f, 1.0f) - s->elev_s) * a;
	s->bin_s += (sd_clampf(p->binaural, 0.0f, 1.0f) - s->bin_s) * a;
	s->room_s += (sd_clampf(p->room, 0.0f, 1.0f) - s->room_s) * a;
	s->near_s += (sd_clampf(p->near_ears, 0.0f, 1.0f) - s->near_s) * a;
	s->dist_s += (sd_clampf(p->distance_m, 0.25f, 4.0f) - s->dist_s) * a;
	s->mono_s += ((p->mono_in ? 1.0f : 0.0f) - s->mono_s) * a;

	/* ---- movimiento ---- */
	const double period = sd_clampf(p->period_s, 0.5f, 1e6f);
	const double w0 = TWO_PI / period;
	const float dir = p->dir < 0 ? -1.0f : 1.0f;
	float motion;
	switch (p->mode) {
	case ORB_ARC:
		s->arc_ph = wrapd2pi(s->arc_ph + w0 * dt);
		motion = dir * s->arc_s * 0.5f * (1.0f - (float)cos(s->arc_ph));
		break;
	case ORB_WANDER: {
		const float seg = (float)(period / 3.0 < 0.5 ? 0.5 : period / 3.0);
		s->w_count -= ORB_BLK;
		if (s->w_count <= 0) {
			float sign = frand(s) < 0.75f ? dir : -dir;
			s->w_vel_t = sign * (float)w0 * (0.4f + 1.2f * frand(s));
			s->w_count = (long)(seg * sr);
		}
		s->w_vel += (s->w_vel_t - s->w_vel) * (snap ? 1.0f : sd_smooth_coef(sr, seg * 0.5f, (float)ORB_BLK));
		s->wan_acc = wrapd2pi(s->wan_acc + (double)s->w_vel * dt);
		motion = (float)s->wan_acc;
		break;
	}
	default: /* ORBIT / TEMPO */
		s->orb_acc = wrapd2pi(s->orb_acc + dir * w0 * dt);
		motion = (float)s->orb_acc;
		break;
	}
	float tgt = wrapf(s->start_s + motion);
	s->az = wrapf(s->az + wrapf(tgt - s->az) * a_az);
	/* elevacion: oscilacion lenta (1,5 ciclos por vuelta; algo mas irregular en deriva) */
	s->elev_ph = wrapd2pi(s->elev_ph + w0 * (p->mode == ORB_WANDER ? 0.7 : 1.5) * dt);
	s->el = s->elev_s * ELEV_MAX * (float)sin(s->elev_ph);

	/* ---- distancia ---- */
	const float r = s->dist_s;
	const float nf = sd_clampf((1.0f - r) / 0.75f, 0.0f, 1.0f);   /* campo cercano: ILD mayor */
	const float prox = sd_clampf((0.6f - r) / 0.35f, 0.0f, 1.0f); /* efecto proximidad (graves) */
	const float dn = logf(r / 0.25f) / logf(16.0f);              /* 0 (25 cm) .. 1 (4 m) */
	float distG = powf(0.5f / r, 0.75f);
	if (distG > 1.41f)
		distG = 1.41f;
	const float proxG = sd_db_to_lin(4.0f * prox);
	const float roomG = s->room_ok ? s->room_s * (0.35f + 0.65f * dn) * 0.7f : 0.0f;
	const float inv = 1.0f / (float)ORB_BLK;
	if (snap) {
		s->distG = distG; s->proxG = proxG; s->roomG = roomG;
	}
	s->distG_i = (distG - s->distG) * inv;
	s->proxG_i = (proxG - s->proxG) * inv;
	s->roomG_i = (roomG - s->roomG) * inv;
	int mono = p->mono_in ? 1 : 0;
	if (s->room_ok && (fabsf(dn - s->room_dn_set) > 0.01f || mono != s->room_mono_set))
		room_params(s, dn, mono);

	/* ---- fuentes virtuales ---- */
	const float b = 0.9f * s->near_s;
	const float I = s->bin_s;
	const float ce = cosf(s->el), z = sinf(s->el);
	for (int k = 0; k < ORB_NSRC; k++) {
		orb_src *src = &s->src[k];
		float az = s->az + (k == 0 ? -SPREAD : SPREAD) * (1.0f - s->mono_s);
		az = wrapf(az);
		az += b * 0.5f * sinf(2.0f * az); /* pasa mas tiempo junto a los oidos (+-90) */
		float x = ce * sinf(az), y = ce * cosf(az);
		x = sd_clampf(x, -1.0f, 1.0f);
		float thR = acosf(x), thL = acosf(-x);
		float dR = 2.0f + I * ear_delay_s(thR) * sr;
		float dL = 2.0f + I * ear_delay_s(thL) * sr;
		bd_coefs(src->bR, sr, 1.0f + I * (bd_alpha(thR) - 1.0f));
		bd_coefs(src->bL, sr, 1.0f + I * (bd_alpha(thL) - 1.0f));
		float ild = I * x * (2.0f + 4.0f * nf); /* dB, positivo = derecha mas fuerte */
		float gR = sd_db_to_lin(0.5f * ild), gL = sd_db_to_lin(-0.5f * ild);
		float nrm = sqrtf(2.0f / (gL * gL + gR * gR));
		gR *= nrm;
		gL *= nrm;
		float front = y > 0.0f ? y : 0.0f, back = y < 0.0f ? -y : 0.0f;
		float hs_db = s->fb_s * (1.5f * front - 7.0f * back) + 3.0f * z - 1.6f * (r - 0.25f);
		float hsG = sd_db_to_lin(hs_db);
		float nd = sd_clampf(s->fb_s * 0.65f * back + 0.35f * fabsf(z), 0.0f, 0.9f);
		sd_svf_set(&src->nt_c, sr, 7000.0f * (1.0f + 0.4f * z), 0.7f);
		if (snap) {
			src->dL = dL; src->dR = dR; src->gL = gL; src->gR = gR; src->hsG = hsG; src->nd = nd;
		}
		src->dL_i = (dL - src->dL) * inv;
		src->dR_i = (dR - src->dR) * inv;
		src->gL_i = (gL - src->gL) * inv;
		src->gR_i = (gR - src->gR) * inv;
		src->hsG_i = (hsG - src->hsG) * inv;
		src->nd_i = (nd - src->nd) * inv;
	}
}

static void snap_all(orb_state *s)
{
	const orb_params *p = &s->target;
	s->start_s = sd_clampf(p->start_deg, -180.0f, 180.0f) * DEG;
	s->arc_s = sd_clampf(p->arc_deg, 10.0f, 360.0f) * DEG;
	s->fb_s = p->front_back;
	s->elev_s = p->elevation;
	s->bin_s = p->binaural;
	s->room_s = p->room;
	s->near_s = p->near_ears;
	s->dist_s = sd_clampf(p->distance_m, 0.25f, 4.0f);
	s->mono_s = p->mono_in ? 1.0f : 0.0f;
	s->mix_s = sd_clampf(p->mix, 0.0f, 1.0f);
	s->gain_s = sd_clampf(p->out_gain, 0.0f, 8.0f);
	s->az = s->start_s; /* empieza en el angulo inicial */
	block_update(s, 1);
	s->counter = ORB_BLK;
}

static inline float bd_tick(const float *b, float x, float *x1, float *y1)
{
	float y = b[0] * x + b[1] * *x1 - b[2] * *y1;
	*x1 = x;
	*y1 = y;
	return y;
}

void orb_process(orb_state *s, float *l, float *r, size_t frames)
{
	if (!s->mem)
		return;
	if (!s->initialized) {
		snap_all(s);
		s->initialized = 1;
	}
	const orb_params *p = &s->target;
	const float a_fast = sd_smooth_coef(s->sr, 0.015f, 1.0f);
	const float t_mix = sd_clampf(p->mix, 0.0f, 1.0f);
	const float t_gain = sd_clampf(p->out_gain, 0.0f, 8.0f);

	size_t off = 0;
	while (off < frames) {
		size_t n = frames - off;
		if (n > ORB_CHUNK)
			n = ORB_CHUNK;
		float *L = l + off, *R = r ? r + off : NULL;
		/* sala (estatica, envolvente): solo humedo */
		if (s->room_ok) {
			for (size_t i = 0; i < n; i++) {
				s->room_l[i] = L[i];
				s->room_r[i] = R ? R[i] : L[i];
			}
			rvb_process(&s->room, s->room_l, s->room_r, n);
		}
		for (size_t i = 0; i < n; i++) {
			if (s->counter <= 0) {
				block_update(s, 0);
				s->counter = ORB_BLK;
			}
			s->counter--;
			s->mix_s += (t_mix - s->mix_s) * a_fast;
			s->gain_s += (t_gain - s->gain_s) * a_fast;
			s->distG += s->distG_i;
			s->proxG += s->proxG_i;
			s->roomG += s->roomG_i;

			const float inL = L[i];
			const float inR = R ? R[i] : inL;
			const float mono = 0.5f * (inL + inR);
			const float m = s->mono_s;
			float oL = 0.0f, oR = 0.0f;
			for (int k = 0; k < ORB_NSRC; k++) {
				orb_src *src = &s->src[k];
				src->dL += src->dL_i; src->dR += src->dR_i;
				src->gL += src->gL_i; src->gR += src->gR_i;
				src->hsG += src->hsG_i; src->nd += src->nd_i;
				float x = m * mono + (1.0f - m) * (k == 0 ? inL : inR);
				/* efecto proximidad (estanteria de graves) */
				x += (s->proxG - 1.0f) * sd_svf_lp(&src->ls, &s->ls_c, x);
				/* delante/detras, elevacion y aire (estanteria de agudos) */
				x += (src->hsG - 1.0f) * sd_svf_hp(&src->hs, &s->hs_c, x);
				/* muesca tipo pabellon auditivo */
				float lp, bp;
				sd_svf_tick(&src->nt, &src->nt_c, x, &lp, &bp);
				x -= src->nd * src->nt_c.k * bp;
				x *= 0.5f * s->distG;
				src->buf[s->w & src->mask] = x;
				float yl = sd_delay_read_hermite(src->buf, src->mask, s->w & src->mask, src->dL);
				float yr = sd_delay_read_hermite(src->buf, src->mask, s->w & src->mask, src->dR);
				oL += src->gL * bd_tick(src->bL, yl, &src->xL1, &src->yL1);
				oR += src->gR * bd_tick(src->bR, yr, &src->xR1, &src->yR1);
			}
			s->w++;
			if (s->room_ok) {
				oL += s->roomG * s->room_l[i];
				oR += s->roomG * s->room_r[i];
			}
			const float mix = s->mix_s;
			const float dryG = mix <= 0.5f ? 1.0f : 2.0f * (1.0f - mix);
			const float wetG = mix >= 0.5f ? 1.0f : 2.0f * mix;
			float yL = (dryG * inL + wetG * oL) * s->gain_s;
			float yR = (dryG * inR + wetG * oR) * s->gain_s;
			if (R) {
				L[i] = yL;
				R[i] = yR;
			} else {
				L[i] = 0.5f * (yL + yR);
			}
		}
		off += n;
	}
	s->w &= 0x7fffffffu;
	for (int k = 0; k < ORB_NSRC; k++) {
		orb_src *src = &s->src[k];
		sd_svf *f[3] = {&src->ls, &src->hs, &src->nt};
		for (int j = 0; j < 3; j++) {
			f[j]->ic1 = sd_flush(f[j]->ic1);
			f[j]->ic2 = sd_flush(f[j]->ic2);
		}
		src->yL1 = sd_flush(src->yL1);
		src->yR1 = sd_flush(src->yR1);
		src->xL1 = sd_flush(src->xL1);
		src->xR1 = sd_flush(src->xR1);
	}
}
