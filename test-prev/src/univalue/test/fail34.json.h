#include <string_view>

namespace json_tests {
inline constexpr char detail_fail34_bytes[] {
'\x7b','\x7d','\x20','\x67','\x61','\x72','\x62','\x61',
'\x67','\x65',
};

inline constexpr std::string_view fail34{std::begin(detail_fail34_bytes), std::end(detail_fail34_bytes)};
}
