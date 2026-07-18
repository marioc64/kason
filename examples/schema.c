#include <stdint.h>
#include <stdio.h>

#include "kason_schema.h"

typedef struct {
    char name[32];
    uint32_t age;
} person;

static const kason_schema_field person_fields[] = {
    KaSON_FIELD_STRING(person, name, "name", KaSON_REQUIRED),
    KaSON_FIELD_U32(person, age, "age", KaSON_DEFAULT_U32(0))
};

KaSON_SCHEMA_DEFINE(person_schema, person, person_fields, 4);

int main(void)
{
    char json[] = "{\"name\":\"Ada\",\"age\":36}";
    char output[128];
    person value;
    kason_schema_error error;
    kason_writer writer;

    if (kason_schema_init(&person_schema) != KaSON_SCHEMA_SUCCESS) {
        fputs("invalid schema\n", stderr);
        return 1;
    }

    if (kason_unpack(json, &person_schema, &value, &error) !=
            KaSON_SCHEMA_SUCCESS) {
        fprintf(stderr, "unpack failed: %d\n", error.code);
        return 1;
    }
    printf("%s is %u\n", value.name, (unsigned)value.age);

    value.age++;
    if (kason_writer_init_buffer(&writer, output, sizeof(output)) !=
            KaSON_SCHEMA_SUCCESS ||
        kason_pack(&writer, &person_schema, &value, 0, &error) !=
            KaSON_SCHEMA_SUCCESS) {
        fprintf(stderr, "pack failed: %d\n", error.code);
        return 1;
    }

    puts(output);
    return 0;
}
