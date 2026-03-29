#include <stdio.h>
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

static int require_contains(const char *haystack, const char *needle, const char *msg) {
    if (!strstr(haystack, needle)) {
        fprintf(stderr, "missing expected text: %s (%s)\n", needle, msg);
        return 1;
    }
    return 0;
}

int main() {
    plugin_api_v2_t *api = move_plugin_init_v2(NULL);
    if (!api || !api->create_instance || !api->get_param || !api->destroy_instance) {
        fprintf(stderr, "plugin api unavailable\n");
        return 2;
    }

    void *inst = api->create_instance(".", "{\"sample_path\":\"\"}");
    if (!inst) {
        fprintf(stderr, "create_instance failed\n");
        return 2;
    }

    char chain_params[16384];
    memset(chain_params, 0, sizeof(chain_params));
    if (api->get_param(inst, "chain_params", chain_params, (int)sizeof(chain_params)) < 0) {
        fprintf(stderr, "get_param(chain_params) failed\n");
        api->destroy_instance(inst);
        return 2;
    }

    int rc = 0;
    rc |= require_contains(
        chain_params,
        "\"key\":\"position\",\"name\":\"Position\",\"type\":\"wav_position\"",
        "position should use wav_position type"
    );
    rc |= require_contains(
        chain_params,
        "\"filepath_param\":\"sample_path\"",
        "position should link waveform preview to sample_path"
    );

    api->destroy_instance(inst);
    return rc ? 1 : 0;
}
