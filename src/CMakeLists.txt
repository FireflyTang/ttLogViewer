# ── Core library (all layers except main) ─────────────────────────────────────
add_library(ttLogViewer_lib STATIC
    app_config.cpp
    log_reader.cpp
    filter_chain.cpp
    app_controller.cpp
    render_utils.cpp
    render.cpp
    clipboard.cpp
)

target_include_directories(ttLogViewer_lib
    PUBLIC  ${CMAKE_SOURCE_DIR}/include
    PRIVATE ${CMAKE_BINARY_DIR}/include   # Generated version.hpp
)

target_link_libraries(ttLogViewer_lib PUBLIC
    ftxui::screen
    ftxui::dom
    ftxui::component
    nlohmann_json::nlohmann_json
    ${PLATFORM_LIBS}
)

# ── Main executable ────────────────────────────────────────────────────────────
if(WIN32)
    # Include the generated Windows VERSIONINFO resource for proper "Open With" name
    add_executable(ttLogViewer main.cpp ${CMAKE_BINARY_DIR}/version.rc)
else()
    add_executable(ttLogViewer main.cpp)
endif()

target_link_libraries(ttLogViewer PRIVATE ttLogViewer_lib)

set_target_properties(ttLogViewer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

if(STATIC_RUNTIME)
    if(MINGW)
        # Statically link GCC and C++ runtimes.
        target_link_options(ttLogViewer PRIVATE -static-libgcc -static-libstdc++)
        # Statically link winpthread by specifying the archive's full path.
        # This ensures the static version is used even when FTXUI also references
        # winpthread (the archive is placed AFTER all user .a files in link order).
        find_library(WINPTHREAD_STATIC
            NAMES libwinpthread.a
            PATHS C:/msys64/mingw64/lib ${CMAKE_SYSROOT}/lib
            NO_DEFAULT_PATH)
        if(WINPTHREAD_STATIC)
            # --whole-archive forces all winpthread symbols into the binary upfront,
            # so FTXUI's later references to winpthread resolve from these definitions
            # rather than from the import library.
            target_link_libraries(ttLogViewer PRIVATE
                -Wl,--whole-archive ${WINPTHREAD_STATIC} -Wl,--no-whole-archive)
        else()
            message(WARNING "Static libwinpthread.a not found; binary will depend on libwinpthread-1.dll")
        endif()
    elseif(DEFINED STATIC_RUNTIME_FLAGS)
        target_link_options(ttLogViewer PRIVATE ${STATIC_RUNTIME_FLAGS})
    endif()
endif()
