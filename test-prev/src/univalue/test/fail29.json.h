#include <string_view>

namespace json_tests {
inline constexpr char detail_fail29_bytes[] {
'\x5b','\x30','\x65','\x5d',
};

inline constexpr std::string_view fail29{std::begin(detail_fail29_bytes), std::end(detail_fail29_bytes)};
}
