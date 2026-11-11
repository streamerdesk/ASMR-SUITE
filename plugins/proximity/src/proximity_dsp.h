/*
 * Streamerdesk ASMR Suite - Proximidad: motor DSP (sin dependencias de OBS).
 * Copyright (c) 2026 Streamerdesk. GPL-2.0-or-later.
 *
 * Simula que la voz se acerca o se aleja del oido del oyente (auriculares):
 *   corte de retumbes -> efecto proximidad (low shelf) -> aire (high shelf)
 *   -> perdida de presencia/agudos por distancia -> suavizado de eses (dinamico)
 *   -> calidez (saturacion suave) -> atenuacion por distancia
 *   -> posicion lateral binaural (ITD / ILD / sombra de la cabeza)
 *   + primeras reflexiones de habitacion que crecen con la distancia.
 *
 * Toda la memoria se reserva en prox_init(); prox_process() no reserva nada.
 * Los parametros se suavizan internamente (sin clics al moverlos).
 */
#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PROX_MAX_TAPS 8

typedef struct prox_params {
	float distance;    /* 0 = pegado al oido ... 1 = lejos */
	float prox_amount; /* 0..2  escala del realce de graves por cercania */
	float prox_freq;   /* Hz    frecuencia del efecto proximidad (80..300) */
	float air_db;      /* dB    brillo/aire (high shelf 7 kHz), -6..+6 */
	float hf_loss;     /* 0..1  perdida de agudos con la distancia */
	float level_loss;  /* 0..1  bajada de volumen con la distancia */
	float room;        /* 0..1  ambiente de habitacion (primeras reflexiones) */
	float position;    /* -1 = oido izquierdo, 0 = centro, +1 = oido derecho */
	int binaural;      /* 1 = ITD/ILD/sombra de cabeza, 0 = panorama simple */
	float warmth;      /* 0..1  saturacion suave */
	int deharsh;       /* 1 = suavizar eses activo */
	float deharsh_amt; /* 0..1 */
	float lowcut_hz;   /* Hz    20 = practicamente apagado */
	int mono_in;       /* 1 = suma mono de la entrada */
	float mix;         /* 0..1  intensidad del efecto (seco/procesado) */
	float out_gain;    /* lineal */
} prox_params;

/* filtro SVF de Simper/Zavalishin con salidas mezcladas (estable al modular) */
typedef struct prox_svf {
	float ic1, ic2;
	float a1, a2, a3, m0, m1, m2;
} prox_svf;

typedef struct prox_chan {
	prox_svf lowcut, shelf, air, presence, lowpass, ds_split;
	float dc_x1, dc_y1;
	float ds_env_hf, ds_env_all;
	float shadow_lp; /* sombra de la cabeza (1 polo) */
	float ds_gain;   /* ganancia actual del suavizado de eses */
} prox_chan;

typedef struct prox_state {
	float sr;
	prox_params target; /* lo que pide el usuario */
	/* valores suavizados actuales */
	float s_distance, s_prox_amount, s_prox_freq, s_air_db, s_hf_loss, s_level_loss, s_room;
	float s_position, s_binaural, s_warmth, s_deharsh, s_lowcut, s_mono, s_mix, s_out_gain;
	float smooth_coef; /* por muestra */
	float ctrl_coef;   /* por bloque de control */
	int ctrl_counter;

	/* valores derivados (calculados cada bloque de control, con rampa por muestra) */
	float g_level, g_level_step;     /* atenuacion directa */
	float g_er, g_er_step;           /* nivel de reflexiones */
	/* posicion: ganancia, retardo (ITD) y sombra de cabeza POR OIDO (0 = izq, 1 = der).
	 * Son funciones continuas de la posicion, asi que cruzar el centro no produce saltos. */
	float g_ear[2], g_ear_step[2];
	float itd_ear[2], itd_ear_step[2];  /* muestras */
	float shadow_amt[2], shadow_amt_step[2];
	float shadow_coef;                  /* coef 1 polo del filtro de sombra */
	float drive, drive_step;
	float wet_warm, wet_warm_step;
	float ds_thresh;                 /* relacion HF/total a partir de la cual se reduce */
	float ds_depth;                  /* reduccion maxima (lineal 0..1) */
	float env_att, env_rel;

	prox_chan ch[2];

	/* linea de retardo para ITD (por canal de salida) y reflexiones (mono) */
	float *itd_buf[2];
	size_t itd_mask;
	size_t itd_w;
	float *er_buf;
	size_t er_mask;
	size_t er_w;
	float er_lp; /* paso-bajo de 1 polo de la entrada de reflexiones */
	float er_lp_coef;
	float er_hp; /* estado de corte de graves de las reflexiones */
	float er_hp_coef;
	int ntaps;
	float tap_delay[PROX_MAX_TAPS]; /* muestras */
	float tap_gain_l[PROX_MAX_TAPS];
	float tap_gain_r[PROX_MAX_TAPS];
	int initialized;
} prox_state;

void prox_default_params(prox_params *p);
int prox_init(prox_state *s, float sample_rate); /* 0 = ok */
void prox_free(prox_state *s);
void prox_reset(prox_state *s);
/* fija los parametros objetivo; el motor los alcanza suavemente */
void prox_set_params(prox_state *s, const prox_params *p);
/* salta directamente a los parametros objetivo (sin rampa; solo al crear/tests) */
void prox_snap(prox_state *s);
/* procesa en el sitio. r puede ser NULL (salida mono en l). */
void prox_process(prox_state *s, float *l, float *r, size_t frames);

/* funciones de mapeo (expuestas para los tests) */
float prox_shelf_db(float distance, float amount);
float prox_level_db(float distance, float level_loss);
float prox_lowpass_hz(float distance, float hf_loss);

#ifdef __cplusplus
}
#endif
