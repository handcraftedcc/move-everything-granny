#ifndef GRANNY_GRAIN_ENGINE_H
#define GRANNY_GRAIN_ENGINE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GRN_SAMPLE_RATE 44100
#define GRN_MAX_RENDER 256
#define GRN_MAX_VOICES 8
#define GRN_MAX_GRAINS_PER_VOICE_HIGH 48
#define GRN_WINDOW_LUT_SIZE 2048
#define GRN_MAX_SPAWNS_PER_BLOCK 4

typedef enum {
    GRN_QUALITY_ECO = 0,
    GRN_QUALITY_NORMAL = 1,
    GRN_QUALITY_HIGH = 2
} grn_quality_t;

typedef enum {
    GRN_TRIGGER_PER_VOICE = 0,
    GRN_TRIGGER_GLOBAL_CLOUD = 1
} grn_trigger_mode_t;

typedef struct {
    int active;
    int start_delay;
    float read_pos;
    float inc;
    int remaining;
    int total;
    float gain_l;
    float gain_r;
} grn_grain_t;

typedef struct {
    int active;
    int gate;
    int note;
    float velocity;
    uint32_t age;
    float emission_phase;
} grn_voice_t;

typedef struct {
    float position;
    float size_ms;
    float density;
    float spray;
    float jitter;
    int freeze;
    float pitch_semi;
    float fine_cents;
    float keytrack;
    int window_type;
    float window_shape;
    float grain_gain;
    int polyphony;
    int mono_legato;
    int trigger_mode;
    float spread;
    int quality;
} grn_params_t;

typedef struct {
    int sample_rate;
    uint32_t rng_state;
    uint32_t voice_counter;

    grn_params_t params;

    float sm_position;
    float sm_size_ms;
    float sm_density;
    float sm_spray;
    float sm_jitter;
    float sm_keytrack;
    float sm_grain_gain;
    float sm_spread;

    int freeze_prev;
    float frozen_position;
    uint32_t freeze_rng_state;

    float global_emission_phase;
    int round_robin_voice;

    grn_voice_t voices[GRN_MAX_VOICES];
    grn_grain_t grains[GRN_MAX_VOICES][GRN_MAX_GRAINS_PER_VOICE_HIGH];

    float window_lut[GRN_WINDOW_LUT_SIZE];
    float semitone_ratio_lut[241];
} grn_engine_t;

void grn_engine_init(grn_engine_t *engine);
void grn_engine_set_params(grn_engine_t *engine, const grn_params_t *params);

void grn_engine_note_on(grn_engine_t *engine, int note, float velocity);
void grn_engine_note_off(grn_engine_t *engine, int note);
void grn_engine_all_notes_off(grn_engine_t *engine);

int grn_engine_active_grains(const grn_engine_t *engine);
int grn_engine_active_voices(const grn_engine_t *engine);

void grn_engine_render(grn_engine_t *engine,
                       const float *sample_data,
                       int sample_len,
                       int sample_rate,
                       float *out_left,
                       float *out_right,
                       int frames);

#ifdef __cplusplus
}
#endif

#endif
