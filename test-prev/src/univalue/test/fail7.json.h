#include <string_view>

namespace json_tests {
inline constexpr char detail_fail7_bytes[] {
'\x5b','\x22','\x43','\x6f','\x6d','\x6d','\x61','\x20',
'\x61','\x66','\x74','\x65','\x72','\x20','\x74','\x68',
'\x65','\x20','\x63','\x6c','\x6f','\x73','\x65','\x22',
'\x5d','\x2c',
};

inline constexpr std::string_view fail7{std::begin(detail_fail7_bytes), std::end(detail_fail7_bytes)};
}
