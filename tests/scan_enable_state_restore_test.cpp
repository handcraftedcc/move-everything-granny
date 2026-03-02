#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" {
typedef struct host_api_v1 {
    unsigned int api_version;
    int sample_rate;
    int frames_per_block;
    unsigned char *mapped_memory;
    int audio_out_offset;
    int audio_in_offset;
    void (*log)(const char *msg);
    int (*midi_send_internal)(const unsigned char *msg, int len);
    int (*midi_send_external)(const unsigned char *msg, int len);
} host_api_v1_t;

typedef struct plugin_api_v2 {
    unsigned int api_version;
    void *(*create_instance)(const char *module_dir, const char *json_defaults);
    void (*destroy_instance)(void *instance);
    void (*on_midi)(void *instance, const unsigned char *msg, int len, int source);
    void (*set_param)(void *instance, const char *key, const char *val);
    int (*get_param)(void *instance, const char *key, char *buf, int buf_len);
    int (*get_error)(void *instance, char *buf, int buf_len);
    void (*render_block)(void *instance, short *out_interleaved_lr, int frames);
} plugin_api_v2_t;

plugin_api_v2_t *move_plugin_init_v2(const host_api_v1_t *host);
}

static int expect_param(plugin_api_v2_t *api, void *inst, const char *key, const char *want) {
    char got[64];
    memset(got, 0, sizeof(got));
    if (api->get_param(inst, key, got, (int)sizeof(got)) < 0) {
        fprintf(stderr, "get_param(%s) failed\n", key);
        return 1;
    }
    if (strcmp(got, want) != 0) {
        fprintf(stderr, "expected %s=%s, got %s\n", key, want, got);
        return 1;
    }
    return 0;
}

static int expect_float_approx(plugin_api_v2_t *api, void *inst, const char *key, float want, float eps) {
    char got[64];
    memset(got, 0, sizeof(got));
    if (api->get_param(inst, key, got, (int)sizeof(got)) < 0) {
        fprintf(stderr, "get_param(%s) failed\n", key);
        return 1;
    }
    float gv = (float)atof(got);
    float diff = gv - want;
    if (diff < 0.0f) diff = -diff;
    if (diff > eps) {
        fprintf(stderr, "expected %s~=%g, got %s\n", key, want, got);
        return 1;
    }
    return 0;
}

int main() {
    plugin_api_v2_t *api = move_plugin_init_v2(NULL);
    if (!api || !api->create_instance || !api->set_param || !api->get_param || !api->destroy_instance) {
        fprintf(stderr, "plugin api unavailable\n");
        return 2;
    }

    void *inst = api->create_instance(".", "{\"scan_enable\":\"on\"}");
    if (!inst) {
        fprintf(stderr, "create_instance failed\n");
        return 2;
    }

    int rc = 0;
    rc |= expect_float_approx(api, inst, "position", 0.2f, 0.0002f);
    rc |= expect_float_approx(api, inst, "size_ms", 100.0f, 0.0002f);
    rc |= expect_float_approx(api, inst, "density", 40.0f, 0.0002f);
    rc |= expect_float_approx(api, inst, "spray", 0.05f, 0.0002f);
    rc |= expect_float_approx(api, inst, "jitter", 0.5f, 0.0002f);
    rc |= expect_param(api, inst, "scan_enable", "on");

    api->set_param(inst, "scan_enable", "off");
    rc |= expect_param(api, inst, "scan_enable", "off");

    api->set_param(inst, "state", "{\"scan_enable\":\"on\"}");
    rc |= expect_param(api, inst, "scan_enable", "on");

    api->destroy_instance(inst);
    return rc ? 1 : 0;
}
