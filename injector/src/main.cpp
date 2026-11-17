/* main.cpp — mtd_injector CLI (S3-11 최소 주입기)
 *
 *   mtd_injector --pid PID --addr-log addr.jsonl --labels labels.jsonl --scenario T-timer
 *                [--count 100] [--interval-ms 500] [--jitter-ms 200] [--duration-sec 0]
 *                [--seed 1] [--no-freeze] [--stats FILE] [--verbose]
 *
 * labels 파일이 이미 있고 비어 있지 않으면 거부한다(원본 실험 데이터는 덮어쓰지 않음).
 */
#include "injector.hpp"

#include <nlohmann/json.hpp>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>

namespace {

void usage(const char *prog)
{
    std::fprintf(stderr,
                 "usage: %s --pid PID --addr-log FILE --labels FILE --scenario <T|V>-<var>\n"
                 "          [--count N=100] [--interval-ms N=500] [--jitter-ms N=200] [--duration-sec N=0]\n"
                 "          [--seed N=1] [--no-freeze] [--stats FILE] [--verbose]\n",
                 prog);
}

bool nonempty_file(const std::string &p)
{
    struct stat st{};
    return stat(p.c_str(), &st) == 0 && st.st_size > 0;
}

}  // namespace

int main(int argc, char **argv)
{
    injector::Options opt;
    std::string labels_path, stats_path, scenario;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto value = [&]() -> std::string {
            if (i + 1 >= argc) {
                usage(argv[0]);
                std::exit(2);
            }
            return argv[++i];
        };
        /* 숫자 옵션: 잘못된 값이면 예외로 죽지 않고 사용법을 보여 준다 */
        auto number = [&]() -> unsigned long {
            const std::string v = value();
            char *end = nullptr;
            errno = 0;
            unsigned long n = std::strtoul(v.c_str(), &end, 10);
            if (v.empty() || v[0] == '-' || *end != '\0' || errno != 0 || n > 0xffffffffUL) {
                std::fprintf(stderr, "invalid number for %s: %s\n", a.c_str(), v.c_str());
                usage(argv[0]);
                std::exit(2);
            }
            return n;
        };
        if (a == "--pid") opt.pid = static_cast<pid_t>(number());
        else if (a == "--addr-log") opt.addr_log = value();
        else if (a == "--labels") labels_path = value();
        else if (a == "--scenario") scenario = value();
        else if (a == "--count") opt.count = static_cast<unsigned>(number());
        else if (a == "--interval-ms") opt.interval_ms = static_cast<unsigned>(number());
        else if (a == "--jitter-ms") opt.jitter_ms = static_cast<unsigned>(number());
        else if (a == "--duration-sec") opt.duration_sec = static_cast<unsigned>(number());
        else if (a == "--seed") opt.seed = static_cast<std::uint32_t>(number());
        else if (a == "--stats") stats_path = value();
        else if (a == "--no-freeze") opt.freeze = false;
        else if (a == "--verbose") opt.verbose = true;
        else if (a == "--help" || a == "-h") {
            usage(argv[0]);
            return 0;
        } else {
            std::fprintf(stderr, "unknown option: %s\n", a.c_str());
            usage(argv[0]);
            return 2;
        }
    }
    if (opt.pid <= 0 || opt.addr_log.empty() || labels_path.empty() || scenario.empty()) {
        usage(argv[0]);
        return 2;
    }
    std::string err;
    auto sc = injector::parse_scenario(scenario, err);
    if (!sc) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 2;
    }
    opt.scenario = *sc;
    if (nonempty_file(labels_path)) {
        std::fprintf(stderr, "refusing to overwrite existing labels: %s\n", labels_path.c_str());
        return 2;
    }
    std::ofstream labels(labels_path);
    if (!labels) {
        std::fprintf(stderr, "cannot open labels: %s\n", labels_path.c_str());
        return 2;
    }

    injector::Result r;
    if (!injector::run(opt, labels, r, err)) {
        std::fprintf(stderr, "[injector] %s\n", err.c_str());
        return 1;
    }
    std::fprintf(stderr,
                 "[injector] %s labels=%u write_errors=%u freed_writes=%u skipped=%u freeze_timeouts=%u "
                 "order_errors=%llu bad_log_lines=%u target_exited=%d\n",
                 opt.scenario.label.c_str(), r.labels, r.write_errors, r.freed_writes, r.skipped,
                 r.freeze_timeouts, static_cast<unsigned long long>(r.order_errors), r.bad_log_lines,
                 r.target_exited ? 1 : 0);
    if (!stats_path.empty()) {
        nlohmann::ordered_json j;
        j["scenario"] = opt.scenario.label;
        j["labels"] = r.labels;
        j["write_errors"] = r.write_errors;
        j["freed_writes"] = r.freed_writes;
        j["skipped"] = r.skipped;
        j["freeze_timeouts"] = r.freeze_timeouts;
        j["order_errors"] = r.order_errors;
        j["bad_log_lines"] = r.bad_log_lines;
        j["target_exited"] = r.target_exited;
        std::ofstream out(stats_path);
        out << j.dump(2) << '\n';
    }
    return (r.write_errors == 0 && r.freed_writes == 0) ? 0 : 1;
}
