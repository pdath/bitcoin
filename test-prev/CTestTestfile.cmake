# CMake generated Testfile for 
# Source directory: /home/odroid/bitcoin
# Build directory: /home/odroid/bitcoin/test-prev
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[util_test_runner]=] "/usr/bin/cmake" "-E" "env" "BITCOINUTIL=/home/odroid/bitcoin/test-prev/bin/bitcoin-util" "BITCOINTX=/home/odroid/bitcoin/test-prev/bin/bitcoin-tx" "/usr/bin/python3" "/home/odroid/bitcoin/test-prev/test/util/test_runner.py")
set_tests_properties([=[util_test_runner]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/odroid/bitcoin/cmake/tests.cmake;6;add_test;/home/odroid/bitcoin/cmake/tests.cmake;0;;/home/odroid/bitcoin/CMakeLists.txt;772;include;/home/odroid/bitcoin/CMakeLists.txt;0;")
add_test([=[util_rpcauth_test]=] "/usr/bin/python3" "/home/odroid/bitcoin/test-prev/test/util/rpcauth-test.py")
set_tests_properties([=[util_rpcauth_test]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/odroid/bitcoin/cmake/tests.cmake;12;add_test;/home/odroid/bitcoin/cmake/tests.cmake;0;;/home/odroid/bitcoin/CMakeLists.txt;772;include;/home/odroid/bitcoin/CMakeLists.txt;0;")
subdirs("test")
subdirs("doc")
subdirs("src")
