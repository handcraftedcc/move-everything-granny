#include <atomic>
#include <dirent.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>

extern "C" {
#define MOVE_PLUGIN_API_VERSION 1
#define MOVE_SAMPLE_RATE 44100
#define MOVE_FRAMES_PER_BLOCK 128
#define MOVE_MIDI_SOURCE_INTERNAL 0
#define MOVE_MIDI_SOURCE_EXTERNAL 2

typedef struct host_api_v1 {
    uint32_t api_version;
    int sample_rate;
    int frames_per_block;
    uint8_t *mapped_memory;
    int audio_out_offset;
    int audio_in_offset;
    void (*log)(const char *msg);
    int (*midi_send_internal)(const uint8_t *msg, int len);
    int (*midi_send_external)(const uint8_t *msg, int len);
} host_api_v1_t;

#define MOVE_PLUGIN_API_VERSION_2 2

typedef struct plugin_api_v2 {
    uint32_t api_version;
    void *(*create_instance)(const char *module_dir, const char *json_defaults);
    void (*destroy_instance)(void *instance);
    void (*on_midi)(void *instance, const uint8_t *msg, int len, int source);
    void (*set_param)(void *instance, const char *key, const char *val);
    int (*get_param)(void *instance, const char *key, char *buf, int buf_len);
    int (*get_error)(void *instance, char *buf, int buf_len);
    void (*render_block)(void *instance, int16_t *out_interleaved_lr, int frames);
} plugin_api_v2_t;
}

#include "granny_engine.h"

static const host_api_v1_t *g_host = NULL;

static void plugin_log(const char *msg) {
    if (!g_host || !g_host->log || !msg) return;
    char line[256];
    snprintf(line, sizeof(line), "[granny] %s", msg);
    g_host->log(line);
}

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

typedef struct {
    float *data;
    int length;
    int sample_rate;
} sample_buffer_t;

typedef struct {
    uint8_t bytes[3];
    int len;
    int source;
} midi_event_t;

enum {
    MIDI_QUEUE_SIZE = 256,
    RETIRE_QUEUE_SIZE = 128,
    MAX_WAV_FILES = 128
};

typedef enum {
    PARAM_FLOAT = 0,
    PARAM_INT = 1,
    PARAM_BOOL = 2
} param_type_t;

typedef struct {
    const char *key;
    const char *name;
    param_type_t type;
    float min_val;
    float max_val;
    size_t offset;
} param_meta_t;

static const param_meta_t g_params[] = {
    {"position", "Position", PARAM_FLOAT, 0.0f, 1.0f, offsetof(grn_params_t, position)},
    {"scan", "Scan", PARAM_FLOAT, -1.0f, 1.0f, offsetof(grn_params_t, scan)},
    {"size_ms", "Size", PARAM_FLOAT, 5.0f, 500.0f, offsetof(grn_params_t, size_ms)},
    {"density", "Density", PARAM_FLOAT, 1.0f, 60.0f, offsetof(grn_params_t, density)},
    {"spray", "Spray", PARAM_FLOAT, 0.0f, 1.0f, offsetof(grn_params_t, spray)},
    {"jitter", "Jitter", PARAM_FLOAT, 0.0f, 1.0f, offsetof(grn_params_t, jitter)},
    {"freeze", "Freeze", PARAM_BOOL, 0.0f, 1.0f, offsetof(grn_params_t, freeze)},
    {"pitch_semi", "Pitch", PARAM_INT, -24.0f, 24.0f, offsetof(grn_params_t, pitch_semi)},
    {"fine_cents", "Fine", PARAM_FLOAT, -100.0f, 100.0f, offsetof(grn_params_t, fine_cents)},
    {"keytrack", "KeyTrack", PARAM_FLOAT, 0.0f, 1.0f, offsetof(grn_params_t, keytrack)},
    {"window_type", "Window", PARAM_INT, 0.0f, 2.0f, offsetof(grn_params_t, window_type)},
    {"window_shape", "WinShape", PARAM_FLOAT, 0.0f, 1.0f, offsetof(grn_params_t, window_shape)},
    {"grain_gain", "GrainGain", PARAM_FLOAT, 0.0f, 1.0f, offsetof(grn_params_t, grain_gain)},
    {"polyphony", "PolyVoices", PARAM_INT, 1.0f, 8.0f, offsetof(grn_params_t, polyphony)},
    {"play_mode", "Play Mode", PARAM_INT, 0.0f, 2.0f, offsetof(grn_params_t, play_mode)},
    {"portamento_ms", "Porta Time", PARAM_FLOAT, 0.0f, 2000.0f, offsetof(grn_params_t, portamento_ms)},
    {"trigger_mode", "Trigger", PARAM_INT, 0.0f, 1.0f, offsetof(grn_params_t, trigger_mode)},
    {"scan_end_mode", "Scan End", PARAM_INT, 0.0f, 3.0f, offsetof(grn_params_t, scan_end_mode)},
    {"spread", "Spread", PARAM_FLOAT, 0.0f, 1.0f, offsetof(grn_params_t, spread)},
    {"quality", "Quality", PARAM_INT, 0.0f, 2.0f, offsetof(grn_params_t, quality)},
};

#define PARAM_COUNT ((int)(sizeof(g_params) / sizeof(g_params[0])))

static const char *kWindowOptions[] = {"hann", "triangle", "blackman"};
static const char *kQualityOptions[] = {"eco", "normal", "high"};
static const char *kTriggerOptions[] = {"per_voice", "global_cloud"};
static const char *kScanEndOptions[] = {"wrap", "pingpong", "clamp", "stop"};
static const char *kPlayModeOptions[] = {"mono", "portamento", "poly"};
static const char *kOnOffOptions[] = {"off", "on"};

static const char *enum_value_to_string(const char *key, int value) {
    if (!key) return NULL;
    if (strcmp(key, "window_type") == 0) {
        if (value >= 0 && value < 3) return kWindowOptions[value];
        return NULL;
    }
    if (strcmp(key, "quality") == 0) {
        if (value >= 0 && value < 3) return kQualityOptions[value];
        return NULL;
    }
    if (strcmp(key, "trigger_mode") == 0) {
        if (value >= 0 && value < 2) return kTriggerOptions[value];
        return NULL;
    }
    if (strcmp(key, "scan_end_mode") == 0) {
        if (value >= 0 && value < 4) return kScanEndOptions[value];
        return NULL;
    }
    if (strcmp(key, "play_mode") == 0) {
        if (value >= 0 && value < 3) return kPlayModeOptions[value];
        return NULL;
    }
    if (strcmp(key, "freeze") == 0) {
        if (value >= 0 && value < 2) return kOnOffOptions[value];
        return NULL;
    }
    return NULL;
}

static int parse_enum_value(const char *key, const char *val, int *out) {
    if (!key || !val || !out) return 0;

    char *endp = NULL;
    long iv = strtol(val, &endp, 10);
    if (endp && *endp == '\0') {
        *out = (int)iv;
        return 1;
    }

    const char **options = NULL;
    int count = 0;

    if (strcmp(key, "window_type") == 0) {
        options = kWindowOptions;
        count = 3;
    } else if (strcmp(key, "quality") == 0) {
        options = kQualityOptions;
        count = 3;
    } else if (strcmp(key, "trigger_mode") == 0) {
        options = kTriggerOptions;
        count = 2;
    } else if (strcmp(key, "scan_end_mode") == 0) {
        if (strcasecmp(val, "ping-pong") == 0) {
            *out = 1;
            return 1;
        }
        options = kScanEndOptions;
        count = 4;
    } else if (strcmp(key, "play_mode") == 0) {
        if (strcasecmp(val, "pornamento") == 0) {
            *out = 1;
            return 1;
        }
        options = kPlayModeOptions;
        count = 3;
    } else {
        return 0;
    }

    for (int i = 0; i < count; i++) {
        if (strcasecmp(val, options[i]) == 0) {
            *out = i;
            return 1;
        }
    }
    return 0;
}

typedef struct {
    char module_dir[512];
    char current_sample_rel[512];
    char current_sample_name[128];
    char last_error[256];
    int sample_index;
    int wav_count;
    char wav_rel_paths[MAX_WAV_FILES][512];
    char wav_names[MAX_WAV_FILES][128];

    grn_engine_t engine;
    grn_params_t params;

    std::atomic<sample_buffer_t *> active_sample;

    std::atomic<uint32_t> retire_write;
    std::atomic<uint32_t> retire_read;
    sample_buffer_t *retire_queue[RETIRE_QUEUE_SIZE];

    std::atomic<uint32_t> midi_write;
    std::atomic<uint32_t> midi_read;
    midi_event_t midi_queue[MIDI_QUEUE_SIZE];
} grain_instance_t;

static void set_error(grain_instance_t *inst, const char *msg) {
    if (!inst) return;
    if (!msg) {
        inst->last_error[0] = '\0';
        return;
    }
    snprintf(inst->last_error, sizeof(inst->last_error), "%s", msg);
}

static void free_sample(sample_buffer_t *s) {
    if (!s) return;
    free(s->data);
    free(s);
}

static int queue_retired_sample(grain_instance_t *inst, sample_buffer_t *old_sample) {
    if (!inst || !old_sample) return 1;

    uint32_t w = inst->retire_write.load(std::memory_order_relaxed);
    uint32_t r = inst->retire_read.load(std::memory_order_acquire);
    if (w - r >= RETIRE_QUEUE_SIZE) {
        set_error(inst, "Retire queue full; dropping old sample cleanup");
        return 0;
    }

    inst->retire_queue[w % RETIRE_QUEUE_SIZE] = old_sample;
    inst->retire_write.store(w + 1, std::memory_order_release);
    return 1;
}

static void drain_retired_samples(grain_instance_t *inst) {
    if (!inst) return;

    uint32_t r = inst->retire_read.load(std::memory_order_relaxed);
    uint32_t w = inst->retire_write.load(std::memory_order_acquire);

    while (r < w) {
        sample_buffer_t *s = inst->retire_queue[r % RETIRE_QUEUE_SIZE];
        inst->retire_queue[r % RETIRE_QUEUE_SIZE] = NULL;
        free_sample(s);
        r++;
    }

    inst->retire_read.store(r, std::memory_order_release);
}

static int push_midi_event(grain_instance_t *inst, const uint8_t *msg, int len, int source) {
    if (!inst || !msg || len < 1) return 0;

    uint32_t w = inst->midi_write.load(std::memory_order_relaxed);
    uint32_t r = inst->midi_read.load(std::memory_order_acquire);
    if (w - r >= MIDI_QUEUE_SIZE) {
        return 0;
    }

    midi_event_t *ev = &inst->midi_queue[w % MIDI_QUEUE_SIZE];
    ev->len = (len >= 3) ? 3 : len;
    ev->source = source;
    ev->bytes[0] = msg[0];
    ev->bytes[1] = (ev->len > 1) ? msg[1] : 0;
    ev->bytes[2] = (ev->len > 2) ? msg[2] : 0;

    inst->midi_write.store(w + 1, std::memory_order_release);
    return 1;
}

static int pop_midi_event(grain_instance_t *inst, midi_event_t *out) {
    uint32_t r = inst->midi_read.load(std::memory_order_relaxed);
    uint32_t w = inst->midi_write.load(std::memory_order_acquire);
    if (r >= w) return 0;

    *out = inst->midi_queue[r % MIDI_QUEUE_SIZE];
    inst->midi_read.store(r + 1, std::memory_order_release);
    return 1;
}

static uint16_t rd_u16_le(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd_u32_le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int32_t rd_i24_le(const uint8_t *p) {
    int32_t v = ((int32_t)p[0]) | ((int32_t)p[1] << 8) | ((int32_t)p[2] << 16);
    if (v & 0x00800000) v |= ~0x00FFFFFF;
    return v;
}

static int load_wav_mono(const char *path, sample_buffer_t **out_sample, char *err, int err_len) {
    *out_sample = NULL;

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        snprintf(err, err_len, "Could not open sample file: %s", path);
        return -1;
    }

    uint8_t riff[12];
    if (fread(riff, 1, 12, fp) != 12) {
        fclose(fp);
        snprintf(err, err_len, "Invalid WAV header");
        return -1;
    }

    if (memcmp(riff, "RIFF", 4) != 0 || memcmp(riff + 8, "WAVE", 4) != 0) {
        fclose(fp);
        snprintf(err, err_len, "Not a RIFF/WAVE file");
        return -1;
    }

    int have_fmt = 0;
    int have_data = 0;
    uint16_t audio_format = 0;
    uint16_t channels = 0;
    uint32_t sample_rate = 0;
    uint16_t block_align = 0;
    uint16_t bits_per_sample = 0;
    uint32_t data_size = 0;
    long data_offset = 0;

    while (!feof(fp)) {
        uint8_t chdr[8];
        if (fread(chdr, 1, 8, fp) != 8) break;

        uint32_t chunk_size = rd_u32_le(chdr + 4);
        long chunk_data_pos = ftell(fp);

        if (memcmp(chdr, "fmt ", 4) == 0) {
            uint8_t fmt[40];
            uint32_t want = (chunk_size < sizeof(fmt)) ? chunk_size : (uint32_t)sizeof(fmt);
            if (fread(fmt, 1, want, fp) != want || want < 16) {
                fclose(fp);
                snprintf(err, err_len, "Corrupt fmt chunk");
                return -1;
            }
            audio_format = rd_u16_le(fmt + 0);
            channels = rd_u16_le(fmt + 2);
            sample_rate = rd_u32_le(fmt + 4);
            block_align = rd_u16_le(fmt + 12);
            bits_per_sample = rd_u16_le(fmt + 14);
            have_fmt = 1;
        } else if (memcmp(chdr, "data", 4) == 0) {
            data_size = chunk_size;
            data_offset = chunk_data_pos;
            have_data = 1;
        }

        long next = chunk_data_pos + (long)chunk_size + (chunk_size & 1u);
        if (fseek(fp, next, SEEK_SET) != 0) {
            break;
        }
    }

    if (!have_fmt || !have_data) {
        fclose(fp);
        snprintf(err, err_len, "WAV missing fmt/data chunk");
        return -1;
    }

    if (channels < 1 || block_align < 1 || data_size < (uint32_t)block_align) {
        fclose(fp);
        snprintf(err, err_len, "Invalid WAV channel/layout");
        return -1;
    }

    if (!((audio_format == 1 && (bits_per_sample == 8 || bits_per_sample == 16 || bits_per_sample == 24 || bits_per_sample == 32)) ||
          (audio_format == 3 && bits_per_sample == 32))) {
        fclose(fp);
        snprintf(err, err_len, "Unsupported WAV format (need PCM 8/16/24/32 or float32)");
        return -1;
    }

    if (fseek(fp, data_offset, SEEK_SET) != 0) {
        fclose(fp);
        snprintf(err, err_len, "Failed to seek WAV data");
        return -1;
    }

    uint8_t *raw = (uint8_t *)malloc(data_size);
    if (!raw) {
        fclose(fp);
        snprintf(err, err_len, "Out of memory reading WAV");
        return -1;
    }

    if (fread(raw, 1, data_size, fp) != data_size) {
        free(raw);
        fclose(fp);
        snprintf(err, err_len, "Failed to read WAV data");
        return -1;
    }
    fclose(fp);

    int frame_count = (int)(data_size / block_align);
    if (frame_count <= 0) {
        free(raw);
        snprintf(err, err_len, "Empty WAV data");
        return -1;
    }

    sample_buffer_t *sample = (sample_buffer_t *)calloc(1, sizeof(sample_buffer_t));
    if (!sample) {
        free(raw);
        snprintf(err, err_len, "Out of memory allocating sample buffer");
        return -1;
    }

    sample->data = (float *)malloc((size_t)frame_count * sizeof(float));
    if (!sample->data) {
        free(raw);
        free(sample);
        snprintf(err, err_len, "Out of memory allocating sample data");
        return -1;
    }

    int bytes_per_sample = bits_per_sample / 8;
    for (int i = 0; i < frame_count; i++) {
        float mono = 0.0f;
        const uint8_t *frame_ptr = raw + (size_t)i * block_align;

        for (int ch = 0; ch < channels; ch++) {
            const uint8_t *sp = frame_ptr + ch * bytes_per_sample;
            float v = 0.0f;

            if (audio_format == 1) {
                if (bits_per_sample == 8) {
                    int x = (int)sp[0] - 128;
                    v = (float)x / 128.0f;
                } else if (bits_per_sample == 16) {
                    int16_t x = (int16_t)rd_u16_le(sp);
                    v = (float)x / 32768.0f;
                } else if (bits_per_sample == 24) {
                    int32_t x = rd_i24_le(sp);
                    v = (float)x / 8388608.0f;
                } else {
                    int32_t x = (int32_t)rd_u32_le(sp);
                    v = (float)x / 2147483648.0f;
                }
            } else {
                float x;
                memcpy(&x, sp, sizeof(float));
                v = x;
            }

            mono += v;
        }

        mono /= (float)channels;
        sample->data[i] = clampf(mono, -1.0f, 1.0f);
    }

    free(raw);

    sample->length = frame_count;
    sample->sample_rate = (int)sample_rate;

    *out_sample = sample;
    return 0;
}

static int has_wav_extension(const char *name) {
    if (!name) return 0;
    int len = (int)strlen(name);
    if (len < 5) return 0;
    const char *ext = name + len - 4;
    return (ext[0] == '.' &&
            (ext[1] == 'w' || ext[1] == 'W') &&
            (ext[2] == 'a' || ext[2] == 'A') &&
            (ext[3] == 'v' || ext[3] == 'V'));
}

static int discover_wavs(grain_instance_t *inst) {
    if (!inst) return 0;

    inst->wav_count = 0;
    char wav_dir[1024];
    snprintf(wav_dir, sizeof(wav_dir), "%s/wavs", inst->module_dir);

    DIR *dir = opendir(wav_dir);
    if (!dir) {
        set_error(inst, "No wavs directory found in module");
        return 0;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        if (!has_wav_extension(entry->d_name)) continue;
        if (inst->wav_count >= MAX_WAV_FILES) break;

        snprintf(inst->wav_names[inst->wav_count], sizeof(inst->wav_names[0]), "%s", entry->d_name);
        snprintf(inst->wav_rel_paths[inst->wav_count], sizeof(inst->wav_rel_paths[0]), "wavs/%s", entry->d_name);
        inst->wav_count++;
    }
    closedir(dir);

    if (inst->wav_count <= 0) {
        set_error(inst, "No .wav files found in wavs/");
        return 0;
    }

    for (int i = 0; i < inst->wav_count - 1; i++) {
        for (int j = i + 1; j < inst->wav_count; j++) {
            if (strcmp(inst->wav_names[i], inst->wav_names[j]) > 0) {
                char tmp_name[128];
                char tmp_rel[512];
                memcpy(tmp_name, inst->wav_names[i], sizeof(tmp_name));
                memcpy(tmp_rel, inst->wav_rel_paths[i], sizeof(tmp_rel));
                memcpy(inst->wav_names[i], inst->wav_names[j], sizeof(tmp_name));
                memcpy(inst->wav_rel_paths[i], inst->wav_rel_paths[j], sizeof(tmp_rel));
                memcpy(inst->wav_names[j], tmp_name, sizeof(tmp_name));
                memcpy(inst->wav_rel_paths[j], tmp_rel, sizeof(tmp_rel));
            }
        }
    }

    set_error(inst, NULL);
    return inst->wav_count;
}

static int apply_sample_file(grain_instance_t *inst, const char *relative_path, const char *display_name) {
    if (!inst || !relative_path || !relative_path[0]) return -1;

    char resolved[1024];
    snprintf(resolved, sizeof(resolved), "%s/%s", inst->module_dir, relative_path);

    sample_buffer_t *loaded = NULL;
    char err[256];
    if (load_wav_mono(resolved, &loaded, err, sizeof(err)) != 0) {
        set_error(inst, err);
        return -1;
    }

    sample_buffer_t *old = inst->active_sample.exchange(loaded, std::memory_order_acq_rel);
    queue_retired_sample(inst, old);

    snprintf(inst->current_sample_rel, sizeof(inst->current_sample_rel), "%s", relative_path);
    snprintf(inst->current_sample_name, sizeof(inst->current_sample_name), "%s", display_name ? display_name : relative_path);
    set_error(inst, NULL);
    return 0;
}

static int apply_sample_index(grain_instance_t *inst, int requested_index) {
    if (!inst || inst->wav_count <= 0) {
        set_error(inst, "No wav files available");
        return -1;
    }

    int idx = clampi(requested_index, 0, inst->wav_count - 1);
    if (apply_sample_file(inst, inst->wav_rel_paths[idx], inst->wav_names[idx]) != 0) {
        return -1;
    }

    inst->sample_index = idx;
    return 0;
}

static void set_numeric_param(grn_params_t *params, const param_meta_t *meta, float value) {
    float v = clampf(value, meta->min_val, meta->max_val);

    if (meta->type == PARAM_FLOAT) {
        *(float *)((char *)params + meta->offset) = v;
        return;
    }

    int iv = (int)lroundf(v);
    iv = clampi(iv, (int)meta->min_val, (int)meta->max_val);

    if (meta->type == PARAM_BOOL) {
        iv = iv ? 1 : 0;
    }

    *(int *)((char *)params + meta->offset) = iv;
}

static float get_numeric_param(const grn_params_t *params, const param_meta_t *meta) {
    if (meta->type == PARAM_FLOAT) {
        return *(const float *)((const char *)params + meta->offset);
    }
    return (float)(*(const int *)((const char *)params + meta->offset));
}

static int json_get_number(const char *json, const char *key, float *out) {
    char needle[96];
    snprintf(needle, sizeof(needle), "\"%s\":", key);
    const char *p = strstr(json, needle);
    if (!p) return -1;
    p += strlen(needle);
    while (*p == ' ' || *p == '\t') p++;
    *out = (float)atof(p);
    return 0;
}

static int json_get_string(const char *json, const char *key, char *out, int out_len) {
    if (!json || !key || !out || out_len <= 1) return -1;

    char needle[96];
    snprintf(needle, sizeof(needle), "\"%s\":", key);
    const char *p = strstr(json, needle);
    if (!p) return -1;
    p += strlen(needle);
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '"') return -1;
    p++;

    int n = 0;
    while (*p && *p != '"' && n < out_len - 1) {
        out[n++] = *p++;
    }
    out[n] = '\0';
    return (*p == '"') ? 0 : -1;
}

static const param_meta_t *find_param(const char *key) {
    for (int i = 0; i < PARAM_COUNT; i++) {
        if (strcmp(key, g_params[i].key) == 0) {
            return &g_params[i];
        }
    }
    return NULL;
}

static void init_default_params(grain_instance_t *inst) {
    inst->params.position = 0.5f;
    inst->params.scan = 0.0f;
    inst->params.size_ms = 60.0f;
    inst->params.density = 18.0f;
    inst->params.spray = 0.15f;
    inst->params.jitter = 0.10f;
    inst->params.freeze = 0;
    inst->params.pitch_semi = 0;
    inst->params.fine_cents = 0.0f;
    inst->params.keytrack = 1.0f;
    inst->params.window_type = 0;
    inst->params.window_shape = 0.35f;
    inst->params.grain_gain = 0.72f;
    inst->params.polyphony = 4;
    inst->params.play_mode = 0;
    inst->params.portamento_ms = 120.0f;
    inst->params.trigger_mode = 0;
    inst->params.scan_end_mode = 0;
    inst->params.spread = 0.2f;
    inst->params.quality = 1;
    inst->sample_index = 0;
    inst->wav_count = 0;
    inst->current_sample_rel[0] = '\0';
    inst->current_sample_name[0] = '\0';
}

static void parse_defaults_json(grain_instance_t *inst, const char *json_defaults) {
    if (!json_defaults || !json_defaults[0]) return;

    for (int i = 0; i < PARAM_COUNT; i++) {
        float value;
        if (json_get_number(json_defaults, g_params[i].key, &value) == 0) {
            set_numeric_param(&inst->params, &g_params[i], value);
            continue;
        }

        char str_value[64];
        if (json_get_string(json_defaults, g_params[i].key, str_value, sizeof(str_value)) == 0) {
            int enum_value = 0;
            if (parse_enum_value(g_params[i].key, str_value, &enum_value)) {
                set_numeric_param(&inst->params, &g_params[i], (float)enum_value);
            }
        }
    }

    float sample_index;
    if (json_get_number(json_defaults, "sample_index", &sample_index) == 0) {
        inst->sample_index = (int)sample_index;
    }
}

static void handle_midi_event(grain_instance_t *inst, const midi_event_t *ev) {
    if (!inst || !ev || ev->len < 1) return;

    uint8_t status = ev->bytes[0] & 0xF0;
    uint8_t data1 = (ev->len > 1) ? ev->bytes[1] : 0;
    uint8_t data2 = (ev->len > 2) ? ev->bytes[2] : 0;

    (void)ev->source;

    if (status == 0x90) {
        if (data2 > 0) {
            grn_engine_note_on(&inst->engine, (int)data1, (float)data2 / 127.0f);
        } else {
            grn_engine_note_off(&inst->engine, (int)data1);
        }
    } else if (status == 0x80) {
        grn_engine_note_off(&inst->engine, (int)data1);
    } else if (status == 0xB0) {
        if (data1 == 123 || data1 == 120) {
            grn_engine_all_notes_off(&inst->engine);
        }
    }
}

static void *v2_create_instance(const char *module_dir, const char *json_defaults) {
    grain_instance_t *inst = (grain_instance_t *)calloc(1, sizeof(grain_instance_t));
    if (!inst) return NULL;

    snprintf(inst->module_dir, sizeof(inst->module_dir), "%s", module_dir ? module_dir : "");

    inst->active_sample.store(NULL, std::memory_order_relaxed);
    inst->retire_write.store(0, std::memory_order_relaxed);
    inst->retire_read.store(0, std::memory_order_relaxed);
    inst->midi_write.store(0, std::memory_order_relaxed);
    inst->midi_read.store(0, std::memory_order_relaxed);
    set_error(inst, NULL);

    grn_engine_init(&inst->engine);
    init_default_params(inst);
    parse_defaults_json(inst, json_defaults);

    grn_engine_set_params(&inst->engine, &inst->params);
    inst->params = inst->engine.params;

    discover_wavs(inst);
    if (inst->wav_count > 0) {
        apply_sample_index(inst, inst->sample_index);
    } else {
        sample_buffer_t *old = inst->active_sample.exchange(NULL, std::memory_order_acq_rel);
        queue_retired_sample(inst, old);
    }

    plugin_log("Granny instance created");
    return inst;
}

static void v2_destroy_instance(void *instance) {
    grain_instance_t *inst = (grain_instance_t *)instance;
    if (!inst) return;

    sample_buffer_t *active = inst->active_sample.exchange(NULL, std::memory_order_acq_rel);
    free_sample(active);
    drain_retired_samples(inst);

    free(inst);
    plugin_log("Granny instance destroyed");
}

static void v2_on_midi(void *instance, const uint8_t *msg, int len, int source) {
    grain_instance_t *inst = (grain_instance_t *)instance;
    if (!inst || !msg || len < 1) return;
    push_midi_event(inst, msg, len, source);
}

static void v2_set_param(void *instance, const char *key, const char *val) {
    grain_instance_t *inst = (grain_instance_t *)instance;
    if (!inst || !key || !val) return;

    if (strcmp(key, "state") == 0) {
        for (int i = 0; i < PARAM_COUNT; i++) {
            float f;
            if (json_get_number(val, g_params[i].key, &f) == 0) {
                set_numeric_param(&inst->params, &g_params[i], f);
                continue;
            }

            char enum_text[64];
            if (json_get_string(val, g_params[i].key, enum_text, sizeof(enum_text)) == 0) {
                int enum_value = 0;
                if (parse_enum_value(g_params[i].key, enum_text, &enum_value)) {
                    set_numeric_param(&inst->params, &g_params[i], (float)enum_value);
                }
            }
        }

        float sample_index;
        if (json_get_number(val, "sample_index", &sample_index) == 0) {
            apply_sample_index(inst, (int)sample_index);
        }

        grn_engine_set_params(&inst->engine, &inst->params);
        inst->params = inst->engine.params;
        return;
    }

    if (strcmp(key, "all_notes_off") == 0) {
        grn_engine_all_notes_off(&inst->engine);
        return;
    }

    if (strcmp(key, "sample_index") == 0) {
        apply_sample_index(inst, atoi(val));
        return;
    }

    const param_meta_t *meta = find_param(key);
    if (meta) {
        float f = 0.0f;
        int enum_value = 0;
        if (parse_enum_value(key, val, &enum_value)) {
            f = (float)enum_value;
        } else {
            f = (float)atof(val);
        }
        set_numeric_param(&inst->params, meta, f);
        grn_engine_set_params(&inst->engine, &inst->params);
        inst->params = inst->engine.params;
        return;
    }
}

static int v2_get_param(void *instance, const char *key, char *buf, int buf_len) {
    grain_instance_t *inst = (grain_instance_t *)instance;
    if (!inst || !key || !buf || buf_len <= 0) return -1;

    if (strcmp(key, "name") == 0) {
        return snprintf(buf, buf_len, "Granny");
    }

    if (strcmp(key, "sample_index") == 0) {
        return snprintf(buf, buf_len, "%d", inst->sample_index);
    }

    if (strcmp(key, "sample_count") == 0) {
        return snprintf(buf, buf_len, "%d", inst->wav_count);
    }

    if (strcmp(key, "sample_name") == 0) {
        return snprintf(buf, buf_len, "%s", inst->current_sample_name);
    }

    if (strcmp(key, "active_grains") == 0) {
        return snprintf(buf, buf_len, "%d", grn_engine_active_grains(&inst->engine));
    }

    if (strcmp(key, "active_voices") == 0) {
        return snprintf(buf, buf_len, "%d", grn_engine_active_voices(&inst->engine));
    }

    if (strcmp(key, "sample_loaded") == 0) {
        sample_buffer_t *s = inst->active_sample.load(std::memory_order_acquire);
        return snprintf(buf, buf_len, "%d", s ? 1 : 0);
    }

    if (strcmp(key, "state") == 0) {
        int offset = 0;
        offset += snprintf(buf + offset, buf_len - offset, "{");

        for (int i = 0; i < PARAM_COUNT; i++) {
            float v = get_numeric_param(&inst->params, &g_params[i]);
            if (i > 0) offset += snprintf(buf + offset, buf_len - offset, ",");
            const char *enum_name = enum_value_to_string(g_params[i].key, (int)lroundf(v));
            if (enum_name) {
                offset += snprintf(buf + offset, buf_len - offset,
                                   "\"%s\":\"%s\"", g_params[i].key, enum_name);
            } else if (g_params[i].type == PARAM_FLOAT) {
                offset += snprintf(buf + offset, buf_len - offset,
                                   "\"%s\":%.4f", g_params[i].key, v);
            } else {
                offset += snprintf(buf + offset, buf_len - offset,
                                   "\"%s\":%d", g_params[i].key, (int)lroundf(v));
            }
        }

        offset += snprintf(buf + offset, buf_len - offset,
                           ",\"sample_index\":%d}", inst->sample_index);
        return offset;
    }

    if (strcmp(key, "chain_params") == 0) {
        int offset = 0;
        offset += snprintf(buf + offset, buf_len - offset, "[");

        for (int i = 0; i < PARAM_COUNT; i++) {
            if (i > 0) offset += snprintf(buf + offset, buf_len - offset, ",");
            if (strcmp(g_params[i].key, "size_ms") == 0) {
                offset += snprintf(buf + offset, buf_len - offset,
                                   "{\"key\":\"%s\",\"name\":\"%s\",\"type\":\"%s\",\"min\":%g,\"max\":%g,\"step\":0.5}",
                                   g_params[i].key,
                                   g_params[i].name,
                                   g_params[i].type == PARAM_FLOAT ? "float" : "int",
                                   g_params[i].min_val,
                                   g_params[i].max_val);
            } else if (strcmp(g_params[i].key, "scan") == 0) {
                offset += snprintf(buf + offset, buf_len - offset,
                                   "{\"key\":\"%s\",\"name\":\"%s\",\"type\":\"%s\",\"min\":%g,\"max\":%g,\"step\":0.001}",
                                   g_params[i].key,
                                   g_params[i].name,
                                   g_params[i].type == PARAM_FLOAT ? "float" : "int",
                                   g_params[i].min_val,
                                   g_params[i].max_val);
            } else if (strcmp(g_params[i].key, "density") == 0) {
                offset += snprintf(buf + offset, buf_len - offset,
                                   "{\"key\":\"%s\",\"name\":\"%s\",\"type\":\"%s\",\"min\":%g,\"max\":%g,\"step\":0.5}",
                                   g_params[i].key,
                                   g_params[i].name,
                                   g_params[i].type == PARAM_FLOAT ? "float" : "int",
                                   g_params[i].min_val,
                                   g_params[i].max_val);
            } else if (strcmp(g_params[i].key, "freeze") == 0) {
                offset += snprintf(buf + offset, buf_len - offset,
                                   "{\"key\":\"freeze\",\"name\":\"Freeze\",\"type\":\"enum\",\"options\":[\"off\",\"on\"]}");
            } else if (strcmp(g_params[i].key, "portamento_ms") == 0) {
                offset += snprintf(buf + offset, buf_len - offset,
                                   "{\"key\":\"%s\",\"name\":\"%s\",\"type\":\"%s\",\"min\":%g,\"max\":%g,\"step\":1}",
                                   g_params[i].key,
                                   g_params[i].name,
                                   g_params[i].type == PARAM_FLOAT ? "float" : "int",
                                   g_params[i].min_val,
                                   g_params[i].max_val);
            } else if (strcmp(g_params[i].key, "window_type") == 0) {
                offset += snprintf(buf + offset, buf_len - offset,
                                   "{\"key\":\"window_type\",\"name\":\"Window\",\"type\":\"enum\",\"options\":[\"hann\",\"triangle\",\"blackman\"]}");
            } else if (strcmp(g_params[i].key, "quality") == 0) {
                offset += snprintf(buf + offset, buf_len - offset,
                                   "{\"key\":\"quality\",\"name\":\"Quality\",\"type\":\"enum\",\"options\":[\"eco\",\"normal\",\"high\"]}");
            } else if (strcmp(g_params[i].key, "trigger_mode") == 0) {
                offset += snprintf(buf + offset, buf_len - offset,
                                   "{\"key\":\"trigger_mode\",\"name\":\"Trigger\",\"type\":\"enum\",\"options\":[\"per_voice\",\"global_cloud\"]}");
            } else if (strcmp(g_params[i].key, "scan_end_mode") == 0) {
                offset += snprintf(buf + offset, buf_len - offset,
                                   "{\"key\":\"scan_end_mode\",\"name\":\"Scan End\",\"type\":\"enum\",\"options\":[\"wrap\",\"pingpong\",\"clamp\",\"stop\"]}");
            } else if (strcmp(g_params[i].key, "play_mode") == 0) {
                offset += snprintf(buf + offset, buf_len - offset,
                                   "{\"key\":\"play_mode\",\"name\":\"Play Mode\",\"type\":\"enum\",\"options\":[\"mono\",\"portamento\",\"poly\"]}");
            } else {
                offset += snprintf(buf + offset, buf_len - offset,
                                   "{\"key\":\"%s\",\"name\":\"%s\",\"type\":\"%s\",\"min\":%g,\"max\":%g}",
                                   g_params[i].key,
                                   g_params[i].name,
                                   g_params[i].type == PARAM_FLOAT ? "float" : "int",
                                   g_params[i].min_val,
                                   g_params[i].max_val);
            }
        }

        offset += snprintf(buf + offset, buf_len - offset,
                           ",{\"key\":\"sample_index\",\"name\":\"Sample\",\"type\":\"int\",\"min\":0,\"max\":127}");
        offset += snprintf(buf + offset, buf_len - offset,
                           ",{\"key\":\"sample_count\",\"name\":\"SampleCount\",\"type\":\"int\",\"min\":0,\"max\":128}");
        offset += snprintf(buf + offset, buf_len - offset,
                           ",{\"key\":\"sample_name\",\"name\":\"SampleName\",\"type\":\"string\"}");
        offset += snprintf(buf + offset, buf_len - offset,
                           ",{\"key\":\"active_grains\",\"name\":\"ActiveGrains\",\"type\":\"int\",\"min\":0,\"max\":384}");
        offset += snprintf(buf + offset, buf_len - offset,
                           ",{\"key\":\"active_voices\",\"name\":\"ActiveVoices\",\"type\":\"int\",\"min\":0,\"max\":8}");
        offset += snprintf(buf + offset, buf_len - offset, "]");
        return offset;
    }

    if (strcmp(key, "ui_hierarchy") == 0) {
        const char *hier = "{"
            "\"levels\":{"
                "\"root\":{"
                    "\"name\":\"Granny\","
                    "\"knobs\":[\"position\",\"scan\",\"size_ms\",\"density\",\"spray\",\"jitter\",\"pitch_semi\",\"spread\"],"
                    "\"params\":["
                        "\"position\",\"scan\",\"size_ms\",\"density\",\"spray\",\"jitter\",\"freeze\","
                        "\"pitch_semi\",\"fine_cents\",\"keytrack\","
                        "\"window_type\",\"window_shape\",\"grain_gain\","
                        "\"polyphony\",\"play_mode\",\"portamento_ms\",\"trigger_mode\",\"scan_end_mode\",\"spread\",\"quality\",\"sample_index\""
                    "]"
                "}"
            "}"
        "}";
        int n = (int)strlen(hier);
        if (n >= buf_len) return -1;
        memcpy(buf, hier, (size_t)n + 1);
        return n;
    }

    const param_meta_t *meta = find_param(key);
    if (meta) {
        float v = get_numeric_param(&inst->params, meta);
        const char *enum_name = enum_value_to_string(meta->key, (int)lroundf(v));
        if (enum_name) {
            return snprintf(buf, buf_len, "%s", enum_name);
        }
        if (meta->type == PARAM_FLOAT) {
            return snprintf(buf, buf_len, "%.4f", v);
        }
        return snprintf(buf, buf_len, "%d", (int)lroundf(v));
    }

    return -1;
}

static int v2_get_error(void *instance, char *buf, int buf_len) {
    grain_instance_t *inst = (grain_instance_t *)instance;
    if (!inst || !buf || buf_len <= 0) return -1;
    if (!inst->last_error[0]) return 0;
    return snprintf(buf, buf_len, "%s", inst->last_error);
}

static void v2_render_block(void *instance, int16_t *out_interleaved_lr, int frames) {
    grain_instance_t *inst = (grain_instance_t *)instance;
    if (!out_interleaved_lr || frames <= 0) return;

    if (frames > GRN_MAX_RENDER) {
        frames = GRN_MAX_RENDER;
    }

    if (!inst) {
        memset(out_interleaved_lr, 0, (size_t)frames * 2 * sizeof(int16_t));
        return;
    }

    midi_event_t ev;
    while (pop_midi_event(inst, &ev)) {
        handle_midi_event(inst, &ev);
    }

    sample_buffer_t *sample = inst->active_sample.load(std::memory_order_acquire);
    const float *sample_data = sample ? sample->data : NULL;
    int sample_len = sample ? sample->length : 0;
    int sample_rate = sample ? sample->sample_rate : GRN_SAMPLE_RATE;

    float left[GRN_MAX_RENDER];
    float right[GRN_MAX_RENDER];

    grn_engine_render(&inst->engine, sample_data, sample_len, sample_rate, left, right, frames);

    for (int i = 0; i < frames; i++) {
        float l = left[i];
        float r = right[i];

        if (l > 0.95f || l < -0.95f) l = tanhf(l);
        if (r > 0.95f || r < -0.95f) r = tanhf(r);

        int32_t sl = (int32_t)(l * 32767.0f);
        int32_t sr = (int32_t)(r * 32767.0f);
        if (sl > 32767) sl = 32767;
        if (sl < -32768) sl = -32768;
        if (sr > 32767) sr = 32767;
        if (sr < -32768) sr = -32768;

        out_interleaved_lr[i * 2] = (int16_t)sl;
        out_interleaved_lr[i * 2 + 1] = (int16_t)sr;
    }

    drain_retired_samples(inst);
}

static plugin_api_v2_t g_plugin_api_v2;

extern "C" plugin_api_v2_t *move_plugin_init_v2(const host_api_v1_t *host) {
    g_host = host;

    memset(&g_plugin_api_v2, 0, sizeof(g_plugin_api_v2));
    g_plugin_api_v2.api_version = MOVE_PLUGIN_API_VERSION_2;
    g_plugin_api_v2.create_instance = v2_create_instance;
    g_plugin_api_v2.destroy_instance = v2_destroy_instance;
    g_plugin_api_v2.on_midi = v2_on_midi;
    g_plugin_api_v2.set_param = v2_set_param;
    g_plugin_api_v2.get_param = v2_get_param;
    g_plugin_api_v2.get_error = v2_get_error;
    g_plugin_api_v2.render_block = v2_render_block;

    plugin_log("Granny plugin initialized");
    return &g_plugin_api_v2;
}
