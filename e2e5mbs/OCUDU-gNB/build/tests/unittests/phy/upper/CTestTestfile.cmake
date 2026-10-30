# CMake generated Testfile for 
# Source directory: /home/raj/chatgpt/sdr-mbs/ocudu/tests/unittests/phy/upper
# Build directory: /home/raj/chatgpt/sdr-mbs/ocudu/build/tests/unittests/phy/upper
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
include("/home/raj/chatgpt/sdr-mbs/ocudu/build/tests/unittests/phy/upper/downlink_processor_test[1]_include.cmake")
include("/home/raj/chatgpt/sdr-mbs/ocudu/build/tests/unittests/phy/upper/hard_decision_test[1]_include.cmake")
include("/home/raj/chatgpt/sdr-mbs/ocudu/build/tests/unittests/phy/upper/log_likelihood_ratio_test[1]_include.cmake")
include("/home/raj/chatgpt/sdr-mbs/ocudu/build/tests/unittests/phy/upper/rx_buffer_pool_test[1]_include.cmake")
include("/home/raj/chatgpt/sdr-mbs/ocudu/build/tests/unittests/phy/upper/uplink_processor_test[1]_include.cmake")
include("/home/raj/chatgpt/sdr-mbs/ocudu/build/tests/unittests/phy/upper/uplink_request_processor_test[1]_include.cmake")
include("/home/raj/chatgpt/sdr-mbs/ocudu/build/tests/unittests/phy/upper/upper_phy_rx_symbol_handler_test[1]_include.cmake")
add_test(channel_state_information_test "channel_state_information_test")
set_tests_properties(channel_state_information_test PROPERTIES  _BACKTRACE_TRIPLES "/home/raj/chatgpt/sdr-mbs/ocudu/tests/unittests/phy/upper/CMakeLists.txt;20;add_test;/home/raj/chatgpt/sdr-mbs/ocudu/tests/unittests/phy/upper/CMakeLists.txt;0;")
add_test(downlink_processor_pool_test "downlink_processor_pool_test")
set_tests_properties(downlink_processor_pool_test PROPERTIES  _BACKTRACE_TRIPLES "/home/raj/chatgpt/sdr-mbs/ocudu/tests/unittests/phy/upper/CMakeLists.txt;24;add_test;/home/raj/chatgpt/sdr-mbs/ocudu/tests/unittests/phy/upper/CMakeLists.txt;0;")
subdirs("channel_coding")
subdirs("channel_modulation")
subdirs("channel_processors")
subdirs("equalization")
subdirs("sequence_generators")
subdirs("signal_processors")
set_directory_properties(PROPERTIES LABELS "phy")
