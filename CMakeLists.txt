cmake_minimum_required(VERSION 3.15)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "10.9")
endif()

if(NOT DEFINED ENV{CMAKE_PARALLEL_LEVEL})
  set(ENV{CMAKE_BUILD_PARALLEL_LEVEL} 4)
endif()

include(ExternalProject)

####################################################################
# general setup
####################################################################

message("Generator: ${CMAKE_GENERATOR}")

if(NOT DEFINED CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Debug")
endif()

####################################################################
# configure project
####################################################################

project("Terra")

####################################################################
# configure external projects
####################################################################

# vst3sdk は、 CMAKE_CONFIGURATION_TYPES で Debug と Release しかサポートしていない。
# そのため、 Terra ビルド時に Debug 以外のビルド構成を選択した場合は Release を使うようにする。
set(VST3_CONFIG "$<IF:$<CONFIG:Debug>,Debug,Release>")

# INSTALL_DIR に ${CMAKE_CURRENT_BINARY_DIR}/install/$<CONFIG> のようにして generator expression を使用すると、最初に $<CONFIG> ディレクトリが作られてしまう問題がある。
# そのため、 INSTALL_DIR は使用しないようにする。

set(SUBMODULE_INSTALL_DIR "${CMAKE_CURRENT_BINARY_DIR}/install/$<CONFIG>/opt")

set(ASIOSDK_DOWNLOAD_URL "https://www.steinberg.net/sdk_downloads/asiosdk2.3.zip")
set(ASIOSDK_DIR "${CMAKE_CURRENT_BINARY_DIR}/src/asiosdk")

message("ASIOSDK_DIR: ${ASIOSDK_DIR}")

if(MSVC)
  ExternalProject_Add(
    asiosdk
    PREFIX "${CMAKE_CURRENT_BINARY_DIR}"
    URL ${ASIOSDK_DOWNLOAD_URL}
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ""
    INSTALL_COMMAND cmake -E copy_directory <SOURCE_DIR> "${SUBMODULE_INSTALL_DIR}/asiosdk"
    )
endif()

function(generate_submodule_build_target submodule_name)
  cmake_parse_arguments(GSBT "" "" "BUILD_COMMAND" ${ARGN})

  if(NOT submodule_name)
    message(FATAL_ERROR "SUBMODULE_NAME must be specified.")
  endif()

  if(NOT GSBT_BUILD_COMMAND)
    message(FATAL_ERROR "BUILD_COMMAND must be specified.")
  endif()

  set(GSBT_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ext/${submodule_name}")
  set(GSBT_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/src_/${submodule_name}")
  set(GSBT_INSTALL_DIR "${SUBMODULE_INSTALL_DIR}/${submodule_name}")

  foreach(elem ${GSBT_BUILD_COMMAND})
    string(REPLACE "<SOURCE_DIR>" "${GSBT_SOURCE_DIR}" elem "${elem}")
    string(REPLACE "<BINARY_DIR>" "${GSBT_BINARY_DIR}" elem "${elem}")
    string(REPLACE "<INSTALL_DIR>" "${GSBT_INSTALL_DIR}" elem "${elem}")
    if(MSVC)
      string(REPLACE "^^" "&" elem "${elem}")
    else()
      string(REPLACE "^^" "\;" elem "${elem}")
    endif()

    list(APPEND INNER_BUILD_COMMAND "${elem}")
  endforeach()

  if(MSVC)
    add_custom_target(
      "${submodule_name}"
      COMMAND
        echo "build ${submodule_name}" &
        if exist "${GSBT_INSTALL_DIR}" "("
          echo "${submodule_name} is already installed."
        ")" else "("
          "${CMAKE_COMMAND}" -E make_directory "${GSBT_BINARY_DIR}" &&
          cd "${GSBT_BINARY_DIR}" &&
          ${INNER_BUILD_COMMAND}
        ")"
      )
  else()
    add_custom_target(
      "${submodule_name}"
      COMMAND
        set -ue ";"
        if [ -d "${GSBT_INSTALL_DIR}" ] ";" then
          echo "${submodule_name} is already installed." ";"
          exit 0 ";"
        fi ";"
        "${CMAKE_COMMAND}" -E make_directory "${GSBT_BINARY_DIR}" ";"
        cd "${GSBT_BINARY_DIR}" ";"
        ${INNER_BUILD_COMMAND}
      )
  endif()
endfunction()

generate_submodule_build_target(
  portaudio
  BUILD_COMMAND
    "${CMAKE_COMMAND}"
      -DCMAKE_BUILD_TYPE=$<CONFIG>
      "-DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>"
      -DPA_BUILD_STATIC=ON
      -DPA_BUILD_SHARED=OFF
      $<$<BOOL:${MSVC}>:-DPA_DLL_LINK_WITH_STATIC_RUNTIME=OFF>
      $<$<BOOL:${MSVC}>:-DPA_USE_WDMKS=OFF>
      $<$<BOOL:${MSVC}>:-DPA_USE_ASIO=ON>
      $<$<BOOL:${MSVC}>:-DASIOSDK_ROOT_DIR=${ASIOSDK_DIR}>
      "<SOURCE_DIR>" ^^
    cd "<BINARY_DIR>" ^^
    "${CMAKE_COMMAND}" --build . -j $ENV{CMAKE_BUILD_PARALLEL_LEVEL} --config $<CONFIG> ^^
    "${CMAKE_COMMAND}" --install . --config $<CONFIG>
  )
if(MSVC)
  add_dependencies(portaudio asiosdk)
endif()

generate_submodule_build_target(
  fmt
  BUILD_COMMAND
    "${CMAKE_COMMAND}"
      -DCMAKE_BUILD_TYPE=$<CONFIG>
      "-DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>"
      "<SOURCE_DIR>" ^^
    cd "<BINARY_DIR>" ^^
    "${CMAKE_COMMAND}" --build . -j $ENV{CMAKE_BUILD_PARALLEL_LEVEL} --config $<CONFIG> ^^
    "${CMAKE_COMMAND}" --install . --config $<CONFIG>
  )

generate_submodule_build_target(
  rtmidi
  BUILD_COMMAND
    "${CMAKE_COMMAND}"
      -DCMAKE_BUILD_TYPE=$<CONFIG>
      "-DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>"
      -DRTMIDI_BUILD_STATIC_LIBS=ON
      -DBUILD_SHARED_LIBS=OFF
      -DBUILD_TESTING=OFF
      "<SOURCE_DIR>" ^^
    cd "<BINARY_DIR>" ^^
    "${CMAKE_COMMAND}" --build . -j $ENV{CMAKE_BUILD_PARALLEL_LEVEL} --config $<CONFIG> ^^
    "${CMAKE_COMMAND}" --install . --config $<CONFIG>
  )

# Protocol Buffers
if(MSVC)
  generate_submodule_build_target(
    protobuf
    BUILD_COMMAND
      "${CMAKE_COMMAND}"
        -DCMAKE_BUILD_TYPE=$<CONFIG>
        "-DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>"
        -Dprotobuf_BUILD_TESTS=OFF
        -Dprotobuf_MSVC_STATIC_RUNTIME=OFF
        "<SOURCE_DIR>/cmake" ^^
      cd "<BINARY_DIR>" ^^
      "${CMAKE_COMMAND}" --build . -j $ENV{CMAKE_BUILD_PARALLEL_LEVEL} --config $<CONFIG> ^^
      "${CMAKE_COMMAND}" --install . --config $<CONFIG>
    )
else()

  if("$ENV{CI}" STREQUAL "true")
    list(APPEND AUTOGEN_SH "./autogen.sh")
  else()
    list(APPEND AUTOGEN_SH "test" "./autogen.sh")
  endif()

  generate_submodule_build_target(
    protobuf
    BUILD_COMMAND
      echo "Run autogen.sh" ^^
      cd <SOURCE_DIR> ^^
      ${AUTOGEN_SH} ^^
      echo "autogen.sh succeeded." ^^
      echo "Run configure" ^^
      cd <BINARY_DIR> ^^
      "<SOURCE_DIR>/configure" --disable-shared "--prefix=<INSTALL_DIR>" ^^
      make -j $ENV{CMAKE_BUILD_PARALLEL_LEVEL} ^^
      make install
    )
endif()

generate_submodule_build_target(
  vst3sdk
  BUILD_COMMAND
    "${CMAKE_COMMAND}"
      -DCMAKE_BUILD_TYPE=${VST3_CONFIG}
      -DSMTG_ADD_VST3_PLUGINS_SAMPLES=OFF
      -DSMTG_ADD_VST3_HOSTING_SAMPLES=OFF
      -DSMTG_RUN_VST_VALIDATOR=OFF
      -DSMTG_ADD_VSTGUI=OFF
      "<SOURCE_DIR>" ^^
    cd "<BINARY_DIR>" ^^
    "${CMAKE_COMMAND}" --build . -j $ENV{CMAKE_BUILD_PARALLEL_LEVEL} --config ${VST3_CONFIG} ^^
    cmake -E copy_directory "<BINARY_DIR>/bin/${VST3_CONFIG}" "<INSTALL_DIR>/bin/$<CONFIG>" ^^
    cmake -E copy_directory "<BINARY_DIR>/lib/${VST3_CONFIG}" "<INSTALL_DIR>/lib/$<CONFIG>"
  )

if(MSVC)
  generate_submodule_build_target(
    wxWidgets
    BUILD_COMMAND
      echo "Prepare setup.h" &&
      cd "<SOURCE_DIR>" &&
      "${CMAKE_COMMAND}" -E copy "./include/wx/msw/setup0.h" "./include/wx/msw/setup.h" &&
      echo "Prepare project" &&
      cd "<BINARY_DIR>" &&
      "${CMAKE_COMMAND}"
        -DCMAKE_BUILD_TYPE=$<CONFIG>
        "-DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>"
        -DwxBUILD_SHARED=OFF
        -DwxBUILD_MONOLITHIC=OFF
        -DwxBUILD_USE_STATIC_RUNTIME=OFF
        -DwxBUILD_MSVC_MULTIPROC=ON
        -DwxBUILD_TESTS=OFF
        -DwxBUILD_SAMPLES=OFF
        -DwxBUILD_DEMOS=OFF
        "<SOURCE_DIR>" &&
      cd "<BINARY_DIR>" &&
      "${CMAKE_COMMAND}" --build . -j $ENV{CMAKE_BUILD_PARALLEL_LEVEL} --config $<CONFIG> &&
      "${CMAKE_COMMAND}" --install . --config $<CONFIG>
    )
else()
  generate_submodule_build_target(
    wxWidgets
    BUILD_COMMAND
      echo "Run autogen.sh" &&
      cd <SOURCE_DIR> &&
      test ./autogen.sh &&
      echo "autogen.sh succeeded." &&
      echo "Run configure" &&
      cd <BINARY_DIR> &&
      <SOURCE_DIR>/configure --disable-shared "--prefix=<INSTALL_DIR>"
        "CXXFLAGS=-stdlib=libc++ -DHAVE_TYPE_TRAITS" "OBJCXXFLAGS=-stdlib=libc++"
        "CPPFLAGS=-stdlib=libc++" "LDFLAGS=-stdlib=libc++"
        --with-macosx-version-min=10.9 --with-cocoa
        --with-libjpeg=builtin --with-libpng=builtin
        --with-regex=builtin --with-libtiff=builtin
        --with-zlib=builtin --with-expat=builtin
        --enable-stl --enable-std_containers
        --enable-std_iostreams --enable-std_string
        --enable-unicode --with-cxx=14
        $<$<CONFIG:Debug>:--enable-debug>
        $<$<CONFIG:Debug>:--enable-debug-info>
        $<$<CONFIG:Debug>:--enable-debug-gdb> &&
      make -j $ENV{CMAKE_BUILD_PARALLEL_LEVEL} &&
      make install
    )
endif()

add_custom_target(build-submodules DEPENDS vst3sdk protobuf portaudio rtmidi fmt wxWidgets)

####################################################################
# configure source file lists
####################################################################

# set source directories
set(SOURCE_DIR_LIST "./Terra")

# Test that the target_path should be excluded
# if the path is excluded, IS_EXCLUDED will be set to TRUE
function(TEST_EXCLUDED TARGET_PATH EXCLUSION_PATTERN_LIST IS_EXCLUDED)
  set(IS_EXCLUDED FALSE PARENT_SCOPE)
  foreach(PAT ${EXCLUSION_PATTERN_LIST})
    if(TARGET_PATH MATCHES ${PAT})
      set(IS_EXCLUDED TRUE PARENT_SCOPE)
      break()
    endif()
  endforeach()
endfunction()

# set target extension list
set(TARGET_SOURCE_PATTERNS "*.c" "*.cc" "*.cpp" "*.h" "*.hpp")

if(MSVC)
  list(APPEND TARGET_SOURCE_PATTERNS "*.rc")
elseif(XCODE)
  list(APPEND TARGET_SOURCE_PATTERNS "*.mm")
endif()

# generate_source_list(
#   output
#   BASE_DIR dir
#   SOURCE_DIRS dir1 [dir2...]
#   EXCLUSION_PATTERNS [pattern1...]
#   )
function(generate_source_list output)
  cmake_parse_arguments(GSL "" "BASE_DIR" "SOURCE_DIRS;EXCLUSION_PATTERNS" ${ARGN})

  if(NOT DEFINED GSL_BASE_DIR)
    message(FATAL_ERROR "BASE_DIR must be specified.")
  endif()

  if(NOT DEFINED GSL_SOURCE_DIRS)
    message(FATAL_ERROR "SOURCE_DIRS must be specified.")
  endif()

  message("Print EXCLUSION_PATTERNS: ${GSL_EXCLUSION_PATTERNS}")

  # set the root directory of the source file tree in IDE
  get_filename_component(GSL_BASE_DIR "${GSL_BASE_DIR}" ABSOLUTE)

  foreach(SOURCE_DIR ${GSL_SOURCE_DIRS})
    get_filename_component(SOURCE_DIR "${SOURCE_DIR}" ABSOLUTE)

    set(PATTERNS "")
    foreach(PATTERN ${TARGET_SOURCE_PATTERNS})
      list(APPEND PATTERNS "${SOURCE_DIR}/${PATTERN}")
    endforeach()

    file(GLOB_RECURSE FILES ${PATTERNS})

    # Define SourceGroup reflecting filesystem hierarchy.
    foreach(FILE_PATH ${FILES})
      get_filename_component(FILEPATH "${FILE_PATH}" ABSOLUTE)
      TEST_EXCLUDED(${FILE_PATH} "${GSL_EXCLUSION_PATTERNS}" IS_EXCLUDED)
      if(IS_EXCLUDED)
        continue()
      endif()

      get_filename_component(PARENT_DIR "${FILE_PATH}" DIRECTORY)
      file(RELATIVE_PATH GROUP_NAME "${GSL_BASE_DIR}" "${PARENT_DIR}")
      string(REPLACE "/" "\\" GROUP_NAME "${GROUP_NAME}")
      source_group("${GROUP_NAME}" FILES "${FILE_PATH}")
      list(APPEND SOURCE_FILES "${FILE_PATH}")
    endforeach()
  endforeach()

  message("Files ${SOURCE_FILES}")
  set(${output} "${SOURCE_FILES}" PARENT_SCOPE)
endfunction()

generate_source_list(
  TERRA_APP_SOURCES
  BASE_DIR "./Terra"
  SOURCE_DIRS "./Terra" "./schema/cpp"
  EXCLUSION_PATTERNS ".+/Terra/test/.+")

generate_source_list(
  TERRA_TEST_SOURCES
  BASE_DIR "./Terra"
  SOURCE_DIRS "./Terra" "./schema/cpp"
  )

# スキーマから自動生成されるソースファイルをソースリストに追加する。
set(SCHEMA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/schema")
set(SCHEMA_CPP_DIR "${SCHEMA_DIR}/cpp")

file(GLOB SCHEMA_FILES "${SCHEMA_DIR}/*.proto")
foreach(SCHEMA_FILE ${SCHEMA_FILES})
  get_filename_component(SCHEMA_NAME "${SCHEMA_FILE}" NAME_WE)
  set(SCHEMA_CC "${SCHEMA_CPP_DIR}/${SCHEMA_NAME}.pb.cc")
  set(SCHEMA_H "${SCHEMA_CPP_DIR}/${SCHEMA_NAME}.pb.h")

  set(SCHEMA_GROUP_NAME "schema")
  set(SCHEMA_CPP_GROUP_NAME "schema\\cpp")

  source_group("${SCHEMA_GROUP_NAME}" FILES "${SCHEMA_FILE}")
  source_group("${SCHEMA_CPP_GROUP_NAME}" FILES "${SCHEMA_H}" "${SCHEMA_CC}")

  set_source_files_properties("${SCHEMA_FILE}" PROPERTIES HEADER_FILE_ONLY ON)
  list(APPEND SCHEMA_SOURCES "${SCHEMA_CC}" "${SCHEMA_H}")

  file(WRITE "${SCHEMA_CC}" "#error Need to be auto-generated by compile-schemas target.")
  file(WRITE "${SCHEMA_H}" "#error Need to be auto-generated by compile-schemas target.")
endforeach()

list(APPEND TERRA_APP_SOURCES ${SCHEMA_SOURCES})
list(APPEND TERRA_TEST_SOURCES ${SCHEMA_SOURCES})

# message("schema files: ${SCHEMA_FILES}")
# message("schema sources: ${SCHEMA_SOURCES}")

# スキーマをコンパイルするターゲットを追加する
add_custom_target(
  compile-schemas
  "$<IF:$<BOOL:${MSVC}>,gradlew,./gradlew>" compile_schemas -Pconfig=$<CONFIG>
  SOURCES ${SCHEMA_FILES}
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/gradle"
  VERBATIM
  )

add_dependencies(compile-schemas protobuf)

if(MSVC)
  file(GLOB_RECURSE RESOURCE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/data")
  add_custom_target(
    copy-resources
    gradlew copy_resources -Pconfig=$<CONFIG>
    SOURCES ${RESOURCE_FILES}
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/gradle"
    )
endif()

####################################################################
# configure build targets
####################################################################

if(MSVC)
  add_executable(Terra WIN32 ${TERRA_APP_SOURCES})
  add_executable(Terra-Test ${TERRA_TEST_SOURCES})
else()
  add_executable(Terra MACOSX_BUNDLE ${TERRA_APP_SOURCES})
  add_executable(Terra-Test MACOSX_BUNDLE ${TERRA_TEST_SOURCES})
endif()

list(APPEND TARGET_LIST Terra Terra-Test)

if(MSVC)
  set(VST3_SDK_LIB "${SUBMODULE_INSTALL_DIR}/vst3sdk/lib/${VST3_CONFIG}/sdk.lib")
  set(VST3_BASE_LIB "${SUBMODULE_INSTALL_DIR}/vst3sdk/lib/${VST3_CONFIG}/base.lib")
  set(VST3_PLUGINTERFACES_LIB "${SUBMODULE_INSTALL_DIR}/vst3sdk/lib/${VST3_CONFIG}/pluginterfaces.lib")
  set(PROTOBUF_LIB "${SUBMODULE_INSTALL_DIR}/protobuf/lib/$<IF:$<CONFIG:Debug>,libprotobufd.lib,libprotobuf.lib>")
  set(RTMIDI_LIB "${SUBMODULE_INSTALL_DIR}/rtmidi/lib/rtmidi.lib")
  set(PORTAUDIO_LIB "${SUBMODULE_INSTALL_DIR}/portaudio/lib/portaudio_static_x64.lib")
  set(FMT_LIB "${SUBMODULE_INSTALL_DIR}/fmt/lib/$<IF:$<CONFIG:Debug>,fmtd.lib,fmt.lib>")
else()
  set(VST3_SDK_LIB "${SUBMODULE_INSTALL_DIR}/vst3sdk/lib/${VST3_CONFIG}/libsdk.a")
  set(VST3_BASE_LIB "${SUBMODULE_INSTALL_DIR}/vst3sdk/lib/${VST3_CONFIG}/libbase.a")
  set(VST3_PLUGINTERFACES_LIB "${SUBMODULE_INSTALL_DIR}/vst3sdk/lib/${VST3_CONFIG}/libpluginterfaces.a")
  set(PROTOBUF_LIB "${SUBMODULE_INSTALL_DIR}/protobuf/lib/libprotobuf.a")
  set(RTMIDI_LIB "${SUBMODULE_INSTALL_DIR}/rtmidi/lib/librtmidi.a")
  set(PORTAUDIO_LIB "${SUBMODULE_INSTALL_DIR}/portaudio/lib/libportaudio.a")
  set(FMT_LIB "${SUBMODULE_INSTALL_DIR}/fmt/lib/$<IF:$<CONFIG:Debug>,libfmtd.a,libfmt.a>")
endif()

find_library(JACK_LIB jack)

if(MSVC)
  set(DP "$<$<CONFIG:Debug>:d>")
  list(APPEND WX_LIBS
    wxbase31u${DP}.lib
    wxbase31u${DP}_net.lib
    wxbase31u${DP}_xml.lib
    wxmsw31u${DP}_core.lib
    wxmsw31u${DP}_xrc.lib
    wxmsw31u${DP}_webview.lib
    wxmsw31u${DP}_stc.lib
    wxmsw31u${DP}_richtext.lib
    wxmsw31u${DP}_ribbon.lib
    wxmsw31u${DP}_qa.lib
    wxmsw31u${DP}_propgrid.lib
    wxmsw31u${DP}_media.lib
    wxmsw31u${DP}_html.lib
    wxmsw31u${DP}_gl.lib
    wxmsw31u${DP}_aui.lib
    wxmsw31u${DP}_adv.lib
    wxjpeg${DP}.lib
    wxpng${DP}.lib
    wxtiff${DP}.lib
    wxzlib${DP}.lib
    wxregexu${DP}.lib
    wxexpat${DP}.lib
    wxscintilla${DP}.lib
    )
else()
  list(APPEND WX_LIBS
    libwx_baseu_net-3.1.a
    libwx_baseu_xml-3.1.a
    libwx_baseu-3.1.a
    libwx_osx_cocoau_adv-3.1.a
    libwx_osx_cocoau_aui-3.1.a
    libwx_osx_cocoau_core-3.1.a
    libwx_osx_cocoau_gl-3.1.a
    libwx_osx_cocoau_html-3.1.a
    libwx_osx_cocoau_media-3.1.a
    libwx_osx_cocoau_propgrid-3.1.a
    libwx_osx_cocoau_qa-3.1.a
    libwx_osx_cocoau_ribbon-3.1.a
    libwx_osx_cocoau_richtext-3.1.a
    libwx_osx_cocoau_stc-3.1.a
    libwx_osx_cocoau_webview-3.1.a
    libwx_osx_cocoau_xrc-3.1.a
    libwxexpat-3.1.a
    libwxjpeg-3.1.a
    libwxpng-3.1.a
    libwxtiff-3.1.a
    libwxregexu-3.1.a
    libwxscintilla-3.1.a
    libwxtiff-3.1.a
    libwxzlib-3.1.a
    )
endif()

get_filename_component(PREFIX_HEADER_PATH "./Terra/prefix.hpp" ABSOLUTE)

foreach(TARGET Terra;Terra-Test)
  message("Target: ${TARGET}")

  add_dependencies(${TARGET} build-submodules compile-schemas)

  if(MSVC)
    add_dependencies(${TARGET} copy-resources)
  endif()

  target_include_directories(
    ${TARGET}
    PRIVATE
    "${SUBMODULE_INSTALL_DIR}/portaudio/include"
    "${SUBMODULE_INSTALL_DIR}/rtmidi/include"
    "${SUBMODULE_INSTALL_DIR}/fmt/include"
    "${SUBMODULE_INSTALL_DIR}/protobuf/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/ext/vst3sdk"
    "${CMAKE_CURRENT_SOURCE_DIR}/ext/variant/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/ext/midifile/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/ext/midifile/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/ext/Catch2/single_include"
    "${CMAKE_CURRENT_SOURCE_DIR}/schema/cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Terra"
    )

  if(MSVC)
    target_include_directories(
      ${TARGET}
      PRIVATE
      "${SUBMODULE_INSTALL_DIR}/wxWidgets/include"
      "${SUBMODULE_INSTALL_DIR}/wxWidgets/include/msvc"
      )
  else()
    target_include_directories(
      ${TARGET}
      PRIVATE
      "${SUBMODULE_INSTALL_DIR}/wxWidgets/include/wx-3.1"
      "${SUBMODULE_INSTALL_DIR}/wxWidgets/lib/wx/include/osx_cocoa-unicode-static-3.1"
      )
  endif()

  target_link_libraries(
    ${TARGET}
    "${VST3_SDK_LIB}"
    "${VST3_BASE_LIB}"
    "${VST3_PLUGINTERFACES_LIB}"
    "${PROTOBUF_LIB}"
    "${RTMIDI_LIB}"
    "${PORTAUDIO_LIB}"
    "${FMT_LIB}"
    )

  if(JACK_LIB)
    target_link_libraries(
      ${TARGET}
      "${JACK_LIB}"
      )
  endif()

  if(XCODE)
    target_link_libraries(
      ${TARGET}
      "-framework CoreServices"
      "-framework CoreFoundation"
      "-framework AudioUnit"
      "-framework AudioToolbox"
      "-framework CoreAudio"
      "-framework CoreMidi"
      )
  endif()

  foreach(WX_LIB ${WX_LIBS})
    target_link_libraries(
      ${TARGET}
      "${SUBMODULE_INSTALL_DIR}/wxWidgets/lib/$<$<BOOL:${MSVC}>:vc_x64_lib>/${WX_LIB}"
      )
  endforeach()

  # Use dynamic link runtime libraries.
  set_target_properties(
    ${TARGET}
    PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
    )

  target_compile_options(
    ${TARGET}
    PRIVATE
    $<IF:$<CONFIG:Debug>,-DDEBUG -D_DEBUG,-DNDEBUG>
    )

  if(${TARGET} STREQUAL "Terra-Test")
    target_compile_options(
      ${TARGET}
      PRIVATE
      -DTERRA_BUILD_TEST
      )
  endif()

  if(MSVC)
    target_compile_options(
      ${TARGET}
      PRIVATE
      /nologo
      /MP
      /source-charset:utf-8
      -DHAVE_TYPE_TRAITS=1
      -DUNICODE
      -D_UNICODE
      -DNOMINMAX
      -D_USE_MATH_DEFINES
      /EHsc
      /FI"${PREFIX_HEADER_PATH}"
      )
  else()
    list(APPEND WX_OSX_CXX_FLAGS -D_FILE_OFFSET_BITS=64 -D__WXMAC__ -D__WXOSX__ -D__WXOSX_COCOA__)
    list(APPEND WX_OSX_LINKER_FLAGS "-framework IOKit" "-framework Carbon" "-framework Cocoa" "-framework AudioToolbox" "-framework System" "-framework OpenGL" "-framework WebKit" "-framework Security" -lz -lpthread -liconv)

    target_compile_options(
      ${TARGET}
      PRIVATE
      -Werror=return-type
      ${WX_OSX_CXX_FLAGS}
      )

    target_link_libraries(
      ${TARGET}
      ${WX_OSX_LINKER_FLAGS}
      )

    set_target_properties(${TARGET} PROPERTIES XCODE_ATTRIBUTE_GCC_PREFIX_HEADER "${PREFIX_HEADER_PATH}")
    set_target_properties(${TARGET} PROPERTIES XCODE_ATTRIBUTE_GCC_PRECOMPILE_PREFIX_HEADER "YES")
  endif()
endforeach()

####################################################################
# configure resource file deplyment
####################################################################

# リソースデータの配置
if(XCODE)
  set(RESOURCE_DIR "./data")
  get_filename_component(RESOURCE_DIR ${RESOURCE_DIR} ABSOLUTE)

  set(ICON_FILE_NAME "app-icon.icns")

  get_filename_component(INFO_PLIST_PATH "./Terra/Info.plist" ABSOLUTE)
  message("PLIST PATH: ${INFO_PLIST_PATH}")

  file(GLOB_RECURSE RESOURCE_FILES "${RESOURCE_DIR}/*")

  foreach(TARGET ${TARGET_LIST})
    # 階層構造を保持したままResourceディレクトリ以下に配置されるように設定する
    foreach(RESOURCE_FILE ${RESOURCE_FILES})
      get_filename_component(FILENAME ${RESOURCE_FILE} NAME)

      # 余計なファイルは無視
      if(${FILENAME} STREQUAL ".DS_Store")
        continue()
      endif()

      get_filename_component(DIR ${RESOURCE_FILE} DIRECTORY)
      string(REGEX REPLACE "^${RESOURCE_DIR}" "" DIR_IN_RESOURCE ${DIR})
      target_sources(${TARGET} PRIVATE ${RESOURCE_FILE})

#       message("resource: ${RESOURCE_FILE}")
#       message("dir in resouce: ${DIR_IN_RESOURCE}")
      set_source_files_properties(${RESOURCE_FILE} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources${DIR_IN_RESOURCE}")
      string(REPLACE "/" "\\" GROUP_NAME "Resources${DIR_IN_RESOURCE}")
      source_group(${GROUP_NAME} FILES "${RESOURCE_FILE}")
    endforeach()

    set_target_properties(
      ${TARGET}
      PROPERTIES
      MACOSX_BUNDLE_INFO_STRING "Copyright 2019, diatonic.jp"
      MACOSX_BUNDLE_COPYRIGHT "Copyright 2019, diatonic.jp"
      MACOSX_BUNDLE_GUI_IDENTIFIER "jp.diatonic.terra"
      MACOSX_BUNDLE_BUNDLE_NAME "${TARGET}"
      MACOSX_BUNDLE_BUNDLE_VERSION 0 0 1
      MACOSX_BUNDLE_BUNDLE_SHORT_VERSION_STRING "0.0.1"
      MACOSX_BUNDLE_BUNDLE_LONG_VERSION_STRING "0.0.1"
      MACOSX_BUNDLE_ICON_FILE "${ICON_FILE_NAME}"
      MACOSX_BUNDLE_INFO_PLIST "${INFO_PLIST_PATH}"
      )
  endforeach()
endif()

