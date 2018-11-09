cmake_minimum_required(VERSION 3.11)

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

project("Vst3HostDemo")

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

set(WX_CONFIG_PATH "./ext/wxWidgets/${BUILD_DIR_NAME}/install/bin/wx-config")
get_filename_component(WX_CONFIG_PATH ${WX_CONFIG_PATH} ABSOLUTE)
message("WX_CONFIG_PATH: ${WX_CONFIG_PATH}")

execute_process(COMMAND ${WX_CONFIG_PATH} "--cxxflags" OUTPUT_VARIABLE WX_CXX_FLAGS)
execute_process(COMMAND ${WX_CONFIG_PATH} "--libs base,core" OUTPUT_VARIABLE WX_LIB_FLAGS)

message("WX_LIB_FLAGS ${WX_LIB_FLAGS}")

include_directories(
  "./Vst3HostDemo"
  "./ext/vst3sdk"
  "./ext/portaudio/include"
  )

set(CMAKE_CXX_FLAGS "-DHAVE_TYPE_TRAITS=1 ${WX_CXX_FLAGS} ${CMAKE_CXX_FLAGS} -std=c++17 -stdlib=libc++")
set(CMAKE_EXE_LINKER_FLAGS "${WX_LIB_FLAGS} ${CMAKE_EXE_LINKER_FLAGS}")

set(SOURCE_DIR_LIST "./Vst3HostDemo")
set(TARGET_EXTENSION_LIST "*.c" "*.cpp" "*.h" "*.hpp")
set(BASE_DIR "./Vst3HostDemo")
get_filename_component(BASE_DIR "${BASE_DIR}" ABSOLUTE)

if(IsXcode)
  list(APPEND TARGET_EXTENSION_LIST ${TARGET_EXTENSION_LIST} "*.mm")
endif()

# プロジェクトに含めるソースファイル／ヘッダーファイルを再帰的に検索
foreach(DIR ${SOURCE_DIR_LIST})
  foreach(EXT ${TARGET_EXTENSION_LIST})
    list(APPEND PATTERNS ${PATTERNS} "${DIR}/${EXT}")
  endforeach()

  file(GLOB_RECURSE FILES ${PATTERNS})

  # Define SourceGroup reflecting filesystem hierarchy.
  foreach(FILE_PATH ${FILES})
    get_filename_component(FILE_DIR "${FILE_PATH}" DIRECTORY)
    file(RELATIVE_PATH GROUP_NAME "${BASE_DIR}" "${FILE_DIR}")
    string(REPLACE "/" "\\" GROUP_NAME "${GROUP_NAME}")
    source_group("${GROUP_NAME}" FILES "${FILE_PATH}")
  endforeach()

  list(APPEND SOURCE_FILES ${FILES})
endforeach()

message("Files ${SOURCE_FILES}")

add_executable(${PROJECT_NAME} ${SOURCE_FILES})

set_target_properties(${PROJECT_NAME} PROPERTIES XCODE_ATTRIBUTE_GCC_PREFIX_HEADER "Vst3HostDemo/prefix.hpp")
set_target_properties(${PROJECT_NAME} PROPERTIES XCODE_ATTRIBUTE_GCC_PRECOMPILE_PREFIX_HEADER "YES")


find_library(VST3_SDK_LIB "sdk" PATHS "./ext/vst3sdk/${BUILD_DIR_NAME}/lib/${CAPITAL_CONFIG_NAME}")
find_library(VST3_BASE_LIB "base" PATHS "./ext/vst3sdk/${BUILD_DIR_NAME}/lib/${CAPITAL_CONFIG_NAME}")
find_library(PORTAUDIO_LIB "libportaudio.a" PATHS "./ext/portaudio/${BUILD_DIR_NAME}")
link_directories("./ext/wxWidgets/${BUILD_DIR_NAME}/install/lib")

target_link_libraries(
  ${PROJECT_NAME}
  ${PORTAUDIO_LIB}
  ${VST3_SDK_LIB}
  ${VST3_BASE_LIB}
  "-framework CoreServices"
  "-framework CoreFoundation"
  "-framework AudioUnit"
  "-framework AudioToolbox"
  "-framework CoreAudio"
  "wx_baseu-3.1"
  "wx_osx_cocoau_core-3.1"
  )
