/*
 * Streamerdesk ASMR Suite - Susurro de oido a oido: nucleo DSP (sin OBS).
 * Copyright (c) 2026 Streamerdesk. GPL-2.0-or-later.
 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "sd_dsp.h"

#define ETE_MAX_ITD_MS 0.7f
#define ETE_RING 256 /* potencia de 2; 0.7 ms a 192 kHz = 134 muestras */

enum ete_mode { ETE_MODE_MANUAL = 0, ETE_MODE_AUTO = 1, ETE_MODE_RANDOM = 2, ETE_MODE_TEMPO = 3 };
enum ete_shape { ETE_SHAPE_SINE = 0, ETE_SHAPE_HOLD = 1 };
enum ete_pan { ETE_PAN_EQUAL_POWER = 0, ETE_PAN_BINAURAL = 1 };

typedef struct ete_params {
	int mode;            /* ete_mode */
	float position;      /* -1 (izq) .. +1 (der), modo manual */
	float sweep_s;       /* segundos que tarda en ir de una oreja a la otra (modos auto/aleatorio/tempo) */
	float depth;         /* 0..1 anchura del recorrido */
	int shape;           /* ete_shape */
	float dwell;         /* 0..0.9 fraccion del tiempo parado en cada oreja (forma HOLD) */
	int pan_mode;        /* ete_pan */
	float binaural;      /* 0..1 intensidad binaural (ITD/ILD/sombra) */
	float proximity;     /* 0..1 calidez "pegado a la oreja" en los extremos */
	float center_keep;   /* 0..1 parte de la voz que siempre queda en el centro */
	float center_dip_db; /* 0..6 dB menos cuando la voz pasa por el centro */
	float out_gain;      /* lineal */
	int start_right;     /* 0 empieza por la izquierda, 1 por la derecha */
} ete_params;

typedef struct ete_state {
	float sr;
	ete_params p;
	/* LFO */
	double phase;  /* 0..1 ciclo completo (ida y vuelta) */
	/* aleatorio */
	uint32_t rng;
	float rnd_from, rnd_to, rnd_t, rnd_len;
	/* suavizado de posicion (dos polos en cascada) */
	float pos_s1, pos_s2, pos_coef;
	/* suavizadores de parametros */
	float depth_s, bin_s, prox_s, keep_s, dip_s, gain_s, par_coef;
	float ep_mix_s; /* 0 = ley equal-power, 1 = binaural (fundido sin clics al cambiar) */
	/* linea de retardo ITD */
	float ring[ETE_RING];
	unsigned w;
	/* sombra de cabeza: paso bajo de un polo por oreja */
	float lp_l, lp_r;
	/* proximidad: SVF paso banda fijo en graves-medios */
	sd_svf prox_f;
	sd_svf_coefs prox_c;
	float last_pos; /* para pruebas/medidores */
} ete_state;

void ete_default_params(ete_params *p);
void ete_init(ete_state *s, float sample_rate, const ete_params *p);
void ete_set_params(ete_state *s, const ete_params *p);
/* l y r pueden ser el mismo buffer de entrada; si r es NULL la salida es solo l (mono) */
void ete_process(ete_state *s, float *l, float *r, size_t frames);

/* funciones puras, expuestas para las pruebas */
void ete_gains(int pan_mode_binaural_mix_unused, float pos, float ep_mix, float bin, float *gl, float *gr);
float ete_itd_samples(float pos, float bin, float sr, int left_ear);
float ete_curve(double phase, int shape, float dwell);
