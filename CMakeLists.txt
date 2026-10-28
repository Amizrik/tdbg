cmake_minimum_required(VERSION 3.10)
project(tdbg VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(TDBG_SOURCES src/common.c src/qr.c src/datamatrix.c src/aztec.c src/maxicode.c)

add_library(tdbg SHARED ${TDBG_SOURCES})
target_include_directories(tdbg PUBLIC include PRIVATE src)
target_compile_definitions(tdbg PRIVATE TDBG_BUILD)
set_target_properties(tdbg PROPERTIES C_VISIBILITY_PRESET hidden)

add_library(tdbg_static STATIC ${TDBG_SOURCES})
target_include_directories(tdbg_static PUBLIC include PRIVATE src)
target_compile_definitions(tdbg_static PUBLIC TDBG_STATIC)

add_executable(tdbg_cli cli/main.c)
target_link_libraries(tdbg_cli tdbg_static)
set_target_properties(tdbg_cli PROPERTIES OUTPUT_NAME tdbg)

if(NOT WIN32)
  target_link_libraries(tdbg m)
  target_link_libraries(tdbg_static m)
endif()
if(MSVC)
  target_compile_definitions(tdbg PRIVATE _CRT_SECURE_NO_WARNINGS)
  target_compile_definitions(tdbg_cli PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

install(TARGETS tdbg tdbg_static tdbg_cli)
install(FILES include/tdbg.h DESTINATION include)
