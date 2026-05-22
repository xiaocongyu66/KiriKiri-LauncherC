//
// Minimal spdlog-compatible shim for kirikiroid2-web motionplayer port.
// Replaces spdlog with TVPAddLog so the upstream sources compile unchanged.
//
// Upstream uses two patterns only:
//   #define LOGGER spdlog::get("plugin")
//   LOGGER->info/warn/error/debug/critical("msg {}, {}", arg1, arg2);
//   spdlog::get("plugin")->warn("...", args...);
//
// We map every level to TVPAddLog with a [motionplayer:<level>] prefix and
// use a tiny header-only fmt::format substitute (forwards to std::stringstream
// with positional "{}" placeholder substitution) to keep call sites untouched.
//
#pragma once
#ifndef KRKR2_SPDLOG_COMPAT_H_
#define KRKR2_SPDLOG_COMPAT_H_

#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include "tjsCommHead.h"
#include "MsgIntf.h"  // TVPAddLog(ttstr)

namespace krkr2_spdlog_compat {

inline void appendArg(std::ostringstream &os, const char *s) {
    if (s) os << s; else os << "(null)";
}
inline void appendArg(std::ostringstream &os, const std::string &s) { os << s; }
inline void appendArg(std::ostringstream &os, const std::string_view &s) { os << s; }
inline void appendArg(std::ostringstream &os, const ttstr &t) {
    os << t.AsNarrowStdString();
}
inline void appendArg(std::ostringstream &os, const void *p) { os << p; }
template <typename T>
inline void appendArg(std::ostringstream &os, T &&value) {
    os << std::forward<T>(value);
}

inline void formatRecursive(std::ostringstream &os, const char *&fmt) {
    while (*fmt) {
        if (fmt[0] == '{' && fmt[1] == '}') {
            fmt += 2;
            return;
        }
        os << *fmt++;
    }
}

template <typename First, typename... Rest>
inline void formatRecursive(std::ostringstream &os, const char *&fmt,
                            First &&first, Rest &&...rest) {
    formatRecursive(os, fmt);
    appendArg(os, std::forward<First>(first));
    formatRecursive(os, fmt, std::forward<Rest>(rest)...);
}

template <typename... Args>
inline std::string format(const char *fmt, Args &&...args) {
    std::ostringstream os;
    if constexpr (sizeof...(Args) == 0) {
        return std::string(fmt ? fmt : "");
    } else {
        formatRecursive(os, fmt, std::forward<Args>(args)...);
        // tail of the format string
        while (*fmt) os << *fmt++;
        return os.str();
    }
}

inline void emit(const char *level, const std::string &text) {
    ttstr line(TJS_W("[motionplayer:"));
    line += ttstr(level);
    line += TJS_W("] ");
    line += ttstr(text.c_str());
    TVPAddLog(line);
}

struct Logger {
    template <typename... Args>
    void trace(const char *fmt, Args &&...args) const {
        emit("trace", format(fmt, std::forward<Args>(args)...));
    }
    template <typename... Args>
    void debug(const char *fmt, Args &&...args) const {
        emit("debug", format(fmt, std::forward<Args>(args)...));
    }
    template <typename... Args>
    void info(const char *fmt, Args &&...args) const {
        emit("info", format(fmt, std::forward<Args>(args)...));
    }
    template <typename... Args>
    void warn(const char *fmt, Args &&...args) const {
        emit("warn", format(fmt, std::forward<Args>(args)...));
    }
    template <typename... Args>
    void error(const char *fmt, Args &&...args) const {
        emit("error", format(fmt, std::forward<Args>(args)...));
    }
    template <typename... Args>
    void critical(const char *fmt, Args &&...args) const {
        emit("critical", format(fmt, std::forward<Args>(args)...));
    }
};

inline std::shared_ptr<Logger> sharedLogger() {
    static std::shared_ptr<Logger> g = std::make_shared<Logger>();
    return g;
}

} // namespace krkr2_spdlog_compat

// Drop-in spdlog namespace
namespace spdlog {
using krkr2_spdlog_compat::Logger;
inline std::shared_ptr<Logger> get(const char * /*name*/) {
    return krkr2_spdlog_compat::sharedLogger();
}
inline std::shared_ptr<Logger> get(const std::string & /*name*/) {
    return krkr2_spdlog_compat::sharedLogger();
}
} // namespace spdlog

// Minimal fmt-compatible facade. Upstream uses fmt::format_string<Args...> as
// a compile-time-validated format string type and fmt::format(...) to build a
// std::string. We expose them as forwarding aliases on top of the format()
// helper above so the upstream code compiles without the real libfmt.
namespace fmt {

template <typename... Args>
struct format_string {
    const char *value;
    // implicit ctor from a string literal / const char*
    /*implicit*/ format_string(const char *s) : value(s) {}
    /*implicit*/ operator const char *() const { return value; }
};

// Accept either a fmt::format_string or a raw const char* and delegate.
template <typename... Args>
inline std::string format(format_string<Args...> fmt_str, Args &&...args) {
    return krkr2_spdlog_compat::format(static_cast<const char *>(fmt_str),
                                       std::forward<Args>(args)...);
}

template <typename... Args>
inline std::string format(const char *fmt_str, Args &&...args) {
    return krkr2_spdlog_compat::format(fmt_str, std::forward<Args>(args)...);
}

template <typename... Args>
inline std::string format(const std::string &fmt_str, Args &&...args) {
    return krkr2_spdlog_compat::format(fmt_str.c_str(),
                                       std::forward<Args>(args)...);
}

} // namespace fmt

#endif // KRKR2_SPDLOG_COMPAT_H_
