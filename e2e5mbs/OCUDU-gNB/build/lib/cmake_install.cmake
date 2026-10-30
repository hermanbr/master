# Install script for directory: /home/dwijesek/chatgpt/sdr-mbs-june-10/sdr-mbs/ocudu/lib

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/usr/local")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Release")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Install shared libraries without execute permission?
if(NOT DEFINED CMAKE_INSTALL_SO_NO_EXE)
  set(CMAKE_INSTALL_SO_NO_EXE "1")
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

# Set default install directory permissions.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "/usr/bin/objdump")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/dwijesek/chatgpt/sdr-mbs-june-10/sdr-mbs/ocudu/build/lib/asn1/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/dwijesek/chatgpt/sdr-mbs-june-10/sdr-mbs/ocudu/build/lib/cu_cp/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/dwijesek/chatgpt/sdr-mbs-june-10/sdr-mbs/ocudu/build/lib/cu_up/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/dwijesek/chatgpt/sdr-mbs-june-10/sdr-mbs/ocudu/build/lib/du/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/dwijesek/chatgpt/sdr-mbs-june-10/sdr-mbs/ocudu/build/lib/e1ap/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/dwijesek/chatgpt/sdr-mbs-june-10/sdr-mbs/ocudu/build/lib/e2/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/dwijesek/chatgpt/sdr-mbs-june-10/sdr-mbs/ocudu/build/lib/f1ap/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/dwijesek/chatgpt/sdr-mbs-june-10/sdr-mbs/ocudu/build/lib/f1u/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/dwijesek/chatgpt/sdr-mbs-june-10/sdr-mbs/ocudu/build/lib/fapi_adaptor/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/dwijesek/chatgpt/sdr-mbs-june-10/sdr-mbs/ocudu/build/lib/gateways/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/dwijesek/chatgpt/sdr-mbs-june-10/sdr-mbs/ocudu/build/lib/gtpu/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/dwijesek/chatgpt/sdr-mbs-june-10/sdr-mbs/ocudu/build/lib/instrumentation/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/dwijesek/chatgpt/sdr-mbs-june-10/sdr-mbs/ocudu/build/lib/mac/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/dwijesek/chatgpt/sdr-mbs-june-10/sdr-mbs/ocudu/build/lib/ngap/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/dwijesek/chatgpt/sdr-mbs-june-10/sdr-mbs/ocudu/build/lib/xnap/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/dwijesek/chatgpt/sdr-mbs-june-10/sdr-mbs/ocudu/build/lib/nrppa/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/dwijesek/chatgpt/sdr-mbs-june-10/sdr-mbs/ocudu/build/lib/nru/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/dwijesek/chatgpt/sdr-mbs-june-10/sdr-mbs/ocudu/build/lib/ofh/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/dwijesek/chatgpt/sdr-mbs-june-10/sdr-mbs/ocudu/build/lib/pcap/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/dwijesek/chatgpt/sdr-mbs-june-10/sdr-mbs/ocudu/build/lib/pdcp/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/dwijesek/chatgpt/sdr-mbs-june-10/sdr-mbs/ocudu/build/lib/phy/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/dwijesek/chatgpt/sdr-mbs-june-10/sdr-mbs/ocudu/build/lib/psup/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/dwijesek/chatgpt/sdr-mbs-june-10/sdr-mbs/ocudu/build/lib/radio/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/dwijesek/chatgpt/sdr-mbs-june-10/sdr-mbs/ocudu/build/lib/ran/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/dwijesek/chatgpt/sdr-mbs-june-10/sdr-mbs/ocudu/build/lib/rlc/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/dwijesek/chatgpt/sdr-mbs-june-10/sdr-mbs/ocudu/build/lib/rohc/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/dwijesek/chatgpt/sdr-mbs-june-10/sdr-mbs/ocudu/build/lib/rrc/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/dwijesek/chatgpt/sdr-mbs-june-10/sdr-mbs/ocudu/build/lib/ru/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/dwijesek/chatgpt/sdr-mbs-june-10/sdr-mbs/ocudu/build/lib/scheduler/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/dwijesek/chatgpt/sdr-mbs-june-10/sdr-mbs/ocudu/build/lib/sdap/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/dwijesek/chatgpt/sdr-mbs-june-10/sdr-mbs/ocudu/build/lib/security/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/dwijesek/chatgpt/sdr-mbs-june-10/sdr-mbs/ocudu/build/lib/ocudulog/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/dwijesek/chatgpt/sdr-mbs-june-10/sdr-mbs/ocudu/build/lib/ocuduvec/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/dwijesek/chatgpt/sdr-mbs-june-10/sdr-mbs/ocudu/build/lib/support/cmake_install.cmake")
endif()

