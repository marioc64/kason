#include <stdint.h>
#include <stdio.h>

#include "kason_schema.h"

typedef struct s_network_config {
    char host[64];
    uint32_t port;
} network_config;

typedef struct s_device_config {
    char name[32];
    network_config network;
    double scale;
} device_config;

static const kason_schema_field network_fields[] = {
    KaSON_FIELD_STRING(network_config, host, "host", KaSON_REQUIRED),
    KaSON_FIELD_U32(network_config, port, "port", KaSON_DEFAULT_U32(1883))
};

KaSON_SCHEMA_DEFINE(network_schema, network_config, network_fields, 4);

static const kason_schema_field device_fields[] = {
    KaSON_FIELD_STRING(device_config, name, "name", KaSON_REQUIRED),
    KaSON_FIELD_STRUCT(device_config, network, "network", &network_schema,
                      KaSON_REQUIRED),
    KaSON_FIELD_DOUBLE(device_config, scale, "scale", KaSON_DEFAULT_DOUBLE(1.0))
};

KaSON_SCHEMA_DEFINE(device_schema, device_config, device_fields, 8);

int main(void)
{
    char input[] =
        "{\"name\":\"sensor-1\",\"network\":{\"host\":\"broker\"}}";
    char output[256];
    device_config config;
    kason_schema_error error;
    kason_writer writer;

    if (kason_schema_init(&network_schema) != KaSON_SCHEMA_SUCCESS ||
            kason_schema_init(&device_schema) != KaSON_SCHEMA_SUCCESS) {
        fputs("invalid schema\n", stderr);
        return 1;
    }
    if (kason_unpack(input, &device_schema, &config, &error) !=
            KaSON_SCHEMA_SUCCESS) {
        fprintf(stderr, "unpack failed: %d\n", error.code);
        return 1;
    }

    printf("%s connects to %s:%u\n", config.name, config.network.host,
           (unsigned)config.network.port);

    if (kason_writer_init_buffer(&writer, output, sizeof(output)) !=
            KaSON_SCHEMA_SUCCESS ||
            kason_pack(&writer, &device_schema, &config,
                      KaSON_PACK_OMIT_DEFAULTS, &error) != KaSON_SCHEMA_SUCCESS) {
        fprintf(stderr, "pack failed: %d\n", error.code);
        return 1;
    }
    puts(output);
    return 0;
}
