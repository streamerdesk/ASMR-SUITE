/*
 * Streamerdesk ASMR - 8D / Orbita - nucleo DSP (independiente de OBS)
 * Copyright (c) 2026 Streamerdesk. GPL-2.0-or-later. https://streamerdesk.com
 *
 * Mueve la voz alrededor de la cabeza del oyente (auriculares) con un modelo binaural:
 *   generador de movimiento (orbita continua / vaiven en arco / deriva aleatoria / sincronizado a tempo)
 *   -> sesgo "cerca de los oidos" -> por fuente virtual: pistas delante/detras y elevacion
 *      (estanteria de agudos + muesca tipo pabellon auditivo), distancia (nivel, aire, efecto proximidad)
 *   -> por oido: ITD (retardo fraccional Hermite, modelo esferico de Brown-Duda),
 *      sombra de la cabeza (filtro de Brown-Duda) e ILD de banda ancha (mayor en campo cercano)
 *   -> + sala suave (nucleo de la Reverb de la suite, estatica, mas presente al alejarse)
 *   -> mezcla seco/humedo (50 % = ambos al maximo) -> ganancia.
 * Dos fuentes virtuales: en "suma mono" coinciden; en "mantener estereo" L y R orbitan separadas +-30 grados.
 * Sin reservas de memoria ni bloqueos en process(); todo se suaviza (sin clics).
 */
#pragma once
#include <stddef.h>
#include "sd_dsp.h"
#include "../../reverb/src/reverb_dsp.h"

#ifdef __cplusplus
extern "C" {
#endif

enum orb_mode { ORB_ORBIT = 0, ORB_ARC = 1, ORB_WANDER = 2, ORB_TEMPO = 3 };

#define ORB_BLK 16
#define ORB_CHUNK 256
#define ORB_NSRC 2

typedef struct orb_params {
	int mode;          /* orb_mode (TEMPO se comporta como ORBIT; el periodo ya viene calculado) */
	float period_s;    /* segundos por vuelta (o por ida y vuelta en ARC); 0.5..1e6 */
	int dir;           /* +1 horario (delante -> derecha -> detras -> izquierda), -1 antihorario */
	float start_deg;   /* -180..180: 0 = delante, 90 = oido derecho, -90 = oido izquierdo */
	float arc_deg;     /* 10..360 anchura del arco en modo ARC */
	float distance_m;  /* 0.25..4 m radio de la orbita */
	float front_back;  /* 0..1 intensidad de las pistas delante/detras */
	float elevation;   /* 0..1 movimiento de elevacion (hasta +-35 grados) */
	float binaural;    /* 0..1 intensidad binaural (ITD, ILD, sombra) */
	float room;        /* 0..1 cantidad de sala */
	float near_ears;   /* 0..1 sesgo: pasa mas tiempo junto a los oidos */
	int mono_in;       /* 1 = suma mono */
	float mix;         /* 0..1 (0.5 = seco y procesado al 100 %) */
	float out_gain;    /* lineal */
} orb_params;

typedef struct orb_src {
	float *buf;
	size_t mask;
	sd_svf ls, hs, nt;
	sd_svf_coefs nt_c;
	/* valores actuales e incrementos por muestra (rampa lineal dentro del bloque) */
	float dL, dR, gL, gR, hsG, nd;
	float dL_i, dR_i, gL_i, gR_i, hsG_i, nd_i;
	float bL[3], bR[3]; /* Brown-Duda: b0, b1, a1 */
	float xL1, yL1, xR1, yR1;
} orb_src;

typedef struct orb_state {
	float sr;
	float *mem;
	orb_src src[ORB_NSRC];
	size_t w;
	float *room_l, *room_r;
	rvb_state room;
	int room_ok;
	float room_dn_set; int room_mono_set;

	orb_params target;
	int initialized;
	int counter;

	sd_svf_coefs ls_c, hs_c;
	/* suavizados */
	float start_s, arc_s, fb_s, elev_s, bin_s, room_s, near_s, dist_s, mono_s, mix_s, gain_s;
	float distG, distG_i, proxG, proxG_i, roomG, roomG_i;
	/* movimiento */
	double orb_acc, arc_ph, wan_acc, elev_ph;
	float w_vel, w_vel_t;
	long w_count;
	float az, el;
	unsigned rng;
} orb_state;

int orb_init(orb_state *s, float sample_rate);
void orb_free(orb_state *s);
void orb_default_params(orb_params *p);
void orb_set_params(orb_state *s, const orb_params *p);
/* en sitio; r puede ser NULL (salida mono = media) */
void orb_process(orb_state *s, float *l, float *r, size_t frames);
/* azimut actual (grados, -180..180) y elevacion (grados) para pruebas / medidores */
float orb_current_azimuth_deg(const orb_state *s);
float orb_current_elevation_deg(const orb_state *s);
/* periodo en segundos para el modo tempo: compases de 4 tiempos por vuelta */
static inline float orb_tempo_period(float bpm, float bars)
{
	if (bpm < 1.0f)
		bpm = 1.0f;
	return bars * 4.0f * 60.0f / bpm;
}

#ifdef __cplusplus
}
#endif
