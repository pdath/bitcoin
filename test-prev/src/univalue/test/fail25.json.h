#include <string_view>

namespace json_tests {
inline constexpr char detail_fail25_bytes[] {
'\x5b','\x22','\x09','\x74','\x61','\x62','\x09','\x63',
'\x68','\x61','\x72','\x61','\x63','\x74','\x65','\x72',
'\x09','\x69','\x6e','\x09','\x73','\x74','\x72','\x69',
'\x6e','\x67','\x09','\x22','\x5d',
};

inline constexpr std::string_view fail25{std::begin(detail_fail25_bytes), std::end(detail_fail25_bytes)};
}
