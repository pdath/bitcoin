# CMake generated Testfile for 
# Source directory: /home/odroid/bitcoin/src/secp256k1/src
# Build directory: /home/odroid/bitcoin/test-prev/src/secp256k1/src
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(secp256k1_noverify_tests "/home/odroid/bitcoin/test-prev/src/secp256k1/bin/noverify_tests")
set_tests_properties(secp256k1_noverify_tests PROPERTIES  _BACKTRACE_TRIPLES "/home/odroid/bitcoin/src/secp256k1/src/CMakeLists.txt;90;add_test;/home/odroid/bitcoin/src/secp256k1/src/CMakeLists.txt;0;")
add_test(secp256k1_tests "/home/odroid/bitcoin/test-prev/src/secp256k1/bin/tests")
set_tests_properties(secp256k1_tests PROPERTIES  _BACKTRACE_TRIPLES "/home/odroid/bitcoin/src/secp256k1/src/CMakeLists.txt;95;add_test;/home/odroid/bitcoin/src/secp256k1/src/CMakeLists.txt;0;")
add_test(secp256k1_exhaustive_tests "/home/odroid/bitcoin/test-prev/src/secp256k1/bin/exhaustive_tests")
set_tests_properties(secp256k1_exhaustive_tests PROPERTIES  _BACKTRACE_TRIPLES "/home/odroid/bitcoin/src/secp256k1/src/CMakeLists.txt;104;add_test;/home/odroid/bitcoin/src/secp256k1/src/CMakeLists.txt;0;")
