#pragma once

// Receiver-side measurement for the low-latency 4K link work.
//
// Collects per-second counters along the whole receive path
//   USB/WiFi frame -> WFB-ng (decrypt + FEC) -> RTP -> decoder -> renderer
// and writes them, with wall-clock timestamps, to logs/link_<start>.csv next
// to the executable. It also detects link loss / restore and measures how long
// it takes from the first packet after a dropout to the first rendered frame.
//
// Everything here is passive: it only counts and logs. Default behaviour of the
// receiver is unchanged.

#include <SDL3/SDL_filesystem.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <functional>
#include <limits>
#include <mutex>
#include <string>
#include <thread>

namespace lowlat {

// ---------------------------------------------------------------- time utils

inline std::tm LocalTm(std::time_t t) {
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    return tm;
}

// "2026-10-28 14:51:03.123"
inline std::string WallClock() {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto ms = duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000;
    const std::tm tm = LocalTm(system_clock::to_time_t(now));
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    char out[40];
    std::snprintf(out, sizeof(out), "%s.%03d", buf, static_cast<int>(ms));
    return out;
}

// "20261028_145103" for file names
inline std::string FileStamp() {
    const std::tm tm = LocalTm(std::time(nullptr));
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &tm);
    return buf;
}

inline int64_t SteadyUs() {
    using namespace std::chrono;
    return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}

// logs/ folder next to the executable (falls back to the working directory).
inline std::filesystem::path LogDir() {
    std::filesystem::path base;
    if (const char *p = SDL_GetBasePath()) {
        base = std::filesystem::path(reinterpret_cast<const char8_t *>(p));
    } else {
        base = std::filesystem::current_path();
    }
    auto dir = base / "logs";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

// UTF-8 text of a path (for log messages).
inline std::string PathUtf8(const std::filesystem::path &p) {
    const auto u8 = p.u8string();
    return std::string(reinterpret_cast<const char *>(u8.data()), u8.size());
}

// fopen that also works for non-ASCII paths on Windows.
inline std::FILE *OpenFile(const std::filesystem::path &p, const char *mode) {
#ifdef _WIN32
    std::wstring wmode(mode, mode + std::strlen(mode));
    return _wfopen(p.c_str(), wmode.c_str());
#else
    return std::fopen(p.c_str(), mode);
#endif
}

// ---------------------------------------------------------------- stats

class LinkStats {
public:
    using Sink = std::function<void(const std::string &)>;

    static LinkStats &I() {
        static LinkStats s;
        return s;
    }

    // ---- counters (per-second deltas; reset by the reporter) ----
    // Radio / USB level
    std::atomic<uint64_t> wifi_frames{0};  // every 802.11 frame delivered by the driver
    std::atomic<uint64_t> wfb_frames{0};   // frames that look like WFB-ng frames
    std::atomic<uint64_t> video_frames{0}; // WFB frames on our video channel (link_id/port 0)
    // WFB-ng aggregator (video channel)
    std::atomic<uint64_t> wfb_all{0};
    std::atomic<uint64_t> wfb_session{0};
    std::atomic<uint64_t> wfb_data{0};
    std::atomic<uint64_t> wfb_dec_err{0};
    std::atomic<uint64_t> wfb_bad{0};
    std::atomic<uint64_t> fec_recovered{0};
    std::atomic<uint64_t> lost_after_fec{0};
    std::atomic<uint64_t> wfb_out_pkts{0};
    std::atomic<uint64_t> wfb_out_bytes{0};
    // RTP (after WFB-ng)
    std::atomic<uint64_t> rtp_pkts{0};
    std::atomic<uint64_t> rtp_gap_events{0};
    std::atomic<uint64_t> rtp_gap_pkts{0};
    // Decoder / renderer
    std::atomic<uint64_t> frames_decoded{0};
    std::atomic<uint64_t> frames_dropped_queue{0}; // decoded but replaced by a newer frame before rendering
    std::atomic<uint64_t> frames_rendered{0};
    std::atomic<uint64_t> read_errors{0};
    std::atomic<uint64_t> send_errors{0};
    std::atomic<uint64_t> reopens{0};

    void AddSignal(int rssi0, int rssi1, int snr0, int snr1) {
        std::lock_guard lk(sig_mtx_);
        rssi_sum_[0] += rssi0;
        rssi_sum_[1] += rssi1;
        snr_sum_[0] += snr0;
        snr_sum_[1] += snr1;
        rssi_min_ = std::min({rssi_min_, rssi0, rssi1});
        sig_n_++;
    }

    // Time a decoded frame waited in the queue before the renderer picked it up.
    void AddQueueWaitUs(int64_t us) {
        std::lock_guard lk(sig_mtx_);
        qwait_sum_us_ += us;
        qwait_max_us_ = std::max(qwait_max_us_, us);
        qwait_n_++;
    }

    // Called for every video-channel WFB frame (USB thread).
    void OnVideoPacket() {
        video_frames++;
        const int64_t now = SteadyUs();
        last_video_us_.store(now);
        if (link_lost_.exchange(false)) {
            restore_us_.store(now);
            waiting_first_frame_.store(true);
            const double gap_ms = (now - lost_since_us_.load()) / 1000.0;
            Emit("[EVENT] link restored: video packets back after " + Fmt(gap_ms) + " ms without packets");
        }
    }

    // Called by the renderer side when a frame is actually handed to the GPU.
    void OnFrameRendered() {
        frames_rendered++;
        if (waiting_first_frame_.exchange(false)) {
            const double ms = (SteadyUs() - restore_us_.load()) / 1000.0;
            Emit("[EVENT] recovery: first rendered frame " + Fmt(ms) + " ms after link restored");
        }
    }

    void Start(Sink sink) {
        Stop();
        {
            std::lock_guard lk(run_mtx_);
            sink_ = std::move(sink);
            running_ = true;
        }
        ResetCounters();
        link_lost_ = false;
        waiting_first_frame_ = false;
        last_video_us_ = 0;

        const auto path = LogDir() / ("link_" + FileStamp() + ".csv");
        csv_ = OpenFile(path, "w");
        if (csv_) {
            std::fprintf(csv_,
                         "time,wifi_frames,wfb_frames,video_frames,wfb_all,wfb_session,wfb_data,wfb_dec_err,wfb_bad,"
                         "fec_recovered,lost_after_fec,wfb_out_pkts,wfb_out_kbps,rtp_pkts,rtp_gap_events,rtp_gap_pkts,"
                         "frames_decoded,frames_dropped_queue,frames_rendered,queue_wait_avg_ms,queue_wait_max_ms,"
                         "rssi0_avg,rssi1_avg,rssi_min,snr0_avg,snr1_avg,read_errors,send_errors,reopens\n");
            std::fflush(csv_);
            Emit("[STATS] writing per-second link statistics to " + PathUtf8(path));
        } else {
            Emit("[STATS] could not open " + PathUtf8(path) + " for writing");
        }

        worker_ = std::thread([this] { Run(); });
    }

    void Stop() {
        {
            std::lock_guard lk(run_mtx_);
            if (!running_) {
                return;
            }
            running_ = false;
        }
        cv_.notify_all();
        if (worker_.joinable()) {
            worker_.join();
        }
        if (csv_) {
            std::fclose(csv_);
            csv_ = nullptr;
        }
    }

    ~LinkStats() {
        // At process exit the GUI singleton the sink logs into may already be gone.
        {
            std::lock_guard lk(run_mtx_);
            sink_ = nullptr;
        }
        Stop();
    }

private:
    LinkStats() = default;

    static std::string Fmt(double v) {
        char b[32];
        std::snprintf(b, sizeof(b), "%.1f", v);
        return b;
    }

    void Emit(const std::string &msg) {
        Sink s;
        {
            std::lock_guard lk(run_mtx_);
            s = sink_;
        }
        if (s) {
            s(msg);
        }
    }

    void ResetCounters() {
        for (auto *c : {&wifi_frames,      &wfb_frames,     &video_frames, &wfb_all,          &wfb_session,
                        &wfb_data,         &wfb_dec_err,    &wfb_bad,      &fec_recovered,    &lost_after_fec,
                        &wfb_out_pkts,     &wfb_out_bytes,  &rtp_pkts,     &rtp_gap_events,   &rtp_gap_pkts,
                        &frames_decoded,   &frames_dropped_queue,          &frames_rendered,  &read_errors,
                        &send_errors,      &reopens}) {
            c->store(0);
        }
        std::lock_guard lk(sig_mtx_);
        ResetSignalLocked();
    }

    void ResetSignalLocked() {
        rssi_sum_[0] = rssi_sum_[1] = 0;
        snr_sum_[0] = snr_sum_[1] = 0;
        rssi_min_ = std::numeric_limits<int>::max();
        sig_n_ = 0;
        qwait_sum_us_ = 0;
        qwait_max_us_ = 0;
        qwait_n_ = 0;
    }

    void Run() {
        // Link-loss detection runs every 50 ms; the CSV row is written every second.
        constexpr int64_t kLossThresholdUs = 300'000;
        int64_t next_row_us = SteadyUs() + 1'000'000;

        std::unique_lock lk(run_mtx_);
        while (running_) {
            cv_.wait_for(lk, std::chrono::milliseconds(50));
            if (!running_) {
                break;
            }
            lk.unlock();

            const int64_t now = SteadyUs();
            const int64_t last = last_video_us_.load();
            if (last != 0 && !link_lost_.load() && now - last > kLossThresholdUs) {
                lost_since_us_.store(last);
                link_lost_.store(true);
                waiting_first_frame_.store(false);
                Emit("[EVENT] link lost: no video packets for " + Fmt((now - last) / 1000.0) + " ms");
            }

            if (now >= next_row_us) {
                next_row_us += 1'000'000;
                if (now > next_row_us) {
                    next_row_us = now + 1'000'000;
                }
                WriteRow();
            }

            lk.lock();
        }
    }

    void WriteRow() {
        const uint64_t wifi = wifi_frames.exchange(0), wfb = wfb_frames.exchange(0), vid = video_frames.exchange(0);
        const uint64_t all = wfb_all.exchange(0), ses = wfb_session.exchange(0), dat = wfb_data.exchange(0);
        const uint64_t dec = wfb_dec_err.exchange(0), bad = wfb_bad.exchange(0);
        const uint64_t fec = fec_recovered.exchange(0), lost = lost_after_fec.exchange(0);
        const uint64_t outp = wfb_out_pkts.exchange(0), outb = wfb_out_bytes.exchange(0);
        const uint64_t rtp = rtp_pkts.exchange(0), gape = rtp_gap_events.exchange(0), gapp = rtp_gap_pkts.exchange(0);
        const uint64_t fdec = frames_decoded.exchange(0), fdrop = frames_dropped_queue.exchange(0);
        const uint64_t frend = frames_rendered.exchange(0);
        const uint64_t rerr = read_errors.exchange(0), serr = send_errors.exchange(0), reop = reopens.exchange(0);

        double r0 = 0, r1 = 0, s0 = 0, s1 = 0, qavg = 0, qmax = 0;
        int rmin = 0;
        {
            std::lock_guard lk(sig_mtx_);
            if (sig_n_ > 0) {
                r0 = double(rssi_sum_[0]) / sig_n_;
                r1 = double(rssi_sum_[1]) / sig_n_;
                s0 = double(snr_sum_[0]) / sig_n_;
                s1 = double(snr_sum_[1]) / sig_n_;
                rmin = rssi_min_;
            }
            if (qwait_n_ > 0) {
                qavg = qwait_sum_us_ / 1000.0 / qwait_n_;
                qmax = qwait_max_us_ / 1000.0;
            }
            ResetSignalLocked();
        }

        const std::string ts = WallClock();
        const double kbps = outb * 8.0 / 1000.0;
        if (csv_) {
            std::fprintf(csv_,
                         "%s,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%.1f,%llu,%llu,%llu,%llu,%llu,%llu,"
                         "%.2f,%.2f,%.1f,%.1f,%d,%.1f,%.1f,%llu,%llu,%llu\n",
                         ts.c_str(),
                         (unsigned long long)wifi, (unsigned long long)wfb, (unsigned long long)vid,
                         (unsigned long long)all, (unsigned long long)ses, (unsigned long long)dat,
                         (unsigned long long)dec, (unsigned long long)bad, (unsigned long long)fec,
                         (unsigned long long)lost, (unsigned long long)outp, kbps, (unsigned long long)rtp,
                         (unsigned long long)gape, (unsigned long long)gapp, (unsigned long long)fdec,
                         (unsigned long long)fdrop, (unsigned long long)frend, qavg, qmax, r0, r1, rmin, s0, s1,
                         (unsigned long long)rerr, (unsigned long long)serr, (unsigned long long)reop);
            std::fflush(csv_);
        }

        // Only log a summary when something is flowing (keeps idle logs short).
        if (wifi || rtp || fdec) {
            char line[320];
            std::snprintf(line, sizeof(line),
                          "[STATS] wifi=%llu video=%llu fec_rec=%llu lost=%llu dec_err=%llu rtp=%llu rtp_gap=%llu "
                          "out=%.0fkbps decoded=%llu rendered=%llu qwait=%.1f/%.1fms rssi=%.0f/%.0f(min %d)",
                          (unsigned long long)wifi, (unsigned long long)vid, (unsigned long long)fec,
                          (unsigned long long)lost, (unsigned long long)dec, (unsigned long long)rtp,
                          (unsigned long long)gapp, kbps, (unsigned long long)fdec, (unsigned long long)frend, qavg,
                          qmax, r0, r1, rmin);
            Emit(line);
        }
    }

    std::mutex run_mtx_;
    std::condition_variable cv_;
    bool running_ = false;
    Sink sink_;
    std::thread worker_;
    std::FILE *csv_ = nullptr;

    std::mutex sig_mtx_;
    int64_t rssi_sum_[2]{};
    int64_t snr_sum_[2]{};
    int rssi_min_ = std::numeric_limits<int>::max();
    int64_t sig_n_ = 0;
    int64_t qwait_sum_us_ = 0;
    int64_t qwait_max_us_ = 0;
    int64_t qwait_n_ = 0;

    std::atomic<int64_t> last_video_us_{0};
    std::atomic<int64_t> lost_since_us_{0};
    std::atomic<int64_t> restore_us_{0};
    std::atomic<bool> link_lost_{false};
    std::atomic<bool> waiting_first_frame_{false};
};

} // namespace lowlat
