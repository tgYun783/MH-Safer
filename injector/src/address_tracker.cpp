/* address_tracker.cpp — 주소 로그 추적 (docs/interface.md §3.1) */
#include "address_tracker.hpp"

#include <nlohmann/json.hpp>

#include <cstdlib>

namespace injector {

bool parse_hex_addr(const std::string &s, std::uint64_t &out)
{
    if (s.size() < 3 || s[0] != '0' || (s[1] != 'x' && s[1] != 'X')) return false;
    char *end = nullptr;
    unsigned long long v = std::strtoull(s.c_str() + 2, &end, 16);
    if (end == nullptr || *end != '\0') return false;
    out = static_cast<std::uint64_t>(v);
    return true;
}

std::size_t AddressTracker::feed(const char *data, std::size_t n)
{
    std::size_t bad = 0;
    pending_.append(data, n);
    std::size_t start = 0;
    for (;;) {
        std::size_t nl = pending_.find('\n', start);
        if (nl == std::string::npos) break;
        std::string line = pending_.substr(start, nl - start);
        start = nl + 1;
        if (!line.empty() && !handle_line(line)) ++bad;
    }
    pending_.erase(0, start);
    return bad;
}

const Field *AddressTracker::field(const std::string &name) const
{
    if (!has_current_) return nullptr;
    auto it = current_.fields.find(name);
    return it == current_.fields.end() ? nullptr : &it->second;
}

std::uint64_t AddressTracker::free_ns(std::uint64_t lifetime_id) const
{
    auto it = freed_at_.find(lifetime_id);
    return it == freed_at_.end() ? 0 : it->second;
}

bool AddressTracker::handle_line(const std::string &line)
{
    nlohmann::json j = nlohmann::json::parse(line, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object()) return false;
    if (!j.contains("event") || !j["event"].is_string() || !j.contains("addr") || !j["addr"].is_string() ||
        !j.contains("ns") || !j["ns"].is_number_unsigned())
        return false;
    const std::string event = j["event"].get<std::string>();
    std::uint64_t addr = 0;
    if (!parse_hex_addr(j["addr"].get<std::string>(), addr)) return false;
    const std::uint64_t ns = j["ns"].get<std::uint64_t>();

    auto read_fields = [&](std::map<std::string, Field> &into) {
        if (!j.contains("fields") || !j["fields"].is_array()) return false;
        for (const auto &f : j["fields"]) {
            if (!f.is_object() || !f.contains("name") || !f["name"].is_string() || !f.contains("offset") ||
                !f["offset"].is_number_unsigned() || !f.contains("size") || !f["size"].is_number_unsigned())
                return false;
            Field fd;
            fd.offset = f["offset"].get<std::size_t>();
            fd.size = f["size"].get<std::size_t>();
            into[f["name"].get<std::string>()] = fd;
        }
        return true;
    };

    if (event == "state_alloc") {
        if (has_current_) ++order_errors_;  /* 이전 판 state_free 없이 새 판 — 이전 대상은 버린다 */
        Lifetime lt;
        lt.id = next_id_++;
        lt.addr = addr;
        lt.alloc_ns = ns;
        if (j.contains("size") && j["size"].is_number_unsigned()) lt.size = j["size"].get<std::size_t>();
        if (!read_fields(lt.fields)) return false;
        current_ = std::move(lt);
        has_current_ = true;
        return true;
    }
    if (event == "field_watch") {
        if (!has_current_ || current_.addr != addr) {
            ++order_errors_;
            return true;
        }
        return read_fields(current_.fields);
    }
    if (event == "state_free") {
        ++frees_;
        if (!has_current_ || current_.addr != addr) {
            ++order_errors_;
            return true;
        }
        if (watched_.count(current_.id) != 0) freed_at_[current_.id] = ns;
        has_current_ = false;
        current_ = Lifetime{};
        return true;
    }
    return false;  /* 모르는 이벤트 */
}

}  // namespace injector
