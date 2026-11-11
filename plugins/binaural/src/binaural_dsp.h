/*
 * Streamerdesk ASMR Suite - Espacialidad binaural: motor DSP (sin dependencias de OBS).
 * Copyright (c) 2026 Streamerdesk. GPL-2.0-or-later.
 *
 * Coloca la voz en un punto FIJO del espacio 3D alrededor de la cabeza (auriculares) con un
 * modelo binaural parametrico (sin tablas HRTF de terceros):
 *   - ITD: formula de Woodworth sobre el angulo lateral (cabeza esferica, r = 8,75 cm).
 *   - Sombra de la cabeza / ILD: modelo de Brown & Duda (1998), un polo + un cero por oido.
 *   - ILD de campo cercano y realce de graves por proximidad a distancias cortas.
 *   - Pistas espectrales tipo pabellon auditivo: bandas direccionales (delante ~3,8 kHz,
 *     detras ~1,1 kHz + caida de agudos, encima ~8 kHz) y una muesca que sube con la elevacion.
 *   - Distancia: atenuacion (ley inversa), perdida de agudos por el aire, reflexiones de sala.
 *   - Deslizamiento suave (glide) entre posiciones, por el camino mas corto en azimut.
 * Memoria reservada en bin_init(); bin_process() no reserva ni bloquea.
 */
#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BIN_MAX_TAPS 8
#define BIN_BASE_DELAY 2.0f /* latencia fija en muestras (necesaria para la interpolacion Hermite) */

typedef struct bin_params {
	float azimuth;    /* grados: 0 delante, 90 derecha, 180 detras, 270 izquierda */
	float elevation;  /* grados: -45 (abajo) .. +90 (encima) */
	float distance;   /* 0 = pegado a la oreja .. 1 = al otro lado de la habitacion */
	float glide_s;    /* segundos para ir de una posicion a otra (0 = inmediato, con suavizado minimo) */
	float cues;       /* 0..2 intensidad de las pistas espectrales (delante/detras/arriba) */
	float prox;       /* 0..2 realce de graves de campo cercano */
	float air_loss;   /* 0..1 perdida de agudos con la distancia */
	float level_loss; /* 0..1 bajada de volumen con la distancia */
	float room;       /* 0..1 ambiente de sala */
	int mono_in;      /* 1 = suma mono */
	float mix;        /* 0..1 */
	float out_gain;   /* lineal */
} bin_params;

typedef struct bin_svf {
	float ic1, ic2;
	float a1, a2, a3, m0, m1, m2;
} bin_svf;

typedef struct bin_ear {
	bin_svf prox, hs, front, back, up, notch, air;
	float shadow_s;  /* estado TPT del filtro de sombra (1er orden) */
	float *buf;      /* linea de retardo ITD */
	float *dry;      /* seco retrasado BIN_BASE_DELAY para mezclar sin filtro peine */
} bin_ear;

typedef struct bin_state {
	float sr;
	bin_params target;
	/* posicion: valor que se desliza (glide lineal) y valor suavizado final */
	float g_az, g_el, g_dist;
	float st_az, st_el, st_dist;
	long glide_blocks;
	float last_t_az, last_t_el, last_t_dist;
	float az1, el1, dist1; /* primera etapa del suavizado */
	float az, el, dist; /* posicion efectiva actual */
	float pos_coef;
	/* resto de parametros suavizados */
	float s_cues, s_prox, s_air, s_level, s_room, s_mono, s_mix, s_gain;
	float ctrl_coef;
	int ctrl_counter;

	/* derivados por oido con rampa por muestra */
	float itd[2], itd_step[2];
	float gain[2], gain_step[2];
	float shadow_a[2], shadow_a_step[2]; /* ganancia de agudos de la sombra (alpha de Brown-Duda) */
	float shadow_g;                      /* coef. TPT del polo (fijo) */
	float g_er, g_er_step;

	bin_ear ear[2];
	size_t mask, w;
	float *er_buf;
	size_t er_mask, er_w;
	float er_lp, er_hp, er_lp_c, er_hp_c;
	float tap_delay[BIN_MAX_TAPS], tap_l[BIN_MAX_TAPS], tap_r[BIN_MAX_TAPS];
	int initialized;
} bin_state;

void bin_default_params(bin_params *p);
int bin_init(bin_state *s, float sample_rate);
void bin_free(bin_state *s);
void bin_set_params(bin_state *s, const bin_params *p);
void bin_snap(bin_state *s);
void bin_process(bin_state *s, float *l, float *r, size_t frames);
/* posicion efectiva actual (azimut normalizado 0..360) */
void bin_get_position(const bin_state *s, float *az, float *el, float *dist);
/* mapeos expuestos para tests */
float bin_itd_seconds(float azimuth, float elevation); /* >0: llega antes al oido derecho */
float bin_level_db(float distance, float level_loss);

#ifdef __cplusplus
}
#endif
