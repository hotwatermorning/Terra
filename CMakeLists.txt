cmake_minimum_required(VERSION 3.6)

####################################################################
# general setup
####################################################################

# IsXcodeとIsMSVCを有効にする
message("Generator: ${CMAKE_GENERATOR}")
if(${CMAKE_GENERATOR} STREQUAL "Xcode")
    set(IsXcode "1")
elseif(${CMAKE_GENERATOR} MATCHES "^Visual Studio .*")
    set(IsMSVC "1")
endif()

# GeneratorがVisual Studioの場合、Win64指定されているかどうかでプラットフォーム指定を分岐
# Xcodeでは常にx64を指定する
if(IsMSVC)
    if(${CMAKE_GENERATOR} MATCHES "Win64$")
        set(PLATFORM "x64")
        set(Isx64 "1")
    else()
        set(PLATFORM "x86")
        set(Isx86 "1")
    endif()
elseif(IsXcode)
    set(Isx64 "1")
    set(PLATFORM "x64")
endif()

if(NOT DEFINED CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Debug")
endif()

####################################################################
# configure project
####################################################################

project("Terra")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -Werror=return-type")

string(TOLOWER ${CMAKE_BUILD_TYPE} LOWER_CONFIG_NAME)
string(TOUPPER ${CMAKE_BUILD_TYPE} UPPER_CONFIG_NAME)
string(SUBSTRING ${UPPER_CONFIG_NAME} 0 1 TMP1)
string(SUBSTRING ${LOWER_CONFIG_NAME} 1 -1 TMP2)
string(CONCAT CAPITAL_CONFIG_NAME ${TMP1} ${TMP2})
set(BUILD_DIR_NAME "build_${LOWER_CONFIG_NAME}")

if(${LOWER_CONFIG_NAME} STREQUAL "debug")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_DEBUG")
else()
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_NDEBUG")
endif()

find_program(WX_CONFIG "wx-config" HINTS "./ext/wxWidgets/${BUILD_DIR_NAME}/install/bin" NO_DEFAULT_PATH)

message("WX_CONFIG_PATH: ${WX_CONFIG}")

execute_process(COMMAND ${WX_CONFIG} "--cxxflags" OUTPUT_VARIABLE WX_CXX_FLAGS)
execute_process(COMMAND ${WX_CONFIG} "--libs base,core" OUTPUT_VARIABLE WX_LIB_FLAGS)

message("WX_CXX_FLAGS ${WX_CXX_FLAGS}")
message("WX_LIB_FLAGS ${WX_LIB_FLAGS}")

set(CMAKE_CXX_FLAGS "-DHAVE_TYPE_TRAITS=1 ${WX_CXX_FLAGS} ${CMAKE_CXX_FLAGS} -std=c++17 -Werror=return-type -stdlib=libc++")
set(CMAKE_EXE_LINKER_FLAGS "${WX_LIB_FLAGS} ${CMAKE_EXE_LINKER_FLAGS}")

find_library(PORTAUDIO_LIB "libportaudio.a" PATHS "./ext/portaudio/${BUILD_DIR_NAME}/install/lib" NO_DEFAULT_PATH)
find_library(PROTOBUF_LIB "protobuf" PATHS "./ext/protobuf/${BUILD_DIR_NAME}/install/lib" NO_DEFAULT_PATH)
find_library(FMT_LIB NAMES "fmt" "fmtd" PATHS "./ext/fmt/${BUILD_DIR_NAME}/install/lib" NO_DEFAULT_PATH)
find_library(VST3_SDK_LIB "sdk" PATHS "./ext/vst3sdk/${BUILD_DIR_NAME}/lib/${CAPITAL_CONFIG_NAME}" NO_DEFAULT_PATH)
find_library(VST3_BASE_LIB "base" PATHS "./ext/vst3sdk/${BUILD_DIR_NAME}/lib/${CAPITAL_CONFIG_NAME}" NO_DEFAULT_PATH)
find_library(RTMIDI_LIB "rtmidi_static" PATHS "./ext/rtmidi/${BUILD_DIR_NAME}/install/lib" NO_DEFAULT_PATH)
find_library(JACK_LIB jack)

list(APPEND LINKLIBS
  ${VST3_SDK_LIB}
  ${VST3_BASE_LIB}
  ${PORTAUDIO_LIB}
  ${PROTOBUF_LIB}
  ${FMT_LIB}
  ${RTMIDI_LIB}
  )

if(JACK_LIB)
  list(APPEND LINKLIBS ${JACK_LIB})
endif()

# set source directories
set(SOURCE_DIR_LIST
  "./Terra"
  "./schema/cpp"
  )

# set target extension list
set(TARGET_EXTENSION_LIST "*.c" "*.cc" "*.cpp" "*.h" "*.hpp")

if(IsXcode)
  list(APPEND TARGET_EXTENSION_LIST "*.mm")
endif()

# set the root directory of the source file tree in IDE
set(BASE_DIR "./Terra")
get_filename_component(BASE_DIR "${BASE_DIR}" ABSOLUTE)

foreach(DIR ${SOURCE_DIR_LIST})
  set(PATTERNS "")
  foreach(EXT ${TARGET_EXTENSION_LIST})
    list(APPEND PATTERNS "${DIR}/${EXT}")
  endforeach()

  file(GLOB_RECURSE FILES ${PATTERNS})

  # Define SourceGroup reflecting filesystem hierarchy.
  foreach(FILE_PATH ${FILES})
    get_filename_component(ABS_PATH "${FILE_PATH}" ABSOLUTE)
    get_filename_component(DIR "${ABS_PATH}" DIRECTORY)
    file(RELATIVE_PATH GROUP_NAME "${BASE_DIR}" "${DIR}")
    string(REPLACE "/" "\\" GROUP_NAME "${GROUP_NAME}")
    source_group("${GROUP_NAME}" FILES "${ABS_PATH}")
    list(APPEND SOURCE_FILES "${ABS_PATH}")
  endforeach()
endforeach()

message("Files ${SOURCE_FILES}")

if(IsXcode)
  add_executable(${PROJECT_NAME} MACOSX_BUNDLE ${SOURCE_FILES})
endif()

set(OSX_RESOURCE_DIR "./data")

if(IsXcode)
  get_filename_component(RESOURCE_DIR ${OSX_RESOURCE_DIR} ABSOLUTE)
  file(GLOB_RECURSE RESOURCE_FILES "${RESOURCE_DIR}/*")

  # 階層構造を保持したままResourceディレクトリ以下に配置されるように設定する
  foreach(RESOURCE_FILE ${RESOURCE_FILES})
    get_filename_component(FILENAME ${RESOURCE_FILE} NAME)
    if(${FILENAME} STREQUAL ".DS_Store")
      continue()
    endif()

    get_filename_component(DIR ${RESOURCE_FILE} DIRECTORY)
    string(REGEX REPLACE "^${RESOURCE_DIR}" "" DIR_IN_RESOURCE ${DIR})
    target_sources(${PROJECT_NAME} PRIVATE ${RESOURCE_FILE})
    set_source_files_properties(${RESOURCE_FILE} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources${DIR_IN_RESOURCE}")
    string(REPLACE "/" "\\" GROUP_NAME "Resources${DIR_IN_RESOURCE}")
    source_group(${GROUP_NAME} FILES "${RESOURCE_FILE}")
  endforeach()
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES XCODE_ATTRIBUTE_GCC_PREFIX_HEADER "Terra/prefix.hpp")
set_target_properties(${PROJECT_NAME} PROPERTIES XCODE_ATTRIBUTE_GCC_PRECOMPILE_PREFIX_HEADER "YES")

include_directories(
  "./Terra"
  "./ext/vst3sdk"
  "./ext/variant/include"
  "./schema/cpp"
  "./ext/portaudio/${BUILD_DIR_NAME}/install/include"
  "./ext/protobuf/${BUILD_DIR_NAME}/install/include"
  "./ext/fmt/${BUILD_DIR_NAME}/install/include"
  "./ext/rtmidi/${BUILD_DIR_NAME}/install/include"
  "./ext/midifile/include"
  "./ext/midifile/src-library"
  )

target_link_libraries(
  ${PROJECT_NAME}
  ${LINKLIBS}
  "-framework CoreServices"
  "-framework CoreFoundation"
  "-framework AudioUnit"
  "-framework AudioToolbox"
  "-framework CoreAudio"
  "-framework CoreMidi"
  )

####################################################################
# configure ccache
####################################################################

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
  message("Found ccache: ${CCACHE_PROGRAM}")
  # generate wrapper scripts
  function(generate_launcher_script launcher_path compiler_path file_path)
    string(CONCAT launcher_script_template
      "#!/bin/sh\n"
      "# Xcode generator doesn't include the compiler as the\n"
      "# first argument, Ninja and Makefiles do. Handle both cases.\n"
      "if [[ \"$1\" = \"${compiler_path}\" ]] ; then shift; fi\n"
      "export CCACHE_CPP2=true\n"
      "export CCACHE_SLOPPINESS=pch_defines,time_macros\n"
      "exec \"${launcher_path}\" \"${compiler_path}\" \"$@\"\n"
      )
    file(WRITE "${file_path}" "${launcher_script_template}")
    execute_process(COMMAND chmod a+rx "${file_path}")
  endfunction()

  generate_launcher_script("${CCACHE_PROGRAM}" "${CMAKE_C_COMPILER}" "${CMAKE_BINARY_DIR}/launch-c")
  generate_launcher_script("${CCACHE_PROGRAM}" "${CMAKE_CXX_COMPILER}" "${CMAKE_BINARY_DIR}/launch-cxx")

  if(CMAKE_GENERATOR STREQUAL "Xcode")
    # Set Xcode project attributes to route compilation and linking
    # through our scripts
    set(CMAKE_XCODE_ATTRIBUTE_C "${CMAKE_BINARY_DIR}/launch-c")
    set(CMAKE_XCODE_ATTRIBUTE_CXX "${CMAKE_BINARY_DIR}/launch-cxx")
    set(CMAKE_XCODE_ATTRIBUTE_LD "${CMAKE_BINARY_DIR}/launch-c")
    set(CMAKE_XCODE_ATTRIBUTE_LDPLUSPLUS "${CMAKE_BINARY_DIR}/launch-cxx")
  else()
    # Support Unix Makefiles and Ninja
    set(CMAKE_C_COMPILER_LAUNCHER "${CMAKE_BINARY_DIR}/launch-c")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CMAKE_BINARY_DIR}/launch-cxx")
  endif()
 endif()

