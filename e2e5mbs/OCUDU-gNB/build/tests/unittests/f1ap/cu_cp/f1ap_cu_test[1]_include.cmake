if(EXISTS "/home/raj/chatgpt/sdr-mbs/ocudu/build/tests/unittests/f1ap/cu_cp/f1ap_cu_test")
  if(NOT EXISTS "/home/raj/chatgpt/sdr-mbs/ocudu/build/tests/unittests/f1ap/cu_cp/f1ap_cu_test[1]_tests.cmake" OR
     NOT "/home/raj/chatgpt/sdr-mbs/ocudu/build/tests/unittests/f1ap/cu_cp/f1ap_cu_test[1]_tests.cmake" IS_NEWER_THAN "/home/raj/chatgpt/sdr-mbs/ocudu/build/tests/unittests/f1ap/cu_cp/f1ap_cu_test" OR
     NOT "/home/raj/chatgpt/sdr-mbs/ocudu/build/tests/unittests/f1ap/cu_cp/f1ap_cu_test[1]_tests.cmake" IS_NEWER_THAN "${CMAKE_CURRENT_LIST_FILE}")
    include("/usr/share/cmake-3.22/Modules/GoogleTestAddTests.cmake")
    gtest_discover_tests_impl(
      TEST_EXECUTABLE [==[/home/raj/chatgpt/sdr-mbs/ocudu/build/tests/unittests/f1ap/cu_cp/f1ap_cu_test]==]
      TEST_EXECUTOR [==[]==]
      TEST_WORKING_DIR [==[/home/raj/chatgpt/sdr-mbs/ocudu/build/tests/unittests/f1ap/cu_cp]==]
      TEST_EXTRA_ARGS [==[]==]
      TEST_PROPERTIES [==[LABELS;f1ap_cu]==]
      TEST_PREFIX [==[]==]
      TEST_SUFFIX [==[]==]
      TEST_FILTER [==[]==]
      NO_PRETTY_TYPES [==[FALSE]==]
      NO_PRETTY_VALUES [==[FALSE]==]
      TEST_LIST [==[f1ap_cu_test_TESTS]==]
      CTEST_FILE [==[/home/raj/chatgpt/sdr-mbs/ocudu/build/tests/unittests/f1ap/cu_cp/f1ap_cu_test[1]_tests.cmake]==]
      TEST_DISCOVERY_TIMEOUT [==[15]==]
      TEST_XML_OUTPUT_DIR [==[]==]
    )
  endif()
  include("/home/raj/chatgpt/sdr-mbs/ocudu/build/tests/unittests/f1ap/cu_cp/f1ap_cu_test[1]_tests.cmake")
else()
  add_test(f1ap_cu_test_NOT_BUILT f1ap_cu_test_NOT_BUILT)
endif()
