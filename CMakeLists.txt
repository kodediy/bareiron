# bareiron as an ESP-IDF component. The host firmware (for example
# KodeCraft) provides the network, calls bareiron_run from a task and
# passes its own BareironPlatform.

if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/src/registries.c")
  message(FATAL_ERROR
    "bareiron: src/registries.c is missing. Generate it with "
    "./extract_registries.sh (Java 21 + the 1.21.8 server.jar, see README).")
endif()

# Every server source but the computer's entry point and its platform: a
# new file can't be left out of the firmware
# (ESP-IDF first reads this file in script mode, where CONFIGURE_DEPENDS is not allowed)
if(CMAKE_SCRIPT_MODE_FILE)
  file(GLOB BAREIRON_SRCS "${CMAKE_CURRENT_LIST_DIR}/src/*.c")
else()
  file(GLOB BAREIRON_SRCS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/src/*.c")
endif()
list(FILTER BAREIRON_SRCS EXCLUDE REGEX "/src/(main|platform_posix)\\.c$")

idf_component_register(
  SRCS
    ${BAREIRON_SRCS}
    third_party/miniz/miniz.c
  INCLUDE_DIRS include third_party/miniz
  REQUIRES lwip esp_timer
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
  MINIZ_NO_STDIO MINIZ_NO_TIME MINIZ_NO_ARCHIVE_APIS MINIZ_NO_ZLIB_COMPATIBLE_NAMES)

# miniz is third-party code; keep its warnings from failing -Werror builds
set_source_files_properties(third_party/miniz/miniz.c PROPERTIES COMPILE_OPTIONS "-Wno-type-limits")
