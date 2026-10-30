# SPDX-License-Identifier: LicenseRef-CSSL-1.0

find_package(yaml-cpp)
if (NOT yaml-cpp_FOUND)
  CPMAddPackage("gh:jbeder/yaml-cpp#yaml-cpp-0.7.0@0.7.0")
else()
  find_library(YAML_CPP_SHARED_LIBRARY NAMES yaml-cpp)
  if (YAML_CPP_SHARED_LIBRARY MATCHES "\\.so")
    add_library(oai_yaml_cpp_shared SHARED IMPORTED)
    set_target_properties(oai_yaml_cpp_shared PROPERTIES
      IMPORTED_LOCATION "${YAML_CPP_SHARED_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "${YAML_CPP_INCLUDE_DIR}")
    set(OAI_YAML_CPP_TARGET oai_yaml_cpp_shared)
  elseif (NOT TARGET yaml-cpp::yaml-cpp)
    add_library(yaml-cpp::yaml-cpp ALIAS yaml-cpp)
  endif()
endif()
if (NOT OAI_YAML_CPP_TARGET)
  set(OAI_YAML_CPP_TARGET yaml-cpp::yaml-cpp)
endif()

add_library(params_yaml_static config_yaml.cpp)
target_link_libraries(params_yaml_static PUBLIC ${OAI_YAML_CPP_TARGET})
if (ENABLE_TESTS)
  add_subdirectory(tests)
endif()

add_library(params_yaml MODULE config_yaml.cpp)
target_link_libraries(params_yaml PUBLIC ${OAI_YAML_CPP_TARGET})
set_target_properties(params_yaml PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
