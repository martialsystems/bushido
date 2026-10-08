cmake_minimum_required(VERSION 3.22)
project(JidaiCommon VERSION 1.1.2 LANGUAGES CXX)

# Header-only C++17: the Jidai Cable Standard v1.1 helpers and the shared DSP blocks.
# Other repos vendor include/jidai (copy, subtree or FetchContent) and add include/ to their include path,
# or add_subdirectory() this folder and link jidai::common.
add_library(jidai_common INTERFACE)
add_library(jidai::common ALIAS jidai_common)
target_include_directories(jidai_common INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(jidai_common INTERFACE cxx_std_17)

option(JIDAI_COMMON_TESTS "Build the jidai-common unit tests" ${PROJECT_IS_TOP_LEVEL})
if(JIDAI_COMMON_TESTS)
  enable_testing()
  add_executable(JidaiCommonTests tests/CommonTests.cpp)
  target_link_libraries(JidaiCommonTests PRIVATE jidai_common)
  # Built as C++17 on purpose: proves the headers vendor into C++17 projects.
  set_target_properties(JidaiCommonTests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(JidaiCommonTests PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion -Werror)
  endif()
  add_test(NAME common COMMAND JidaiCommonTests)
endif()

if(JIDAI_COMMON_TESTS)
  add_executable(JidaiDspTests tests/DspTests.cpp)
  target_link_libraries(JidaiDspTests PRIVATE jidai_common)
  set_target_properties(JidaiDspTests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(JidaiDspTests PRIVATE -O2 -Wall -Wextra -Wpedantic -Wshadow -Werror)
  endif()
  add_test(NAME dsp COMMAND JidaiDspTests)
endif()

if(JIDAI_COMMON_TESTS)
  # Header hygiene: every header with -Wfloat-equal and -Wconversion (the strictest set a vendoring unit uses).
  add_executable(JidaiHeaderHygiene tests/HeaderHygiene.cpp)
  target_link_libraries(JidaiHeaderHygiene PRIVATE jidai_common)
  set_target_properties(JidaiHeaderHygiene PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(JidaiHeaderHygiene PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion -Wfloat-equal -Wdouble-promotion -Werror)
  endif()
  add_test(NAME headers COMMAND JidaiHeaderHygiene)
endif()
