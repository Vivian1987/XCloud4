/* SPDX-License-Identifier: GPL-3.0-only */
#include "net_hint.h"
#include "lang.h"
#include <stddef.h>
#include <stdint.h>

const char *x4_net_hint(int error)
{
    uint32_t code = (uint32_t)error;
    if ((code >= 0x804101DCu && code <= 0x804101EFu) ||
        code == 0x80410140u ||
        code == 0x80410141u) {
        return x4_tr(X4_TR_NET_HINT_DNS);
    }
    if (code == 0x8041013Cu) {
        return x4_tr(X4_TR_NET_HINT_TIMEOUT);
    }
    if (code == 0x8095F00Cu) {
        return x4_tr(X4_TR_NET_HINT_SSL_CERT);
    }
    return NULL;
}
