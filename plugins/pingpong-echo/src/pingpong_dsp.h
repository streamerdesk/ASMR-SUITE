/*
 * Streamerdesk ASMR - Eco Ping-Pong - nucleo DSP (independiente de OBS)
 * Copyright (c) 2026 Streamerdesk. GPL-2.0-or-later.
 */
#pragma once
#include <stddef.h>
#include "sd_dsp.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PPE_MAX_DELAY_MS 4000.0f /* como TAL-Dub 1.1.2: max 4 s */

typedef struct ppe_params {
	float delay_l_ms;  /* 1..4000 */
	float delay_r_ms;  /* 1..4000 */
	float fb_l;        /* 0..1.2 (lineal) */
	float fb_r;        /* 0..1.2 */
	float damp_hz;     /* corte del paso-bajo resonante 12 dB/oct; >=20000 = sin filtro */
	float resonance;   /* 0..1 */
	float lowcut_hz;   /* corte del paso-alto 12 dB/oct; <=10 = sin filtro */
	float mix;         /* 0..1 dry/wet (0.5 = ambos al 100%) */
	float pingpong;    /* 0..1 (0 = eco estereo normal, 1 = rebote L/R total) */
	float balance;     /* -1..1 balance de salida */
	float out_gain;    /* ganancia lineal */
	int filter_fb;     /* 1 = filtro dentro de la realimentacion (FILTER FB), 0 = filtro post (FX) */
	int filter_in;     /* 1 = la senal seca tambien pasa por el filtro (IN) */
	int swap_lr;       /* 1 = intercambia L/R de la entrada (SWAP L/R) */
	int mono_in;       /* 1 = suma a mono antes de la linea de retardo */
	int start_right;   /* 1 = la primera repeticion sale por la derecha */
	int saturate;      /* 1 = limitador/saturacion suave en la realimentacion */
} ppe_params;

typedef struct ppe_state {
	float sample_rate;
	float *buf[2];
	size_t size; /* potencia de 2 */
	size_t mask;
	size_t w;

	ppe_params target;
	int initialized;

	/* valores suavizados */
	float d_smooth[2]; /* retardo en muestras */
	float fb_s[2], mix_s, pp_s, bal_s, gain_s;
	float damp_s, res_s, lowcut_s;

	/* coeficientes de filtro */
	sd_svf_coefs lp_c, hp_c;
	int lp_on, hp_on;
	int coef_counter;

	sd_svf lp_wet[2], hp_wet[2];
	sd_svf lp_dry[2], hp_dry[2];
} ppe_state;

/* Reserva memoria (fuera del hilo de audio). Devuelve 0 si OK. */
int ppe_init(ppe_state *s, float sample_rate);
void ppe_free(ppe_state *s);
void ppe_reset(ppe_state *s);
void ppe_default_params(ppe_params *p);
/* Copia los parametros objetivo; el procesado los suaviza. */
void ppe_set_params(ppe_state *s, const ppe_params *p);
/* Procesa en sitio. Si r == NULL se trata como mono (salida = media de L/R del eco). */
void ppe_process(ppe_state *s, float *l, float *r, size_t frames);

#ifdef __cplusplus
}
#endif
