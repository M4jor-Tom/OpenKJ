#ifndef QSTRINGFORMATTER_H
#define QSTRINGFORMATTER_H

#include <QString>
#include <ostream>
#include <spdlog/fmt/fmt.h>

// OpenKJ passes QString straight to spdlog in ~275 places. That worked because
// fmt would implicitly fall back to a type's operator<<(std::ostream&). fmt
// removed that fallback in version 9, so from there on a formatter has to be
// declared explicitly.
//
// ostream_formatter simply reuses the operator<< overloads this codebase
// already declares, so no call site needs to change, and the guard keeps
// fmt 8 (Debian 12's libspdlog-dev) building exactly as before.
#if defined(FMT_VERSION) && FMT_VERSION >= 90000
#include <spdlog/fmt/ostr.h>
template<>
struct fmt::formatter<QString> : fmt::ostream_formatter {
};
#endif

#endif // QSTRINGFORMATTER_H
