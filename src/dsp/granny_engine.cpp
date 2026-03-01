#include "granny_engine.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const int kTierMaxGrains[3] = {16, 32, 48};
static const float kTierDensityCap[3] = {20.0f, 40.0f, 60.0f};
static const int kTierSprayCapSamples[3] = {4096, 16384, 65536};

static inline float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static inline int clampi(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static inline uint32_t xorshift32(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static inline float rand01(uint32_t *state) {
    return (float)(xorshift32(state) & 0x00FFFFFFu) / 16777216.0f;
}

static inline int voice_has_active_grains(const grn_engine_t *engine, int voice_idx) {
    for (int i = 0; i < GRN_MAX_GRAINS_PER_VOICE_HIGH; i++) {
        if (engine->grains[voice_idx][i].active) {
            return 1;
        }
    }
    return 0;
}

static inline float semitone_ratio(const grn_engine_t *engine, float semitones) {
    float clamped = clampf(semitones, -120.0f, 120.0f);
    float idxf = clamped + 120.0f;
    int idx = (int)idxf;
    float frac = idxf - (float)idx;
    if (idx < 0) idx = 0;
    if (idx > 239) idx = 239;
    float a = engine->semitone_ratio_lut[idx];
    float b = engine->semitone_ratio_lut[idx + 1];
    return a + (b - a) * frac;
}

static inline float window_sample(const grn_engine_t *engine, int window_type, float t, float shape) {
    if (t <= 0.0f || t >= 1.0f) return 0.0f;

    float centered = t - 0.5f;
    float scale = 1.0f - 0.85f * clampf(shape, 0.0f, 1.0f);
    float warped = 0.5f + centered * scale;
    warped = clampf(warped, 0.0f, 1.0f);

    if (window_type == 1) {
        /* Triangle window */
        return 1.0f - fabsf(2.0f * warped - 1.0f);
    }

    if (window_type == 2) {
        /* Blackman window */
        float w = 2.0f * (float)M_PI * warped;
        return 0.42f - 0.5f * cosf(w) + 0.08f * cosf(2.0f * w);
    }

    /* Hann window (default, LUT) */
    float pos = warped * (float)(GRN_WINDOW_LUT_SIZE - 1);
    int idx = (int)pos;
    float frac = pos - (float)idx;
    int idx2 = (idx < GRN_WINDOW_LUT_SIZE - 1) ? (idx + 1) : idx;

    float a = engine->window_lut[idx];
    float b = engine->window_lut[idx2];
    return a + (b - a) * frac;
}

static int find_free_voice(const grn_engine_t *engine, int polyphony) {
    for (int i = 0; i < polyphony; i++) {
        if (!engine->voices[i].active && !voice_has_active_grains(engine, i)) {
            return i;
        }
    }

    int oldest_released = -1;
    uint32_t oldest_released_age = 0xFFFFFFFFu;
    for (int i = 0; i < polyphony; i++) {
        if (engine->voices[i].active && !engine->voices[i].gate && engine->voices[i].age < oldest_released_age) {
            oldest_released_age = engine->voices[i].age;
            oldest_released = i;
        }
    }
    if (oldest_released >= 0) return oldest_released;

    int oldest_active = 0;
    uint32_t oldest_active_age = 0xFFFFFFFFu;
    for (int i = 0; i < polyphony; i++) {
        if (engine->voices[i].age < oldest_active_age) {
            oldest_active_age = engine->voices[i].age;
            oldest_active = i;
        }
    }
    return oldest_active;
}

void grn_engine_init(grn_engine_t *engine) {
    memset(engine, 0, sizeof(*engine));
    engine->sample_rate = GRN_SAMPLE_RATE;
    engine->rng_state = 0xA5F31E2Du;
    engine->voice_counter = 1;

    for (int i = 0; i < GRN_WINDOW_LUT_SIZE; i++) {
        float t = (float)i / (float)(GRN_WINDOW_LUT_SIZE - 1);
        engine->window_lut[i] = 0.5f - 0.5f * cosf(2.0f * (float)M_PI * t);
    }

    for (int i = 0; i <= 240; i++) {
        float semi = (float)(i - 120);
        engine->semitone_ratio_lut[i] = powf(2.0f, semi / 12.0f);
    }

    grn_params_t defaults;
    defaults.position = 0.5f;
    defaults.size_ms = 60.0f;
    defaults.density = 18.0f;
    defaults.spray = 0.15f;
    defaults.jitter = 0.10f;
    defaults.freeze = 0;
    defaults.pitch_semi = 0.0f;
    defaults.fine_cents = 0.0f;
    defaults.keytrack = 1.0f;
    defaults.window_type = 0;
    defaults.window_shape = 0.35f;
    defaults.grain_gain = 0.72f;
    defaults.polyphony = 4;
    defaults.mono_legato = 0;
    defaults.trigger_mode = GRN_TRIGGER_PER_VOICE;
    defaults.spread = 0.2f;
    defaults.quality = GRN_QUALITY_NORMAL;

    engine->sm_position = defaults.position;
    engine->sm_size_ms = defaults.size_ms;
    engine->sm_density = defaults.density;
    engine->sm_spray = defaults.spray;
    engine->sm_jitter = defaults.jitter;
    engine->sm_keytrack = defaults.keytrack;
    engine->sm_grain_gain = defaults.grain_gain;
    engine->sm_spread = defaults.spread;

    grn_engine_set_params(engine, &defaults);
}

void grn_engine_set_params(grn_engine_t *engine, const grn_params_t *params) {
    grn_params_t p = *params;

    p.position = clampf(p.position, 0.0f, 1.0f);
    p.size_ms = clampf(p.size_ms, 5.0f, 250.0f);
    p.density = clampf(p.density, 1.0f, 60.0f);
    p.spray = clampf(p.spray, 0.0f, 1.0f);
    p.jitter = clampf(p.jitter, 0.0f, 1.0f);
    p.freeze = p.freeze ? 1 : 0;
    p.pitch_semi = clampf(p.pitch_semi, -24.0f, 24.0f);
    p.fine_cents = clampf(p.fine_cents, -100.0f, 100.0f);
    p.keytrack = clampf(p.keytrack, 0.0f, 1.0f);
    p.window_type = clampi(p.window_type, 0, 2);
    p.window_shape = clampf(p.window_shape, 0.0f, 1.0f);
    p.grain_gain = clampf(p.grain_gain, 0.0f, 1.0f);
    p.polyphony = clampi(p.polyphony, 1, GRN_MAX_VOICES);
    p.mono_legato = p.mono_legato ? 1 : 0;
    p.trigger_mode = clampi(p.trigger_mode, 0, 1);
    p.spread = clampf(p.spread, 0.0f, 1.0f);
    p.quality = clampi(p.quality, 0, 2);

    if (p.mono_legato) {
        p.polyphony = 1;
    }

    float density_cap = kTierDensityCap[p.quality];
    if (p.density > density_cap) {
        p.density = density_cap;
    }

    int prev_polyphony = engine->params.polyphony;
    int prev_freeze = engine->params.freeze;
    engine->params = p;

    if (!prev_freeze && p.freeze) {
        engine->frozen_position = p.position;
        engine->freeze_rng_state = engine->rng_state;
        engine->freeze_prev = 1;
    } else if (prev_freeze && !p.freeze) {
        engine->freeze_prev = 0;
    }

    if (p.polyphony < prev_polyphony) {
        for (int i = p.polyphony; i < GRN_MAX_VOICES; i++) {
            engine->voices[i].active = 0;
            engine->voices[i].gate = 0;
            for (int j = 0; j < GRN_MAX_GRAINS_PER_VOICE_HIGH; j++) {
                engine->grains[i][j].active = 0;
            }
        }
    }
}

void grn_engine_note_on(grn_engine_t *engine, int note, float velocity) {
    int polyphony = engine->params.polyphony;
    if (polyphony < 1) polyphony = 1;

    if (engine->params.mono_legato) {
        grn_voice_t *v = &engine->voices[0];
        if (v->active && v->gate) {
            v->note = note;
            v->velocity = clampf(velocity, 0.0f, 1.0f);
            v->age = engine->voice_counter++;
            return;
        }
        v->active = 1;
        v->gate = 1;
        v->note = note;
        v->velocity = clampf(velocity, 0.0f, 1.0f);
        v->age = engine->voice_counter++;
        v->emission_phase = 0.0f;
        return;
    }

    int idx = find_free_voice(engine, polyphony);
    grn_voice_t *v = &engine->voices[idx];
    v->active = 1;
    v->gate = 1;
    v->note = note;
    v->velocity = clampf(velocity, 0.0f, 1.0f);
    v->age = engine->voice_counter++;
    v->emission_phase = 0.0f;
}

void grn_engine_note_off(grn_engine_t *engine, int note) {
    int polyphony = engine->params.polyphony;
    for (int i = 0; i < polyphony; i++) {
        grn_voice_t *v = &engine->voices[i];
        if (v->active && v->gate && v->note == note) {
            v->gate = 0;
        }
    }
}

void grn_engine_all_notes_off(grn_engine_t *engine) {
    for (int i = 0; i < GRN_MAX_VOICES; i++) {
        engine->voices[i].active = 0;
        engine->voices[i].gate = 0;
        engine->voices[i].emission_phase = 0.0f;
        for (int j = 0; j < GRN_MAX_GRAINS_PER_VOICE_HIGH; j++) {
            engine->grains[i][j].active = 0;
        }
    }
    engine->global_emission_phase = 0.0f;
}

int grn_engine_active_grains(const grn_engine_t *engine) {
    int count = 0;
    int polyphony = engine->params.polyphony;
    for (int i = 0; i < polyphony; i++) {
        for (int j = 0; j < GRN_MAX_GRAINS_PER_VOICE_HIGH; j++) {
            if (engine->grains[i][j].active) count++;
        }
    }
    return count;
}

int grn_engine_active_voices(const grn_engine_t *engine) {
    int count = 0;
    int polyphony = engine->params.polyphony;
    for (int i = 0; i < polyphony; i++) {
        const grn_voice_t *v = &engine->voices[i];
        if (v->active || voice_has_active_grains(engine, i)) {
            count++;
        }
    }
    return count;
}

static void spawn_grain(grn_engine_t *engine,
                        int voice_idx,
                        int sample_len,
                        int sample_rate,
                        int frames,
                        float density_cap,
                        int spray_cap_samples,
                        int max_grains_per_voice) {
    grn_voice_t *voice = &engine->voices[voice_idx];
    if (!voice->active) return;
    if (sample_len < 2) return;

    int slot = -1;
    for (int i = 0; i < max_grains_per_voice; i++) {
        if (!engine->grains[voice_idx][i].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return;

    uint32_t *rng = engine->params.freeze ? &engine->freeze_rng_state : &engine->rng_state;

    int size_samples = (int)(engine->sm_size_ms * 0.001f * (float)engine->sample_rate);
    size_samples = clampi(size_samples, 16, engine->sample_rate / 2);

    float base_pos = engine->params.freeze ? engine->frozen_position : engine->sm_position;
    int center = (int)(clampf(base_pos, 0.0f, 1.0f) * (float)(sample_len - 1));

    int max_offset = (int)(engine->sm_spray * (float)sample_len);
    if (max_offset > spray_cap_samples) max_offset = spray_cap_samples;
    if (max_offset < 0) max_offset = 0;

    int offset = 0;
    if (max_offset > 0) {
        float r = rand01(rng) * 2.0f - 1.0f;
        offset = (int)(r * (float)max_offset);
    }

    int start_idx = center + offset;
    int loop_len = sample_len - 1;
    while (start_idx < 0) start_idx += loop_len;
    while (start_idx >= loop_len) start_idx -= loop_len;

    float semis = engine->params.pitch_semi + engine->params.fine_cents * 0.01f;
    semis += ((float)voice->note - 60.0f) * engine->sm_keytrack;
    float pitch_ratio = semitone_ratio(engine, semis);

    float src_to_out = (float)sample_rate / (float)engine->sample_rate;
    float inc = src_to_out * pitch_ratio;
    inc = clampf(inc, 0.01f, 8.0f);

    float jitter = clampf(engine->sm_jitter, 0.0f, 1.0f);
    int start_delay = 0;
    if (jitter > 0.0f && frames > 1) {
        start_delay = (int)(rand01(rng) * jitter * (float)(frames - 1));
    }

    float velocity_gain = 0.25f + 0.75f * voice->velocity;
    float density_norm = clampf(engine->sm_density / density_cap, 0.1f, 1.0f);
    float base_gain = engine->sm_grain_gain * velocity_gain * (0.18f + 0.22f * (1.0f - density_norm));

    float pan = (rand01(rng) * 2.0f - 1.0f) * engine->sm_spread;
    float theta = (pan + 1.0f) * 0.25f * (float)M_PI;
    float gain_l = cosf(theta) * base_gain;
    float gain_r = sinf(theta) * base_gain;

    grn_grain_t *g = &engine->grains[voice_idx][slot];
    g->active = 1;
    g->start_delay = start_delay;
    g->read_pos = (float)start_idx;
    g->inc = inc;
    g->remaining = size_samples;
    g->total = size_samples;
    g->gain_l = gain_l;
    g->gain_r = gain_r;
}

static void schedule_spawns(grn_engine_t *engine,
                            int sample_len,
                            int sample_rate,
                            int frames,
                            float density,
                            float density_cap,
                            int spray_cap_samples,
                            int max_grains_per_voice) {
    int polyphony = engine->params.polyphony;

    if (engine->params.trigger_mode == GRN_TRIGGER_GLOBAL_CLOUD) {
        int active_voices[GRN_MAX_VOICES];
        int count = 0;
        for (int i = 0; i < polyphony; i++) {
            if (engine->voices[i].active && engine->voices[i].gate) {
                active_voices[count++] = i;
            }
        }
        if (count == 0) return;

        engine->global_emission_phase += density * (float)frames / (float)engine->sample_rate;
        int spawn_count = (int)engine->global_emission_phase;
        if (spawn_count > GRN_MAX_SPAWNS_PER_BLOCK) {
            spawn_count = GRN_MAX_SPAWNS_PER_BLOCK;
        }
        engine->global_emission_phase -= (float)spawn_count;

        for (int i = 0; i < spawn_count; i++) {
            int v = active_voices[engine->round_robin_voice % count];
            engine->round_robin_voice++;
            spawn_grain(engine, v, sample_len, sample_rate, frames,
                        density_cap, spray_cap_samples, max_grains_per_voice);
        }
        return;
    }

    for (int i = 0; i < polyphony; i++) {
        grn_voice_t *v = &engine->voices[i];
        if (!v->active || !v->gate) {
            continue;
        }

        v->emission_phase += density * (float)frames / (float)engine->sample_rate;
        int spawn_count = (int)v->emission_phase;
        if (spawn_count > GRN_MAX_SPAWNS_PER_BLOCK) {
            spawn_count = GRN_MAX_SPAWNS_PER_BLOCK;
        }
        v->emission_phase -= (float)spawn_count;

        for (int s = 0; s < spawn_count; s++) {
            spawn_grain(engine, i, sample_len, sample_rate, frames,
                        density_cap, spray_cap_samples, max_grains_per_voice);
        }
    }
}

void grn_engine_render(grn_engine_t *engine,
                       const float *sample_data,
                       int sample_len,
                       int sample_rate,
                       float *out_left,
                       float *out_right,
                       int frames) {
    if (frames > GRN_MAX_RENDER) frames = GRN_MAX_RENDER;
    if (frames < 1) return;

    for (int i = 0; i < frames; i++) {
        out_left[i] = 0.0f;
        out_right[i] = 0.0f;
    }

    const float smooth = 0.08f;
    engine->sm_position += (engine->params.position - engine->sm_position) * smooth;
    engine->sm_size_ms += (engine->params.size_ms - engine->sm_size_ms) * smooth;
    engine->sm_density += (engine->params.density - engine->sm_density) * smooth;
    engine->sm_spray += (engine->params.spray - engine->sm_spray) * smooth;
    engine->sm_jitter += (engine->params.jitter - engine->sm_jitter) * smooth;
    engine->sm_keytrack += (engine->params.keytrack - engine->sm_keytrack) * smooth;
    engine->sm_grain_gain += (engine->params.grain_gain - engine->sm_grain_gain) * smooth;
    engine->sm_spread += (engine->params.spread - engine->sm_spread) * smooth;

    int quality = clampi(engine->params.quality, 0, 2);
    float density_cap = kTierDensityCap[quality];
    int spray_cap_samples = kTierSprayCapSamples[quality];
    int max_grains_per_voice = kTierMaxGrains[quality];

    float density = clampf(engine->sm_density, 1.0f, density_cap);

    schedule_spawns(engine, sample_len, sample_rate, frames,
                    density, density_cap, spray_cap_samples, max_grains_per_voice);

    if (!sample_data || sample_len < 2) {
        for (int i = 0; i < engine->params.polyphony; i++) {
            if (!engine->voices[i].gate) {
                engine->voices[i].active = 0;
            }
            for (int g = 0; g < GRN_MAX_GRAINS_PER_VOICE_HIGH; g++) {
                engine->grains[i][g].active = 0;
            }
        }
        return;
    }

    int loop_len = sample_len - 1;
    int polyphony = engine->params.polyphony;
    float shape = engine->params.window_shape;
    int window_type = engine->params.window_type;

    for (int v = 0; v < polyphony; v++) {
        for (int g = 0; g < max_grains_per_voice; g++) {
            grn_grain_t *grain = &engine->grains[v][g];
            if (!grain->active) continue;

            for (int i = 0; i < frames; i++) {
                if (!grain->active) break;

                if (grain->start_delay > 0) {
                    grain->start_delay--;
                    continue;
                }

                if (grain->remaining <= 0 || grain->total <= 0) {
                    grain->active = 0;
                    break;
                }

                int idx = (int)grain->read_pos;
                if (idx < 0) idx = 0;
                if (idx >= loop_len) idx = loop_len - 1;

                float frac = grain->read_pos - (float)idx;
                if (frac < 0.0f) frac = 0.0f;
                if (frac > 1.0f) frac = 1.0f;

                float s0 = sample_data[idx];
                float s1 = sample_data[idx + 1];
                float sample = s0 + (s1 - s0) * frac;

                int life = grain->total - grain->remaining;
                float t = (float)life / (float)grain->total;
                float env = window_sample(engine, window_type, t, shape);

                out_left[i] += sample * env * grain->gain_l;
                out_right[i] += sample * env * grain->gain_r;

                grain->read_pos += grain->inc;
                while (grain->read_pos >= (float)loop_len) grain->read_pos -= (float)loop_len;
                while (grain->read_pos < 0.0f) grain->read_pos += (float)loop_len;

                grain->remaining--;
                if (grain->remaining <= 0) {
                    grain->active = 0;
                }
            }
        }

        if (!engine->voices[v].gate && !voice_has_active_grains(engine, v)) {
            engine->voices[v].active = 0;
        }
    }
}
