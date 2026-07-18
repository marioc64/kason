#include <stddef.h>
#include <stdint.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int main(void)
{
    static const uint8_t valid[] = "{\"a\":[1,true,null,\"text\"]}";
    static const uint8_t invalid[] = "[\"\\uD800\",01,]";
    static const uint8_t binary[] = {0x00, 0xff, 0xc0, 0x80, '{', '}', 0x00};

    (void)LLVMFuzzerTestOneInput(NULL, 0);
    (void)LLVMFuzzerTestOneInput(valid, sizeof(valid) - 1);
    (void)LLVMFuzzerTestOneInput(invalid, sizeof(invalid) - 1);
    (void)LLVMFuzzerTestOneInput(binary, sizeof(binary));
    return 0;
}
