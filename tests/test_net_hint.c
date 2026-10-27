/* SPDX-License-Identifier: GPL-3.0-only */
#include "../src/ui/net_hint.h"
#include "../src/ui/lang.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    x4_lang_init();

    /* 1. Spanish checks */
    x4_lang_set(X4_LANG_ES, "es-ES", "ES");

    /* DNS range 0x804101DC .. 0x804101EF */
    for (uint32_t code = 0x804101DCu; code <= 0x804101EFu; ++code) {
        const char *hint = x4_net_hint((int)code);
        assert(hint != NULL);
        assert(strcmp(hint, x4_tr(X4_TR_NET_HINT_DNS)) == 0);
        assert(strstr(hint, "DNS") != NULL);
    }

    /* Additional DNS errors: 0x80410140 (EHOSTDOWN), 0x80410141 (EHOSTUNREACH) */
    assert(x4_net_hint((int)0x80410140u) != NULL);
    assert(strcmp(x4_net_hint((int)0x80410140u), x4_tr(X4_TR_NET_HINT_DNS)) == 0);
    assert(x4_net_hint((int)0x80410141u) != NULL);
    assert(strcmp(x4_net_hint((int)0x80410141u), x4_tr(X4_TR_NET_HINT_DNS)) == 0);

    /* Timeout: 0x8041013C (ETIMEDOUT) */
    const char *timeout_es = x4_net_hint((int)0x8041013Cu);
    assert(timeout_es != NULL);
    assert(strcmp(timeout_es, x4_tr(X4_TR_NET_HINT_TIMEOUT)) == 0);
    assert(strstr(timeout_es, "demasiado") != NULL);

    /* SSL unknown CA: 0x8095F00C */
    const char *ssl_es = x4_net_hint((int)0x8095F00Cu);
    assert(ssl_es != NULL);
    assert(strcmp(ssl_es, x4_tr(X4_TR_NET_HINT_SSL_CERT)) == 0);
    assert(strstr(ssl_es, "certificado") != NULL);

    /* Boundary / other codes must return NULL */
    assert(x4_net_hint(0) == NULL);
    assert(x4_net_hint((int)0x804101DBu) == NULL);
    assert(x4_net_hint((int)0x804101F0u) == NULL);
    assert(x4_net_hint((int)0x8041013Bu) == NULL);
    assert(x4_net_hint((int)0x8041013Du) == NULL);
    assert(x4_net_hint(-1) == NULL);
    assert(x4_net_hint((int)2148916233u) == NULL);
    assert(x4_net_hint(404) == NULL);

    /* 2. English checks */
    x4_lang_set(X4_LANG_EN, "en-US", "US");

    for (uint32_t code = 0x804101DCu; code <= 0x804101EFu; ++code) {
        const char *hint = x4_net_hint((int)code);
        assert(hint != NULL);
        assert(strcmp(hint, x4_tr(X4_TR_NET_HINT_DNS)) == 0);
        assert(strstr(hint, "DNS") != NULL);
    }
    assert(strcmp(x4_net_hint((int)0x80410140u), x4_tr(X4_TR_NET_HINT_DNS)) == 0);
    assert(strcmp(x4_net_hint((int)0x80410141u), x4_tr(X4_TR_NET_HINT_DNS)) == 0);

    const char *timeout_en = x4_net_hint((int)0x8041013Cu);
    assert(timeout_en != NULL);
    assert(strcmp(timeout_en, x4_tr(X4_TR_NET_HINT_TIMEOUT)) == 0);
    assert(strstr(timeout_en, "timed out") != NULL);

    /* SSL unknown CA: 0x8095F00C */
    const char *ssl_en = x4_net_hint((int)0x8095F00Cu);
    assert(ssl_en != NULL);
    assert(strcmp(ssl_en, x4_tr(X4_TR_NET_HINT_SSL_CERT)) == 0);
    assert(strstr(ssl_en, "certificate") != NULL);

    assert(x4_net_hint(0) == NULL);
    assert(x4_net_hint((int)0x804101DBu) == NULL);
    assert(x4_net_hint((int)0x804101F0u) == NULL);
    assert(x4_net_hint(-1) == NULL);

    printf("PASS test_net_hint\n");
    return 0;
}
