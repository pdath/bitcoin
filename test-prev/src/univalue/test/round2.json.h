#include <string_view>

namespace json_tests {
inline constexpr char detail_round2_bytes[] {
'\x5b','\x22','\x61','\xc2','\xa7','\xe2','\x96','\xa0',
'\xf0','\x90','\x8e','\x92','\xf0','\x9d','\x85','\xa1',
'\x22','\x5d','\x0a',
};

inline constexpr std::string_view round2{std::begin(detail_round2_bytes), std::end(detail_round2_bytes)};
}
