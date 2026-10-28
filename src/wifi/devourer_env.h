#pragma once

// Pass devourer's RX tuning knobs through to the driver.
//
// devourer (the user-space RTL8812AU driver) reads no environment variables
// itself; only its example programs translate DEVOURER_* variables into a
// DeviceConfig. Upstream Aviateur creates the device with a default config, so
// those variables were silently ignored. This helper reads the RX-related ones
// so that queue depth etc. can be A/B tested without rebuilding.
//
//   DEVOURER_RX_URBS        bulk-IN URB queue depth (driver default 8)
//   DEVOURER_RX_URB_BYTES   per-URB buffer size (default 16384)
//   DEVOURER_RX_MODE        async | sync | reorder-pool | spsc-fat | decoupled
//   DEVOURER_RX_POOL_SPARE  extra buffers for the pool modes
//   DEVOURER_RX_RING_MS     period of the rx.ring USB telemetry event (0 = off)
//
// Unset variables keep the driver defaults, so behaviour is unchanged unless
// a variable is set.

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>

#include "DeviceConfig.h"

namespace lowlat {

inline bool EnvLong(const char *name, long *out) {
    const char *e = std::getenv(name);
    if (!e || !*e) {
        return false;
    }
    *out = std::strtol(e, nullptr, 0);
    return true;
}

inline bool StrIEq(const char *a, const char *b) {
    for (; *a && *b; ++a, ++b) {
        if (std::tolower(static_cast<unsigned char>(*a)) != std::tolower(static_cast<unsigned char>(*b))) {
            return false;
        }
    }
    return *a == *b;
}

inline const char *RxModeName(devourer::RxMode m) {
    switch (m) {
        case devourer::RxMode::Async:
            return "async";
        case devourer::RxMode::Sync:
            return "sync";
        case devourer::RxMode::ReorderPool:
            return "reorder-pool";
        case devourer::RxMode::SpscFat:
            return "spsc-fat";
        case devourer::RxMode::Decoupled:
            return "decoupled";
    }
    return "?";
}

// Returns the config and a one-line human readable summary.
inline devourer::DeviceConfig DevourerRxConfigFromEnv(std::string &summary) {
    devourer::DeviceConfig cfg{};
    long v = 0;

    if (EnvLong("DEVOURER_RX_URBS", &v) && v >= 1) {
        cfg.rx.urbs = static_cast<int>(v);
    }
    if (EnvLong("DEVOURER_RX_URB_BYTES", &v) && v >= 4096) {
        cfg.rx.urb_bytes = static_cast<int>(v);
    }
    if (const char *e = std::getenv("DEVOURER_RX_MODE")) {
        if (StrIEq(e, "sync")) {
            cfg.rx.rx_mode = devourer::RxMode::Sync;
        } else if (StrIEq(e, "reorder-pool") || StrIEq(e, "reorder_pool") || StrIEq(e, "pool")) {
            cfg.rx.rx_mode = devourer::RxMode::ReorderPool;
        } else if (StrIEq(e, "spsc-fat") || StrIEq(e, "spsc_fat") || StrIEq(e, "spsc")) {
            cfg.rx.rx_mode = devourer::RxMode::SpscFat;
        } else if (StrIEq(e, "decoupled")) {
            cfg.rx.rx_mode = devourer::RxMode::Decoupled;
        } else {
            cfg.rx.rx_mode = devourer::RxMode::Async;
        }
    }
    if (EnvLong("DEVOURER_RX_POOL_SPARE", &v) && v >= 0) {
        cfg.rx.pool_spare = static_cast<int>(v);
    }
    if (EnvLong("DEVOURER_RX_RING_MS", &v) && v > 0) {
        cfg.rx.ring_ms = static_cast<int>(v);
    }

    summary = "[DRIVER] devourer RX config: urbs=" +
              (cfg.rx.urbs ? std::to_string(*cfg.rx.urbs) : std::string("default(8)")) +
              " urb_bytes=" +
              (cfg.rx.urb_bytes ? std::to_string(*cfg.rx.urb_bytes) : std::string("default(16384)")) +
              " mode=" + RxModeName(cfg.rx.rx_mode) + " pool_spare=" + std::to_string(cfg.rx.pool_spare) +
              " ring_ms=" + (cfg.rx.ring_ms ? std::to_string(*cfg.rx.ring_ms) : std::string("off"));
    return cfg;
}

} // namespace lowlat
