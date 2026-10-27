/* SPDX-License-Identifier: GPL-3.0-only */
#include "../src/auth/ca_bundle.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    printf("Testing CA bundle integrity...\n");

    size_t count = x4_ca_bundle_count();
    printf("Total bundled CA certificates: %zu\n", count);
    assert(count == X4_CA_BUNDLE_COUNT);
    assert(count == 11u);

    for (size_t i = 0; i < count; ++i) {
        const X4CaCertEntry *e = x4_ca_bundle_entry(i);
        assert(e != NULL);
        assert(e->name != NULL && strlen(e->name) > 0);
        assert(e->subject != NULL && strlen(e->subject) > 0);
        assert(strstr(e->subject, "CN=") != NULL);
        assert(e->sha256_fingerprint != NULL);
        assert(strlen(e->sha256_fingerprint) == 95); /* 32 bytes hex + 31 colons */

        /* PEM validation */
        assert(e->pem_data != NULL);
        assert(e->pem_size > 0);
        assert(strstr(e->pem_data, "-----BEGIN CERTIFICATE-----") != NULL);
        assert(strstr(e->pem_data, "-----END CERTIFICATE-----") != NULL);
        assert(strlen(e->pem_data) == e->pem_size);

        /* DER validation */
        assert(e->der_data != NULL);
        assert(e->der_size > 0);
        assert(e->der_data[0] == 0x30); /* ASN.1 SEQUENCE header */

        printf("  [%zu] %-45s (DER %4zu B, PEM %4zu B): OK\n",
               i, e->name, e->der_size, e->pem_size);
    }

    /* Out of bounds checks */
    assert(x4_ca_bundle_entry(count) == NULL);
    assert(x4_ca_bundle_entry(count + 10) == NULL);

    /* OpenOrbis SslPem descriptor array checks */
    size_t ptr_count = 0;
    const SslPem * const *ptrs = x4_ca_bundle_get_ssl_pem_ptrs(&ptr_count);
    assert(ptrs != NULL);
    assert(ptr_count == count);
    for (size_t i = 0; i < ptr_count; ++i) {
        assert(ptrs[i] != NULL);
        assert(ptrs[i]->ptr != NULL);
        assert(ptrs[i]->size > 0);
        assert(ptrs[i]->size == x4_ca_bundle_entry(i)->pem_size);
    }

    size_t rsa_count = x4_ca_bundle_rsa_count();
    assert(rsa_count == 7u);
    size_t rsa_ptr_count = 0;
    const SslPem * const *rsa_ptrs = x4_ca_bundle_get_ssl_pem_ptrs_rsa(&rsa_ptr_count);
    assert(rsa_ptrs != NULL);
    assert(rsa_ptr_count == 7u);
    for (size_t i = 0; i < rsa_ptr_count; ++i) {
        assert(rsa_ptrs[i] != NULL);
        assert(rsa_ptrs[i]->ptr != NULL);
        assert(rsa_ptrs[i]->size > 0);
    }

    printf("PASS test_ca_bundle: all %zu root certificates valid and verified.\n", count);
    return 0;
}
