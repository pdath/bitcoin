#include <string_view>

namespace json_tests {
inline constexpr char detail_fail17_bytes[] {
'\x5b','\x22','\x49','\x6c','\x6c','\x65','\x67','\x61',
'\x6c','\x20','\x62','\x61','\x63','\x6b','\x73','\x6c',
'\x61','\x73','\x68','\x20','\x65','\x73','\x63','\x61',
'\x70','\x65','\x3a','\x20','\x5c','\x30','\x31','\x37',
'\x22','\x5d',
};

inline constexpr std::string_view fail17{std::begin(detail_fail17_bytes), std::end(detail_fail17_bytes)};
}
