/*
 * Streamerdesk ASMR - Reverb - nucleo DSP (independiente de OBS)
 * Copyright (c) 2026 Streamerdesk. GPL-2.0-or-later. https://streamerdesk.com
 *
 * Reverb algoritmica estereo:
 *   entrada (suma mono o estereo) -> pre-delay -> difusion de entrada (4 allpass/canal)
 *   -> reflexiones tempranas (multitap L/R) + cola tardia (FDN de 8 lineas, matriz Hadamard,
 *      amortiguacion por linea, modulacion suave, congelar) -> ancho estereo -> corte graves/agudos
 *   -> ducking por la voz seca -> mezcla seco/humedo -> ganancia.
 * Sin reservas de memoria en process(); todos los parametros se suavizan (sin clics).
 */
#pragma once
#include <stddef.h>
#include "sd_dsp.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RVB_LINES 8
#define RVB_DIFF 4
#define RVB_ER_TAPS 12
#define RVB_MAX_PREDELAY_MS 200.0f

typedef struct rvb_params {
	float size;       /* 0..1 tamano de la sala */
	float decay_s;    /* 0.2..10 s (RT60) */
	float predelay_ms;/* 0..200 */
	float damping;    /* 0..1 absorcion de agudos en la cola */
	float lowcut_hz;  /* 20..1000 sobre la senal humeda (<=20 = apagado) */
	float highcut_hz; /* 1000..20000 sobre la senal humeda (>=20000 = apagado) */
	float er_late;    /* 0..1: 0 = solo reflexiones tempranas, 1 = solo cola, 0.5 = ambas al maximo */
	float width;      /* 0..1.5 ancho estereo de la reverb (1 = natural) */
	float diffusion;  /* 0..1 */
	float modulation; /* 0..1 */
	int freeze;       /* 1 = congelar la cola */
	float duck;       /* 0..1 cantidad de ducking */
	float duck_release_ms; /* 20..2000 */
	float mix;        /* 0..1 (0.5 = seco y humedo al 100%) */
	float out_gain;   /* lineal */
	int mono_in;      /* 1 = suma mono antes de la reverb */
} rvb_params;

typedef struct rvb_line {
	float *buf;
	size_t mask;
	float base_len;   /* longitud base (muestras, tamano 1.0) */
	float len_s;      /* longitud suavizada actual (muestras) */
	float g;          /* ganancia de realimentacion */
	float lp;         /* estado de amortiguacion */
	float lfo_c, lfo_s, lfo_cw, lfo_sw; /* oscilador por fasor rotatorio */
} rvb_line;

typedef struct rvb_ap {
	float *buf;
	size_t len;
	size_t pos;
} rvb_ap;

typedef struct rvb_state {
	float sr;
	float *mem;
	size_t w;
	/* lineas */
	float *pre;   size_t pre_mask;   /* pre-delay (estereo intercalado no: 2 buffers) */
	float *pre_r;
	float *er;    size_t er_mask;    /* linea de reflexiones tempranas (mono) */
	rvb_line line[RVB_LINES];
	rvb_ap ap[2][RVB_DIFF];
	size_t wl;    /* indice de escritura comun para pre, er y lineas */

	rvb_params target;
	int initialized;

	/* suavizados */
	float size_s, decay_s, pre_s, damp_s, erl_s, width_s, diff_s, mod_s, freeze_s;
	float duck_s, mix_s, gain_s, lowcut_s, highcut_s;
	float duck_env, duck_gain;
	int counter;

	/* coeficientes por bloque */
	float er_scale;
	sd_svf_coefs lc, hc;
	int lc_on, hc_on;
	sd_svf lc_st[2], hc_st[2];
	float dc_l, dc_r;
	unsigned rng;
} rvb_state;

int rvb_init(rvb_state *s, float sample_rate);
void rvb_free(rvb_state *s);
void rvb_reset(rvb_state *s);
void rvb_default_params(rvb_params *p);
void rvb_set_params(rvb_state *s, const rvb_params *p);
/* en sitio; r puede ser NULL (salida mono = media) */
void rvb_process(rvb_state *s, float *l, float *r, size_t frames);

#ifdef __cplusplus
}
#endif
