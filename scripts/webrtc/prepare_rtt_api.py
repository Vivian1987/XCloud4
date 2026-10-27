#!/usr/bin/env python3
"""Expose PeerConnection::rtt() via C API rtcGetRtt() for pinned libdatachannel.

Idempotent patch for include/rtc/rtc.h and src/capi.cpp.
"""
import pathlib
import subprocess
import sys

PIN_COMMIT = "bdc5ff28e9d3b863144c94a677ecf5bf043aaf15"

def replace(text, old, new, label):
    if new in text:
        return text
    if text.count(old) != 1:
        raise SystemExit(f"Unexpected pinned anchor in {label} ({text.count(old)})")
    return text.replace(old, new, 1)

def apply(base):
    base = pathlib.Path(base).resolve()
    vendor = base / "libdatachannel"

    # Verify pinned commit if git repository metadata exists
    git_dir = vendor / ".git"
    if git_dir.exists():
        try:
            head = subprocess.check_output(
                ["git", "-C", str(vendor), "rev-parse", "HEAD"],
                stderr=subprocess.DEVNULL
            ).decode().strip()
            if head != PIN_COMMIT:
                raise SystemExit(f"libdatachannel commit mismatch: expected {PIN_COMMIT}, got {head}")
        except Exception as e:
            if isinstance(e, SystemExit):
                raise
            pass

    header = vendor / "include/rtc/rtc.h"
    header_text = header.read_text(encoding="utf-8")
    header_anchor = (
        "RTC_C_EXPORT int rtcGetSelectedCandidatePair(int pc, char *local, int localSize, char *remote,\n"
        "                                             int remoteSize);\n"
    )
    header_patch = (
        "RTC_C_EXPORT int rtcGetSelectedCandidatePair(int pc, char *local, int localSize, char *remote,\n"
        "                                             int remoteSize);\n"
        "RTC_C_EXPORT int rtcGetRtt(int pc);\n"
    )
    header_new = replace(header_text, header_anchor, header_patch, "include/rtc/rtc.h")
    if header_new != header_text:
        header.write_text(header_new, encoding="utf-8")

    capi = vendor / "src/capi.cpp"
    capi_text = capi.read_text(encoding="utf-8")
    capi_anchor = (
        "int rtcGetSelectedCandidatePair(int pc, char *local, int localSize, char *remote, int remoteSize) {\n"
    )
    capi_patch = (
        "int rtcGetRtt(int pc) {\n"
        "\treturn wrap([&] {\n"
        "\t\tauto peerConnection = getPeerConnection(pc);\n"
        "\t\tauto rtt = peerConnection->rtt();\n"
        "\t\tif (!rtt)\n"
        "\t\t\treturn RTC_ERR_NOT_AVAIL;\n"
        "\t\treturn static_cast<int>(rtt->count());\n"
        "\t});\n"
        "}\n\n"
        "int rtcGetSelectedCandidatePair(int pc, char *local, int localSize, char *remote, int remoteSize) {\n"
    )
    capi_new = replace(capi_text, capi_anchor, capi_patch, "src/capi.cpp")
    if capi_new != capi_text:
        capi.write_text(capi_new, encoding="utf-8")

    # Verification
    if "RTC_C_EXPORT int rtcGetRtt(int pc);" not in header.read_text(encoding="utf-8"):
        raise SystemExit("Verification failed: rtcGetRtt not declared in include/rtc/rtc.h")
    if "int rtcGetRtt(int pc) {" not in capi.read_text(encoding="utf-8"):
        raise SystemExit("Verification failed: rtcGetRtt not implemented in src/capi.cpp")
    print("libdatachannel rtcGetRtt API patch applied and verified.")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: prepare_rtt_api.py <RTC_ROOT>", file=sys.stderr)
        sys.exit(1)
    apply(sys.argv[1])
