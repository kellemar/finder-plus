cmake_minimum_required(VERSION 3.20)
project(finder-plus C CXX OBJC)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable ARC for Objective-C files
set(CMAKE_OBJC_FLAGS "${CMAKE_OBJC_FLAGS} -fobjc-arc")

# Compiler flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wpedantic")
set(CMAKE_C_FLAGS_DEBUG "-g -O0 -DDEBUG")
set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG")

# ============================================
# Local AI Models Option
# ============================================
option(FINDER_PLUS_AI_MODELS "Enable real local AI model inference" ON)

if(FINDER_PLUS_AI_MODELS)
    include(FetchContent)

    # bert.cpp for text embeddings (with its own ggml)
    FetchContent_Declare(
        bert_cpp
        GIT_REPOSITORY https://github.com/skeskinen/bert.cpp.git
        GIT_TAG master
        GIT_SHALLOW TRUE
    )

    # Set ggml options for bert.cpp's ggml
    # Disable Metal for now due to ARC compatibility issues in older ggml
    set(GGML_METAL OFF CACHE BOOL "Disable Metal backend" FORCE)
    set(GGML_ACCELERATE ON CACHE BOOL "Enable Accelerate framework" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static libraries" FORCE)

    FetchContent_MakeAvailable(bert_cpp)

    # clip.cpp for CLIP image/text embeddings
    FetchContent_Declare(
        clip_cpp
        GIT_REPOSITORY https://github.com/monatis/clip.cpp.git
        GIT_TAG main
        GIT_SHALLOW TRUE
    )

    # Disable clip.cpp examples and tests
    set(CLIP_BUILD_EXAMPLES OFF CACHE BOOL "Disable clip.cpp examples" FORCE)
    set(CLIP_BUILD_TESTS OFF CACHE BOOL "Disable clip.cpp tests" FORCE)

    # Get clip.cpp sources only (don't build, to avoid ggml conflicts)
    FetchContent_GetProperties(clip_cpp)
    if(NOT clip_cpp_POPULATED)
        FetchContent_Populate(clip_cpp)
    endif()

    # Build clip + its ggml as a SHARED library to give it its own symbol namespace
    # This avoids ggml symbol conflicts between bert.cpp (GGUF v1) and clip.cpp (GGUF v2)
    add_library(clip_dylib SHARED
        ${clip_cpp_SOURCE_DIR}/ggml/src/ggml.c
        ${clip_cpp_SOURCE_DIR}/ggml/src/ggml-alloc.c
        ${CMAKE_SOURCE_DIR}/src/ai/clip_patched.cpp
    )
    target_include_directories(clip_dylib PUBLIC
        ${clip_cpp_SOURCE_DIR}
    )
    target_include_directories(clip_dylib PRIVATE
        ${clip_cpp_SOURCE_DIR}/ggml/include
        ${clip_cpp_SOURCE_DIR}/ggml/include/ggml
    )
    target_compile_definitions(clip_dylib PRIVATE GGML_USE_ACCELERATE)
    target_link_libraries(clip_dylib "-framework Accelerate")
    set_target_properties(clip_dylib PROPERTIES
        OUTPUT_NAME "clip"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    )

    message(STATUS "bert.cpp source dir: ${bert_cpp_SOURCE_DIR}")
    message(STATUS "clip.cpp source dir: ${clip_cpp_SOURCE_DIR}")
endif()

# Raylib
set(RAYLIB_PATH ${CMAKE_SOURCE_DIR}/raylib)
set(RAYLIB_INCLUDE ${RAYLIB_PATH}/src)
set(RAYLIB_LIB ${RAYLIB_PATH}/src/libraylib.a)

# Source files
set(SOURCES
    src/main.c
    src/app.c
    src/core/filesystem.c
    src/core/operations.c
    src/core/operation_queue.c
    src/core/search.c
    src/core/git.c
    src/core/network.c
    src/ui/browser.c
    src/ui/breadcrumb.c
    src/ui/preview.c
    src/ui/video.c
    src/ui/sidebar.c
    src/ui/statusbar.c
    src/ui/tabs.c
    src/ui/command_bar.c
    src/ui/queue_panel.c
    src/ui/palette.c
    src/ui/dialog.c
    src/ui/context_menu.c
    src/ui/dual_pane.c
    src/ui/progress_indicator.c
    src/ui/file_view_modal.c
    src/utils/config.c
    src/utils/theme.c
    src/utils/keybindings.c
    src/utils/perf.c
    src/utils/text.c
    src/utils/font.c
    # Phase 4: AI Foundation
    src/api/http_client.c
    src/api/claude_client.c
    src/api/gemini_client.c
    src/api/auth.c
    src/tools/tool_registry.c
    src/tools/tool_executor.c
    external/cJSON/cJSON.c
    # Phase 5: Local AI
    src/ai/embeddings.c
    src/ai/vectordb.c
    src/ai/indexer.c
    src/ai/semantic_search.c
    src/ai/clip.c
    src/ai/visual_search.c
    # Phase 6: AI Features
    src/ai/duplicates.c
    src/ai/smart_rename.c
    src/ai/organization.c
    src/ai/summarize.c
    src/ai/summarize_async.c
    src/ai/nl_operations.c
    # Platform-specific
    src/platform/fsevents.c
    src/platform/clipboard.m
    src/platform/trash.m
)

# Main executable
add_executable(finder-plus ${SOURCES})

target_include_directories(finder-plus PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/external
    ${RAYLIB_INCLUDE}
)

# Find libcurl for HTTP requests
find_package(CURL REQUIRED)

# Find SQLite3 for vector database
find_package(SQLite3 REQUIRED)

# Find libssh2 for SFTP support (Phase 8)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBSSH2 REQUIRED IMPORTED_TARGET libssh2)

# macOS frameworks
find_library(COCOA_FRAMEWORK Cocoa)
find_library(IOKIT_FRAMEWORK IOKit)
find_library(OPENGL_FRAMEWORK OpenGL)
find_library(CORESERVICES_FRAMEWORK CoreServices)

target_link_libraries(finder-plus
    ${RAYLIB_LIB}
    ${COCOA_FRAMEWORK}
    ${IOKIT_FRAMEWORK}
    ${OPENGL_FRAMEWORK}
    ${CORESERVICES_FRAMEWORK}
    CURL::libcurl
    SQLite::SQLite3
    PkgConfig::LIBSSH2
)

# Local AI model support
if(FINDER_PLUS_AI_MODELS)
    target_compile_definitions(finder-plus PRIVATE FINDER_PLUS_AI_MODELS)
    target_include_directories(finder-plus PRIVATE
        ${bert_cpp_SOURCE_DIR}
        ${clip_cpp_SOURCE_DIR}
    )
    target_link_libraries(finder-plus bert ggml clip_dylib)

    # Metal frameworks for GPU acceleration on macOS
    if(APPLE)
        find_library(METAL_FRAMEWORK Metal)
        find_library(METALKIT_FRAMEWORK MetalKit)
        find_library(FOUNDATION_FRAMEWORK Foundation)
        if(METAL_FRAMEWORK AND METALKIT_FRAMEWORK)
            target_link_libraries(finder-plus
                ${METAL_FRAMEWORK}
                ${METALKIT_FRAMEWORK}
                ${FOUNDATION_FRAMEWORK}
            )
        endif()
    endif()
endif()

# Copy assets to build directory
file(COPY ${CMAKE_SOURCE_DIR}/assets DESTINATION ${CMAKE_BINARY_DIR})

# Test executable (minimal - no UI dependencies)
set(TEST_SOURCES
    tests/test_main.c
    tests/test_filesystem.c
    tests/test_selection.c
    tests/test_operations.c
    tests/test_search.c
    tests/test_config.c
    tests/test_tabs.c
    tests/test_tool_registry.c
    tests/test_http_client.c
    tests/test_claude_client.c
    tests/test_tool_executor.c
    tests/test_ai.c
    tests/test_phase6.c
    tests/test_git.c
    tests/test_operation_queue.c
    tests/test_keybindings.c
    tests/test_dialog.c
    tests/test_context_menu.c
    tests/test_dual_pane.c
    tests/test_network.c
    tests/test_perf.c
    tests/test_phase2.c
    tests/test_video.c
    tests/test_preview.c
    tests/test_gemini_client.c
    tests/test_font.c
    tests/test_progress_indicator.c
    tests/test_file_view_modal.c
    src/core/filesystem.c
    src/core/operations.c
    src/core/operation_queue.c
    src/core/git.c
    src/core/network.c
    src/utils/theme.c
    src/utils/keybindings.c
    src/utils/perf.c
    src/utils/font.c
    src/ui/dialog.c
    src/ui/context_menu.c
    src/ui/dual_pane.c
    src/ui/video.c
    src/ui/progress_indicator.c
    src/ui/file_view_modal.c
    src/api/http_client.c
    src/api/claude_client.c
    src/api/gemini_client.c
    src/api/auth.c
    src/tools/tool_registry.c
    src/tools/tool_executor.c
    external/cJSON/cJSON.c
    # Phase 5: Local AI
    src/ai/embeddings.c
    src/ai/vectordb.c
    src/ai/indexer.c
    src/ai/semantic_search.c
    src/ai/clip.c
    src/ai/visual_search.c
    # Phase 6: AI Features
    src/ai/duplicates.c
    src/ai/smart_rename.c
    src/ai/organization.c
    src/ai/summarize.c
    src/ai/summarize_async.c
    src/ai/nl_operations.c
    # Platform-specific
    src/platform/fsevents.c
    src/platform/clipboard.m
    src/platform/trash.m
)

add_executable(test_runner ${TEST_SOURCES})

# Define TESTING macro for test builds to exclude app-dependent code
target_compile_definitions(test_runner PRIVATE TESTING)

target_include_directories(test_runner PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/tests
    ${CMAKE_SOURCE_DIR}/external
    ${RAYLIB_INCLUDE}
)

target_link_libraries(test_runner
    ${RAYLIB_LIB}
    ${COCOA_FRAMEWORK}
    ${IOKIT_FRAMEWORK}
    ${OPENGL_FRAMEWORK}
    ${CORESERVICES_FRAMEWORK}
    CURL::libcurl
    SQLite::SQLite3
    PkgConfig::LIBSSH2
)

# Local AI model support for tests
if(FINDER_PLUS_AI_MODELS)
    target_compile_definitions(test_runner PRIVATE FINDER_PLUS_AI_MODELS)
    target_include_directories(test_runner PRIVATE
        ${bert_cpp_SOURCE_DIR}
        ${clip_cpp_SOURCE_DIR}
    )
    target_link_libraries(test_runner bert ggml clip_dylib)

    if(APPLE)
        if(METAL_FRAMEWORK AND METALKIT_FRAMEWORK)
            target_link_libraries(test_runner
                ${METAL_FRAMEWORK}
                ${METALKIT_FRAMEWORK}
                ${FOUNDATION_FRAMEWORK}
            )
        endif()
    endif()
endif()

# Custom target to run tests
add_custom_target(test
    COMMAND test_runner
    DEPENDS test_runner
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# Performance test executable
add_executable(perf_test
    tests/test_performance.c
    src/core/filesystem.c
    src/utils/perf.c
)

target_include_directories(perf_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${RAYLIB_INCLUDE}
)

target_link_libraries(perf_test
    ${COCOA_FRAMEWORK}
)
