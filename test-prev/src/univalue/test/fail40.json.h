#include <string_view>

namespace json_tests {
inline constexpr char detail_fail40_bytes[] {
'\x5b','\x22','\x9d','\x85','\xa1','\x22','\x5d',
};

inline constexpr std::string_view fail40{std::begin(detail_fail40_bytes), std::end(detail_fail40_bytes)};
}
