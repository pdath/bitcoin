#include <string_view>

namespace json_tests {
inline constexpr char detail_fail22_bytes[] {
'\x5b','\x22','\x43','\x6f','\x6c','\x6f','\x6e','\x20',
'\x69','\x6e','\x73','\x74','\x65','\x61','\x64','\x20',
'\x6f','\x66','\x20','\x63','\x6f','\x6d','\x6d','\x61',
'\x22','\x3a','\x20','\x66','\x61','\x6c','\x73','\x65',
'\x5d',
};

inline constexpr std::string_view fail22{std::begin(detail_fail22_bytes), std::end(detail_fail22_bytes)};
}
