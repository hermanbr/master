# CMake generated Testfile for 
# Source directory: /home/raj/chatgpt/sdr-mbs/ocudu/tests/unittests/phy/support
# Build directory: /home/raj/chatgpt/sdr-mbs/ocudu/build/tests/unittests/phy/support
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
include("/home/raj/chatgpt/sdr-mbs/ocudu/build/tests/unittests/phy/support/resource_grid_mapper_test[1]_include.cmake")
include("/home/raj/chatgpt/sdr-mbs/ocudu/build/tests/unittests/phy/support/precoding_configuration_test[1]_include.cmake")
add_test(resource_grid_pool_test "resource_grid_pool_test")
set_tests_properties(resource_grid_pool_test PROPERTIES  _BACKTRACE_TRIPLES "/home/raj/chatgpt/sdr-mbs/ocudu/tests/unittests/phy/support/CMakeLists.txt;10;add_test;/home/raj/chatgpt/sdr-mbs/ocudu/tests/unittests/phy/support/CMakeLists.txt;0;")
add_test(resource_grid_test "resource_grid_test")
set_tests_properties(resource_grid_test PROPERTIES  _BACKTRACE_TRIPLES "/home/raj/chatgpt/sdr-mbs/ocudu/tests/unittests/phy/support/CMakeLists.txt;14;add_test;/home/raj/chatgpt/sdr-mbs/ocudu/tests/unittests/phy/support/CMakeLists.txt;0;")
add_test(interpolator_test "interpolator_test")
set_tests_properties(interpolator_test PROPERTIES  _BACKTRACE_TRIPLES "/home/raj/chatgpt/sdr-mbs/ocudu/tests/unittests/phy/support/CMakeLists.txt;18;add_test;/home/raj/chatgpt/sdr-mbs/ocudu/tests/unittests/phy/support/CMakeLists.txt;0;")
add_test(re_pattern_test "re_pattern_test")
set_tests_properties(re_pattern_test PROPERTIES  _BACKTRACE_TRIPLES "/home/raj/chatgpt/sdr-mbs/ocudu/tests/unittests/phy/support/CMakeLists.txt;41;add_test;/home/raj/chatgpt/sdr-mbs/ocudu/tests/unittests/phy/support/CMakeLists.txt;0;")
set_directory_properties(PROPERTIES LABELS "phy")
