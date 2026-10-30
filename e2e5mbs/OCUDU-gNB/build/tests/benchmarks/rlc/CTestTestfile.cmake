# CMake generated Testfile for 
# Source directory: /home/raj/chatgpt/sdr-mbs/ocudu/tests/benchmarks/rlc
# Build directory: /home/raj/chatgpt/sdr-mbs/ocudu/build/tests/benchmarks/rlc
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(rlc_handle_status_report "rlc_handle_status_report")
set_tests_properties(rlc_handle_status_report PROPERTIES  LABELS "rlc" _BACKTRACE_TRIPLES "/home/raj/chatgpt/sdr-mbs/ocudu/tests/benchmarks/rlc/CMakeLists.txt;10;add_test;/home/raj/chatgpt/sdr-mbs/ocudu/tests/benchmarks/rlc/CMakeLists.txt;0;")
add_test(rlc_am_rx_benchmark "rlc_am_rx_benchmark")
set_tests_properties(rlc_am_rx_benchmark PROPERTIES  LABELS "rlc|rtsan" _BACKTRACE_TRIPLES "/home/raj/chatgpt/sdr-mbs/ocudu/tests/benchmarks/rlc/CMakeLists.txt;14;add_test;/home/raj/chatgpt/sdr-mbs/ocudu/tests/benchmarks/rlc/CMakeLists.txt;0;")
add_test(rlc_am_tx_benchmark "rlc_am_tx_benchmark")
set_tests_properties(rlc_am_tx_benchmark PROPERTIES  LABELS "rlc|rtsan" _BACKTRACE_TRIPLES "/home/raj/chatgpt/sdr-mbs/ocudu/tests/benchmarks/rlc/CMakeLists.txt;18;add_test;/home/raj/chatgpt/sdr-mbs/ocudu/tests/benchmarks/rlc/CMakeLists.txt;0;")
