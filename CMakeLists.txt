cmake_minimum_required(VERSION 3.20)

project(Solar VERSION 0.5.0 LANGUAGES C)

include(CTest)
include(GNUInstallDirs)

option(SOLAR_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

set(SOLAR_YANC_MAKE_OPTIONS)
if(SOLAR_ENABLE_SANITIZERS AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    list(APPEND SOLAR_YANC_MAKE_OPTIONS
        "CFLAGS=-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer"
    )
endif()

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(SOLAR_YANC_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/yanc")
set(SOLAR_YANC_STAGE_DIR "${CMAKE_CURRENT_BINARY_DIR}/yanc-stage")
set(SOLAR_YANC_BUNDLE_DIR "${CMAKE_CURRENT_BINARY_DIR}/yanc-bundle")
set(SOLAR_YANC_BIN_DIR "${SOLAR_YANC_BUNDLE_DIR}/bin")
set(SOLAR_YANC_BINARIES
    "${SOLAR_YANC_BIN_DIR}/cmmcomp"
    "${SOLAR_YANC_BIN_DIR}/appcomp"
    "${SOLAR_YANC_BIN_DIR}/asmcomp"
    "${SOLAR_YANC_BIN_DIR}/cpppp"
    "${SOLAR_YANC_BIN_DIR}/cppcomp"
    "${SOLAR_YANC_BIN_DIR}/gen_gtkw"
)
set(SOLAR_COCOTB_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/cocotb")
set(SOLAR_COCOTB_BUILD_SCRIPT
    "${SOLAR_COCOTB_BUILD_DIR}/solar_cocotb_runner.py"
)
file(MAKE_DIRECTORY "${SOLAR_COCOTB_BUILD_DIR}")
configure_file(
    scripts/solar_cocotb_runner.py
    "${SOLAR_COCOTB_BUILD_SCRIPT}"
    COPYONLY
)

find_program(SOLAR_MAKE_EXECUTABLE NAMES gmake make REQUIRED)
find_program(SOLAR_FLEX_EXECUTABLE NAMES flex REQUIRED)
find_program(SOLAR_BISON_EXECUTABLE NAMES bison REQUIRED)

file(GLOB_RECURSE SOLAR_YANC_SOURCES CONFIGURE_DEPENDS
    "${SOLAR_YANC_SOURCE_DIR}/Compilers/*"
    "${SOLAR_YANC_SOURCE_DIR}/HDL/*"
    "${SOLAR_YANC_SOURCE_DIR}/Scripts/gen_gtkw.c"
    "${SOLAR_YANC_SOURCE_DIR}/Makefile"
)

# YANC generates Flex/Bison C files beside its grammars. Building from a staged
# copy keeps the vendored upstream snapshot read-only and reviewable.
add_custom_command(
    OUTPUT ${SOLAR_YANC_BINARIES}
    COMMAND "${CMAKE_COMMAND}" -E remove_directory "${SOLAR_YANC_STAGE_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E remove_directory "${SOLAR_YANC_BUNDLE_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${SOLAR_YANC_SOURCE_DIR}" "${SOLAR_YANC_STAGE_DIR}"
    COMMAND "${SOLAR_MAKE_EXECUTABLE}" -C "${SOLAR_YANC_STAGE_DIR}"
        "CC=${CMAKE_C_COMPILER}"
        "FLEX=${SOLAR_FLEX_EXECUTABLE}"
        "BISON=${SOLAR_BISON_EXECUTABLE}"
        "BIN=${SOLAR_YANC_BIN_DIR}"
        ${SOLAR_YANC_MAKE_OPTIONS}
        cmmcomp appcomp asmcomp cpppp cppcomp gen_gtkw
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${SOLAR_YANC_STAGE_DIR}/HDL" "${SOLAR_YANC_BUNDLE_DIR}/HDL"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${SOLAR_YANC_STAGE_DIR}/Compilers/CMMComp/Includes"
        "${SOLAR_YANC_BUNDLE_DIR}/Macros"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${SOLAR_YANC_STAGE_DIR}/Compilers/CPPComp/Includes"
        "${SOLAR_YANC_BUNDLE_DIR}/Header"
    DEPENDS ${SOLAR_YANC_SOURCES}
    COMMENT "Building bundled YANC 5.2 toolchain"
    VERBATIM
)
add_custom_target(solar_yanc_toolchain ALL DEPENDS ${SOLAR_YANC_BINARIES})

add_library(solar_warnings INTERFACE)
target_compile_options(solar_warnings INTERFACE
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wshadow
    -Wstrict-prototypes
)

if(SOLAR_ENABLE_SANITIZERS)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(solar_warnings INTERFACE
            -fsanitize=address,undefined
            -fno-omit-frame-pointer
        )
        target_link_options(solar_warnings INTERFACE
            -fsanitize=address,undefined
            -fno-omit-frame-pointer
        )
    else()
        message(WARNING "SOLAR_ENABLE_SANITIZERS is unsupported by this compiler")
    endif()
endif()

add_library(solar_core STATIC
    src/core/artifact.c
    src/core/diagnostics.c
    src/core/filesystem.c
    src/core/config.c
    src/core/config_edit.c
    src/core/config_model.c
    src/core/cmm_source.c
    src/core/discovery.c
    src/core/project.c
    src/core/progress.c
    src/core/report_compare.c
    src/core/report_history.c
    src/core/rtl.c
    src/core/build.c
    src/core/compiler.c
    src/core/cocotb.c
    src/core/test.c
    src/core/synthesis.c
    src/core/synthesis_statistics.c
    src/core/system.c
    src/core/waveform.c
    src/core/runner.c
    src/core/scan.c
    src/backends/registry.c
    src/backends/iverilog.c
    src/backends/verilator.c
    src/backends/yanc_resolver.c
    src/backends/yanc.c
    src/backends/yosys.c
)
target_include_directories(solar_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_compile_definitions(solar_core PRIVATE
    _POSIX_C_SOURCE=200809L
    _XOPEN_SOURCE=700
    SOLAR_BUILD_COMPILER="${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}"
    SOLAR_BUILD_TYPE="${CMAKE_BUILD_TYPE}"
    SOLAR_YANC_INSTALL_RELATIVE="../${CMAKE_INSTALL_LIBEXECDIR}/solar/yanc"
    SOLAR_COCOTB_INSTALL_RELATIVE="../${CMAKE_INSTALL_LIBEXECDIR}/solar/solar_cocotb_runner.py"
)
target_link_libraries(solar_core PRIVATE solar_warnings)

add_executable(solar
    src/cli/main.c
    src/cli/command_init.c
    src/cli/command_check.c
    src/cli/command_config.c
    src/cli/command_clean.c
    src/cli/command_doctor.c
    src/cli/command_view.c
    src/cli/command_build.c
    src/cli/command_report.c
    src/cli/command_completion.c
    src/cli/command_scan.c
    src/cli/progress.c
)
target_link_libraries(solar PRIVATE solar_core solar_warnings)
add_dependencies(solar solar_yanc_toolchain)

install(
    TARGETS solar solar_core
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(
    DIRECTORY include/solar
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h"
)
install(
    FILES README.md CHANGELOG.md CONTRIBUTING.md SUPPORT.md SECURITY.md
          CODE_OF_CONDUCT.md LICENSE
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
)
install(
    PROGRAMS install.sh
    DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/solar
)
install(
    PROGRAMS ${SOLAR_YANC_BINARIES}
    DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/solar/yanc/bin
)
install(
    DIRECTORY "${SOLAR_YANC_BUNDLE_DIR}/HDL/"
    DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/solar/yanc/HDL
)
install(
    DIRECTORY "${SOLAR_YANC_BUNDLE_DIR}/Macros/"
    DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/solar/yanc/Macros
)
install(
    DIRECTORY "${SOLAR_YANC_BUNDLE_DIR}/Header/"
    DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/solar/yanc/Header
)
install(
    FILES THIRD_PARTY_NOTICES.md third_party/yanc/LICENSE
    DESTINATION ${CMAKE_INSTALL_DOCDIR}/licenses
)
install(
    PROGRAMS "${SOLAR_COCOTB_BUILD_SCRIPT}"
    DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/solar
)
install(
    DIRECTORY docs/
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
)
install(
    FILES completions/bash/solar
    DESTINATION ${CMAKE_INSTALL_DATADIR}/bash-completion/completions
)
install(
    FILES completions/zsh/_solar
    DESTINATION ${CMAKE_INSTALL_DATADIR}/zsh/site-functions
)
install(
    FILES completions/fish/solar.fish
    DESTINATION ${CMAKE_INSTALL_DATADIR}/fish/vendor_completions.d
)

set(CPACK_GENERATOR "TGZ")
set(CPACK_PACKAGE_NAME "solar")
set(CPACK_PACKAGE_VENDOR "ART3121")
set(CPACK_PACKAGE_CONTACT "https://github.com/ART3121/solar")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Lightweight open hardware design platform and EDA workflow orchestrator"
)
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME
    "solar-${PROJECT_VERSION}-linux-${CMAKE_SYSTEM_PROCESSOR}"
)
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
include(CPack)

if(BUILD_TESTING)
    find_program(SOLAR_TEST_BASH_EXECUTABLE bash)
    find_program(SOLAR_TEST_ZSH_EXECUTABLE zsh)
    find_program(SOLAR_TEST_FISH_EXECUTABLE fish)

    add_executable(test_result tests/test_result.c)
    target_link_libraries(test_result PRIVATE solar_core solar_warnings)
    add_test(NAME result COMMAND test_result)

    add_executable(test_system tests/test_system.c)
    target_link_libraries(test_system PRIVATE solar_core solar_warnings)
    add_test(NAME system COMMAND test_system)

    add_executable(test_config tests/test_config.c)
    target_link_libraries(test_config PRIVATE solar_core solar_warnings)
    add_test(NAME config COMMAND test_config)

    add_executable(test_config_v2 tests/test_config_v2.c)
    target_link_libraries(test_config_v2 PRIVATE solar_core solar_warnings)
    add_test(NAME config_v2 COMMAND test_config_v2)

    add_executable(test_config_edit tests/test_config_edit.c)
    target_link_libraries(test_config_edit PRIVATE solar_core solar_warnings)
    add_test(NAME config_edit COMMAND test_config_edit)

    add_executable(test_validation_v2 tests/test_validation_v2.c)
    target_link_libraries(test_validation_v2 PRIVATE solar_core solar_warnings)
    add_test(NAME validation_v2 COMMAND test_validation_v2)

    add_executable(test_effective_config tests/test_effective_config.c)
    target_link_libraries(test_effective_config PRIVATE solar_core solar_warnings)
    add_test(NAME effective_config COMMAND test_effective_config)

    add_executable(test_project tests/test_project.c)
    target_link_libraries(test_project PRIVATE solar_core solar_warnings)
    add_test(NAME project COMMAND test_project)

    add_executable(test_discovery tests/test_discovery.c)
    target_link_libraries(test_discovery PRIVATE solar_core solar_warnings)
    add_test(NAME discovery COMMAND test_discovery $<TARGET_FILE:solar>)

    add_executable(test_scan tests/test_scan.c)
    target_link_libraries(test_scan PRIVATE solar_core solar_warnings)
    add_test(NAME scan COMMAND test_scan)

    add_executable(runner_helper tests/runner_helper.c)
    target_link_libraries(runner_helper PRIVATE solar_warnings)

    add_executable(test_runner tests/test_runner.c)
    target_link_libraries(test_runner PRIVATE solar_core solar_warnings)
    add_test(NAME runner COMMAND test_runner $<TARGET_FILE:runner_helper>)

    add_executable(test_progress tests/test_progress.c src/cli/progress.c)
    target_link_libraries(test_progress PRIVATE solar_core solar_warnings)
    add_test(NAME progress COMMAND test_progress)

    add_executable(test_filesystem tests/test_filesystem.c)
    target_link_libraries(test_filesystem PRIVATE solar_core solar_warnings)
    add_test(NAME filesystem COMMAND test_filesystem)

    add_executable(test_artifact tests/test_artifact.c)
    target_link_libraries(test_artifact PRIVATE solar_core solar_warnings)
    add_test(NAME artifact_registry COMMAND test_artifact)

    add_executable(viewer_helper tests/helpers/viewer_helper.c)
    target_link_libraries(viewer_helper PRIVATE solar_warnings)

    add_executable(test_waveform tests/test_waveform.c)
    target_link_libraries(test_waveform PRIVATE solar_core solar_warnings)
    add_test(
        NAME waveform_viewer
        COMMAND test_waveform $<TARGET_FILE:viewer_helper>
    )

    add_executable(backend_helper tests/backend_helper.c)
    target_link_libraries(backend_helper PRIVATE solar_warnings)

    add_executable(test_backends tests/test_backends.c)
    target_link_libraries(test_backends PRIVATE solar_core solar_warnings)
    add_test(NAME backends COMMAND test_backends $<TARGET_FILE:backend_helper>)

    add_executable(
        test_synthesis_statistics tests/test_synthesis_statistics.c
    )
    target_link_libraries(
        test_synthesis_statistics PRIVATE solar_core solar_warnings
    )
    target_compile_definitions(test_synthesis_statistics PRIVATE
        SOLAR_TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/yosys"
    )
    add_test(NAME synthesis_statistics COMMAND test_synthesis_statistics)

    add_executable(test_report_history tests/test_report_history.c)
    target_link_libraries(test_report_history PRIVATE solar_core solar_warnings)
    add_test(
        NAME report_history
        COMMAND test_report_history $<TARGET_FILE:solar>
    )

    add_executable(test_completion tests/test_completion.c)
    target_link_libraries(test_completion PRIVATE solar_core solar_warnings)
    target_compile_definitions(test_completion PRIVATE
        SOLAR_BASH_COMPLETION="${CMAKE_CURRENT_SOURCE_DIR}/completions/bash/solar"
        SOLAR_ZSH_COMPLETION="${CMAKE_CURRENT_SOURCE_DIR}/completions/zsh/_solar"
        SOLAR_FISH_COMPLETION="${CMAKE_CURRENT_SOURCE_DIR}/completions/fish/solar.fish"
        SOLAR_TEST_BASH="$<IF:$<BOOL:${SOLAR_TEST_BASH_EXECUTABLE}>,${SOLAR_TEST_BASH_EXECUTABLE},>"
        SOLAR_TEST_ZSH="$<IF:$<BOOL:${SOLAR_TEST_ZSH_EXECUTABLE}>,${SOLAR_TEST_ZSH_EXECUTABLE},>"
    )
    add_test(
        NAME completion
        COMMAND test_completion $<TARGET_FILE:solar>
    )
    add_test(
        NAME installer
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_installer.sh
            ${CMAKE_CURRENT_SOURCE_DIR}/install.sh $<TARGET_FILE:solar>
    )
    if(SOLAR_TEST_BASH_EXECUTABLE)
        add_test(
            NAME completion_bash_syntax
            COMMAND ${SOLAR_TEST_BASH_EXECUTABLE} -n
                ${CMAKE_CURRENT_SOURCE_DIR}/completions/bash/solar
        )
    endif()
    if(SOLAR_TEST_ZSH_EXECUTABLE)
        add_test(
            NAME completion_zsh_syntax
            COMMAND ${SOLAR_TEST_ZSH_EXECUTABLE} -n
                ${CMAKE_CURRENT_SOURCE_DIR}/completions/zsh/_solar
        )
    endif()
    if(SOLAR_TEST_FISH_EXECUTABLE)
        add_test(
            NAME completion_fish_syntax
            COMMAND ${SOLAR_TEST_FISH_EXECUTABLE} -n
                ${CMAKE_CURRENT_SOURCE_DIR}/completions/fish/solar.fish
        )
    else()
        add_test(NAME completion_fish_syntax COMMAND test_completion --skip-fish)
        set_tests_properties(
            completion_fish_syntax PROPERTIES SKIP_RETURN_CODE 77
        )
    endif()

    add_executable(test_verilator tests/test_verilator.c)
    target_link_libraries(test_verilator PRIVATE solar_core solar_warnings)
    add_test(
        NAME verilator_backend
        COMMAND test_verilator $<TARGET_FILE:test_service_backend_helper>
    )

    add_executable(test_cocotb tests/test_cocotb.c)
    target_link_libraries(test_cocotb PRIVATE solar_core solar_warnings)
    add_test(
        NAME cocotb_driver
        COMMAND test_cocotb $<TARGET_FILE:test_service_backend_helper>
    )

    add_executable(test_service_backend_helper tests/helpers/backend_helper.c)
    target_link_libraries(test_service_backend_helper PRIVATE solar_warnings)

    add_executable(test_test_service tests/test_test_service.c)
    target_link_libraries(test_test_service PRIVATE solar_core solar_warnings)
    add_test(
        NAME test_service
        COMMAND test_test_service $<TARGET_FILE:test_service_backend_helper>
    )

    add_executable(yanc_tool tests/helpers/yanc_tool.c)
    target_link_libraries(yanc_tool PRIVATE solar_warnings)

    add_executable(test_yanc_resolver tests/test_yanc_resolver.c)
    target_link_libraries(test_yanc_resolver PRIVATE solar_core solar_warnings)
    add_test(
        NAME yanc_resolver
        COMMAND test_yanc_resolver $<TARGET_FILE:yanc_tool>
    )

    add_executable(test_compiler_yanc tests/test_compiler_yanc.c)
    target_link_libraries(test_compiler_yanc PRIVATE solar_core solar_warnings)
    add_test(
        NAME compiler_yanc
        COMMAND test_compiler_yanc $<TARGET_FILE:yanc_tool>
    )

    add_executable(test_cli tests/test_cli.c)
    target_link_libraries(test_cli PRIVATE solar_core solar_warnings)
    add_test(
        NAME cli
        COMMAND test_cli
            $<TARGET_FILE:solar>
            $<TARGET_FILE:backend_helper>
            $<TARGET_FILE:viewer_helper>
    )

    add_executable(test_integration tests/test_integration.c)
    target_link_libraries(test_integration PRIVATE solar_core solar_warnings)
    add_test(
        NAME integration_iverilog
        COMMAND test_integration $<TARGET_FILE:solar> sim
    )
    add_test(
        NAME integration_iverilog_no_waveform
        COMMAND test_integration $<TARGET_FILE:solar> sim-no-waveform
    )
    add_test(
        NAME integration_yosys
        COMMAND test_integration $<TARGET_FILE:solar> synth
    )
    add_test(
        NAME integration_verilator
        COMMAND test_integration $<TARGET_FILE:solar> verilator
    )
    set_tests_properties(
        integration_iverilog
        integration_iverilog_no_waveform
        integration_yosys
        integration_verilator
        PROPERTIES SKIP_RETURN_CODE 77 LABELS integration
    )

    add_executable(
        test_cocotb_integration tests/test_cocotb_integration.c
    )
    target_link_libraries(
        test_cocotb_integration PRIVATE solar_core solar_warnings
    )
    add_test(
        NAME integration_verilator_cocotb
        COMMAND test_cocotb_integration $<TARGET_FILE:solar>
    )
    set_tests_properties(
        integration_verilator_cocotb
        PROPERTIES SKIP_RETURN_CODE 77 LABELS "integration;verilator;cocotb"
    )

    add_executable(test_yanc_integration tests/test_yanc_integration.c)
    target_link_libraries(test_yanc_integration PRIVATE solar_core solar_warnings)
    foreach(yanc_language IN ITEMS cmm cpp asm)
        add_test(
            NAME integration_yanc_${yanc_language}
            COMMAND test_yanc_integration
                $<TARGET_FILE:solar>
                yanc-${yanc_language}
                $<TARGET_FILE:viewer_helper>
        )
        set_tests_properties(
            integration_yanc_${yanc_language}
            PROPERTIES
                SKIP_RETURN_CODE 77
                LABELS "integration;yanc"
                ENVIRONMENT "SOLAR_TEST_YANC_ROOT=${SOLAR_YANC_BUNDLE_DIR}"
        )
    endforeach()

    add_executable(test_v1_integration tests/test_v1_integration.c)
    target_link_libraries(test_v1_integration PRIVATE solar_core solar_warnings)
    add_test(
        NAME integration_v1_compatibility
        COMMAND test_v1_integration $<TARGET_FILE:solar>
    )
    set_tests_properties(
        integration_v1_compatibility
        PROPERTIES SKIP_RETURN_CODE 77 LABELS integration
    )

    add_executable(test_v2_integration tests/test_v2_integration.c)
    target_link_libraries(test_v2_integration PRIVATE solar_core solar_warnings)
    add_test(
        NAME integration_v2_verilog_flow
        COMMAND test_v2_integration $<TARGET_FILE:solar>
    )
    set_tests_properties(
        integration_v2_verilog_flow
        PROPERTIES SKIP_RETURN_CODE 77 LABELS "integration;verilog;v2"
    )
endif()
