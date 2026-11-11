/*
 * Streamerdesk ASMR Suite - primitivas DSP compartidas (header-only, sin OBS).
 * Copyright (c) 2026 Streamerdesk. GPL-2.0-or-later.
 *
 * Usado por todos los efectos de la suite (eco ping-pong, y futuros:
 * panner susurro oreja a oreja, proximidad, reverb, orbita 8D, espacializador binaural).
 */
#pragma once
#include <math.h>
#include <stddef.h>

#ifndef SD_PI
#define SD_PI 3.14159265358979323846
#endif

static inline float sd_clampf(float v, float lo, float hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

static inline float sd_db_to_lin(float db)
{
	return powf(10.0f, db / 20.0f);
}

/* coeficiente de un suavizador de un polo para una constante de tiempo (s) */
static inline float sd_smooth_coef(float sample_rate, float tau_s, float step_samples)
{
	return 1.0f - expf(-step_samples / (tau_s * sample_rate));
}

/* ---- Zavalishin TPT state-variable filter (12 dB/oct) ---- */
typedef struct sd_svf {
	float ic1, ic2;
} sd_svf;

typedef struct sd_svf_coefs {
	float g, k, a1;
} sd_svf_coefs;

/* k = 1/Q (1.414 = Butterworth) */
static inline void sd_svf_set(sd_svf_coefs *c, float sample_rate, float fc, float k)
{
	fc = sd_clampf(fc, 10.0f, sample_rate * 0.45f);
	c->g = (float)tan(SD_PI * fc / sample_rate);
	c->k = k;
	c->a1 = 1.0f / (1.0f + c->g * (c->g + k));
}

static inline void sd_svf_tick(sd_svf *f, const sd_svf_coefs *c, float v0, float *lp, float *bp)
{
	float v3 = v0 - f->ic2;
	float v1 = c->a1 * f->ic1 + c->g * c->a1 * v3;
	float v2 = f->ic2 + c->g * v1;
	f->ic1 = 2.0f * v1 - f->ic1;
	f->ic2 = 2.0f * v2 - f->ic2;
	*lp = v2;
	*bp = v1;
}

static inline float sd_svf_lp(sd_svf *f, const sd_svf_coefs *c, float x)
{
	float lp, bp;
	sd_svf_tick(f, c, x, &lp, &bp);
	return lp;
}

static inline float sd_svf_hp(sd_svf *f, const sd_svf_coefs *c, float x)
{
	float lp, bp;
	sd_svf_tick(f, c, x, &lp, &bp);
	return x - c->k * bp - lp;
}

/* ---- saturacion / limitador suave (aprox. Pade de tanh, satura a +-1) ---- */
static inline float sd_soft_sat(float x)
{
	if (x > 3.0f)
		return 1.0f;
	if (x < -3.0f)
		return -1.0f;
	float x2 = x * x;
	return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

/* ---- linea de retardo circular (tamano potencia de 2), lectura fraccional lineal ---- */
static inline float sd_delay_read(const float *buf, size_t mask, size_t w, float d)
{
	float pos = (float)w - d;
	while (pos < 0.0f)
		pos += (float)(mask + 1);
	size_t i0 = (size_t)pos;
	float frac = pos - (float)i0;
	i0 &= mask;
	size_t i1 = (i0 + 1) & mask;
	return buf[i0] + (buf[i1] - buf[i0]) * frac;
}

/* lectura fraccional con interpolacion Hermite de 4 puntos (casi sin perdida de agudos;
 * ideal para lineas en bucle de realimentacion como reverbs). Requiere d >= 2. */
static inline float sd_delay_read_hermite(const float *buf, size_t mask, size_t w, float d)
{
	float pos = (float)w - d;
	while (pos < 0.0f)
		pos += (float)(mask + 1);
	size_t i1 = (size_t)pos;
	float t = pos - (float)i1;
	float xm1 = buf[(i1 - 1) & mask], x0 = buf[i1 & mask];
	float x1 = buf[(i1 + 1) & mask], x2 = buf[(i1 + 2) & mask];
	float c1 = 0.5f * (x1 - xm1);
	float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
	float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
	return ((c3 * t + c2) * t + c1) * t + x0;
}

static inline size_t sd_next_pow2(size_t n)
{
	size_t s = 1;
	while (s < n)
		s <<= 1;
	return s;
}

/* balance estereo: -1 = solo izquierda, +1 = solo derecha */
static inline void sd_balance_gains(float bal, float *gl, float *gr)
{
	*gl = bal > 0.0f ? 1.0f - bal : 1.0f;
	*gr = bal < 0.0f ? 1.0f + bal : 1.0f;
}

static inline float sd_flush(float x)
{
	return (!(x == x) || fabsf(x) < 1e-20f) ? 0.0f : x;
}
