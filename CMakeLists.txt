cmake_minimum_required(VERSION 3.14)
project(whisper-typer LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Configure whisper.cpp build: enable SDL2, disable examples and tests
set(WHISPER_SDL2 ON CACHE BOOL "" FORCE)
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static libraries" FORCE)

add_subdirectory(whisper.cpp)

find_package(SDL2 REQUIRED)
find_package(Threads REQUIRED)

# Optional system tray support via ayatana-appindicator3
option(ENABLE_TRAY "Build with system tray support" ON)
if(ENABLE_TRAY)
    find_package(PkgConfig)
    if(PkgConfig_FOUND)
        pkg_check_modules(APPINDICATOR ayatana-appindicator3-0.1)
    endif()
    if(APPINDICATOR_FOUND)
        set(HAS_TRAY ON)
    else()
        set(HAS_TRAY OFF)
        message(STATUS "Tray disabled: libayatana-appindicator3-dev not found")
    endif()
else()
    set(HAS_TRAY OFF)
endif()

set(EXAMPLES_DIR ${CMAKE_SOURCE_DIR}/whisper.cpp/examples)

# Build the common library (normally built by examples/CMakeLists.txt)
add_library(common STATIC
    ${EXAMPLES_DIR}/common.h
    ${EXAMPLES_DIR}/common.cpp
    ${EXAMPLES_DIR}/common-ggml.h
    ${EXAMPLES_DIR}/common-ggml.cpp
    ${EXAMPLES_DIR}/common-whisper.h
    ${EXAMPLES_DIR}/common-whisper.cpp
    ${EXAMPLES_DIR}/grammar-parser.h
    ${EXAMPLES_DIR}/grammar-parser.cpp
)
target_include_directories(common PUBLIC ${EXAMPLES_DIR})
target_link_libraries(common PRIVATE whisper)
set_target_properties(common PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Build the common-sdl library
add_library(common-sdl STATIC
    ${EXAMPLES_DIR}/common-sdl.h
    ${EXAMPLES_DIR}/common-sdl.cpp
)
target_include_directories(common-sdl PUBLIC ${EXAMPLES_DIR} ${SDL2_INCLUDE_DIRS})
target_compile_definitions(common-sdl PUBLIC _REENTRANT)
target_link_libraries(common-sdl PRIVATE ${SDL2_LIBRARIES})
set_target_properties(common-sdl PROPERTIES POSITION_INDEPENDENT_CODE ON)

# whisper-typer executable
add_executable(whisper-typer
    src/typer.cpp
    src/hotkey.cpp
    src/text-output.cpp
)

target_include_directories(whisper-typer PRIVATE
    ${EXAMPLES_DIR}
    ${CMAKE_SOURCE_DIR}/whisper.cpp/include
    ${CMAKE_SOURCE_DIR}/src
)

# Link order matters: common-sdl (and its SDL2 dependency) must come
# before common to avoid SDL2 audio callback issues
target_link_libraries(whisper-typer PRIVATE
    common-sdl
    ${SDL2_LIBRARIES}
    common
    whisper
    ${CMAKE_THREAD_LIBS_INIT}
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(whisper-typer PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(HAS_TRAY)
    target_sources(whisper-typer PRIVATE src/tray.cpp)
    target_include_directories(whisper-typer PRIVATE ${APPINDICATOR_INCLUDE_DIRS})
    target_link_libraries(whisper-typer PRIVATE ${APPINDICATOR_LIBRARIES})
    target_compile_definitions(whisper-typer PRIVATE HAS_TRAY=1)
endif()

include(GNUInstallDirs)
install(TARGETS whisper-typer RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES contrib/whisper-typer.service
    DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/user)
install(FILES contrib/whisper-typer.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES contrib/whisper-typer.desktop
    DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/xdg/autostart)

# Tests
option(BUILD_TESTS "Build unit tests" OFF)
if(BUILD_TESTS)
    enable_testing()

    add_executable(test-parsers tests/test_parsers.cpp)
    target_compile_features(test-parsers PRIVATE cxx_std_17)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(test-parsers PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME parsers COMMAND test-parsers)

    add_executable(test-hotkey tests/test_hotkey.cpp)
    target_include_directories(test-hotkey PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_compile_features(test-hotkey PRIVATE cxx_std_17)
    target_link_libraries(test-hotkey PRIVATE ${CMAKE_THREAD_LIBS_INIT})
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(test-hotkey PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-function)
    endif()
    add_test(NAME hotkey COMMAND test-hotkey)

    add_executable(test-terminal tests/test_terminal.cpp)
    target_include_directories(test-terminal PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_compile_features(test-terminal PRIVATE cxx_std_17)
    target_link_libraries(test-terminal PRIVATE ${CMAKE_THREAD_LIBS_INIT})
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(test-terminal PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-function)
    endif()
    add_test(NAME terminal COMMAND test-terminal)

    add_executable(test-history tests/test_history.cpp)
    target_compile_features(test-history PRIVATE cxx_std_17)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(test-history PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME history COMMAND test-history)

    add_executable(test-check-dep tests/test_check_dep.cpp)
    target_compile_features(test-check-dep PRIVATE cxx_std_17)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(test-check-dep PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME check-dep COMMAND test-check-dep)
endif()
