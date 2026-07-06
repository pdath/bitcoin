#include <string_view>

namespace json_tests {
inline constexpr char detail_fail37_bytes[] {
'\x7b','\x22','\x61','\x22','\x3a','\x31','\x20','\x22',
'\x62','\x22','\x3a','\x32','\x7d','\x0a',
};

inline constexpr std::string_view fail37{std::begin(detail_fail37_bytes), std::end(detail_fail37_bytes)};
}
