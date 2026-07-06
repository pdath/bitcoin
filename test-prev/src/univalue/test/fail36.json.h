#include <string_view>

namespace json_tests {
inline constexpr char detail_fail36_bytes[] {
'\x7b','\x22','\x61','\x22','\x3a','\x7d','\x0a',
};

inline constexpr std::string_view fail36{std::begin(detail_fail36_bytes), std::end(detail_fail36_bytes)};
}
