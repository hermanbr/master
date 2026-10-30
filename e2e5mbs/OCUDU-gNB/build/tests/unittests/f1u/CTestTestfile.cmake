# CMake generated Testfile for 
# Source directory: /home/raj/chatgpt/sdr-mbs/ocudu/tests/unittests/f1u
# Build directory: /home/raj/chatgpt/sdr-mbs/ocudu/build/tests/unittests/f1u
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
include("/home/raj/chatgpt/sdr-mbs/ocudu/build/tests/unittests/f1u/f1u_local_connector_test[1]_include.cmake")
include("/home/raj/chatgpt/sdr-mbs/ocudu/build/tests/unittests/f1u/f1u_cu_up_split_connector_test[1]_include.cmake")
include("/home/raj/chatgpt/sdr-mbs/ocudu/build/tests/unittests/f1u/f1u_du_split_connector_test[1]_include.cmake")
include("/home/raj/chatgpt/sdr-mbs/ocudu/build/tests/unittests/f1u/f1u_session_manager_test[1]_include.cmake")
include("/home/raj/chatgpt/sdr-mbs/ocudu/build/tests/unittests/f1u/f1u_mbs_router_test[1]_include.cmake")
subdirs("cu_up")
subdirs("du")
