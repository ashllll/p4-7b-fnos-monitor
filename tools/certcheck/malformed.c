/* Run the extracted firmware parser on exact-size heap strings under ASan. */
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
bool pem_fingerprint(const char *pem, char *out, size_t out_cap);
int main(void)
{
    const char *cases[] = {
        "", "-", "-----BEGIN CERTIFICATE-----",
        "-----BEGIN CERTIFICATE-----\nAAAA",
        "-----END CERTIFICATE-----\n-----BEGIN CERTIFICATE-----",
        "-----BEGIN CERTIFICATE-----\n-\n-----END CERTIFICATE-----",
    };
    char out[128];
    assert(!pem_fingerprint(NULL, out, sizeof out));
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        char *pem = malloc(strlen(cases[i]) + 1);
        assert(pem);
        strcpy(pem, cases[i]);
        assert(!pem_fingerprint(pem, out, sizeof out));
        free(pem);
    }
    puts("PASS: malformed and truncated PEM rejected without out-of-bounds reads");
    return 0;
}
