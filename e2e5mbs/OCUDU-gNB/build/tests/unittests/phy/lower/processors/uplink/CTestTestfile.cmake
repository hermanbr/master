# CMake generated Testfile for 
# Source directory: /home/raj/chatgpt/sdr-mbs/ocudu/tests/unittests/phy/lower/processors/uplink
# Build directory: /home/raj/chatgpt/sdr-mbs/ocudu/build/tests/unittests/phy/lower/processors/uplink
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(lower_phy_uplink_processor_test "lower_phy_uplink_processor_test")
set_tests_properties(lower_phy_uplink_processor_test PROPERTIES  _BACKTRACE_TRIPLES "/home/raj/chatgpt/sdr-mbs/ocudu/tests/unittests/phy/lower/processors/uplink/CMakeLists.txt;11;add_test;/home/raj/chatgpt/sdr-mbs/ocudu/tests/unittests/phy/lower/processors/uplink/CMakeLists.txt;0;")
subdirs("prach")
subdirs("puxch")
set_directory_properties(PROPERTIES LABELS "phy")
