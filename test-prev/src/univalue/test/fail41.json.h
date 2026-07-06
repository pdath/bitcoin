#include <string_view>

namespace json_tests {
inline constexpr char detail_fail41_bytes[] {
'\x5b','\x22','\xf0','\x9d','\x85','\x22','\x5d',
};

inline constexpr std::string_view fail41{std::begin(detail_fail41_bytes), std::end(detail_fail41_bytes)};
}
