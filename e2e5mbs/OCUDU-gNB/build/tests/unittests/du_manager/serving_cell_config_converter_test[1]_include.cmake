if(EXISTS "/home/raj/chatgpt/sdr-mbs/ocudu/build/tests/unittests/du_manager/serving_cell_config_converter_test")
  if(NOT EXISTS "/home/raj/chatgpt/sdr-mbs/ocudu/build/tests/unittests/du_manager/serving_cell_config_converter_test[1]_tests.cmake" OR
     NOT "/home/raj/chatgpt/sdr-mbs/ocudu/build/tests/unittests/du_manager/serving_cell_config_converter_test[1]_tests.cmake" IS_NEWER_THAN "/home/raj/chatgpt/sdr-mbs/ocudu/build/tests/unittests/du_manager/serving_cell_config_converter_test" OR
     NOT "/home/raj/chatgpt/sdr-mbs/ocudu/build/tests/unittests/du_manager/serving_cell_config_converter_test[1]_tests.cmake" IS_NEWER_THAN "${CMAKE_CURRENT_LIST_FILE}")
    include("/usr/share/cmake-3.22/Modules/GoogleTestAddTests.cmake")
    gtest_discover_tests_impl(
      TEST_EXECUTABLE [==[/home/raj/chatgpt/sdr-mbs/ocudu/build/tests/unittests/du_manager/serving_cell_config_converter_test]==]
      TEST_EXECUTOR [==[]==]
      TEST_WORKING_DIR [==[/home/raj/chatgpt/sdr-mbs/ocudu/build/tests/unittests/du_manager]==]
      TEST_EXTRA_ARGS [==[]==]
      TEST_PROPERTIES [==[]==]
      TEST_PREFIX [==[]==]
      TEST_SUFFIX [==[]==]
      TEST_FILTER [==[]==]
      NO_PRETTY_TYPES [==[FALSE]==]
      NO_PRETTY_VALUES [==[FALSE]==]
      TEST_LIST [==[serving_cell_config_converter_test_TESTS]==]
      CTEST_FILE [==[/home/raj/chatgpt/sdr-mbs/ocudu/build/tests/unittests/du_manager/serving_cell_config_converter_test[1]_tests.cmake]==]
      TEST_DISCOVERY_TIMEOUT [==[15]==]
      TEST_XML_OUTPUT_DIR [==[]==]
    )
  endif()
  include("/home/raj/chatgpt/sdr-mbs/ocudu/build/tests/unittests/du_manager/serving_cell_config_converter_test[1]_tests.cmake")
else()
  add_test(serving_cell_config_converter_test_NOT_BUILT serving_cell_config_converter_test_NOT_BUILT)
endif()
