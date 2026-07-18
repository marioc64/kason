cmake_minimum_required(VERSION 3.16)

project(KaSON VERSION 0.9.0 LANGUAGES C)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(KaSON_IS_TOP_LEVEL ON)
else()
    set(KaSON_IS_TOP_LEVEL OFF)
endif()

option(KaSON_BUILD_TESTS "Build the KaSON tests" "${KaSON_IS_TOP_LEVEL}")
option(KaSON_BUILD_EXAMPLES "Build the example applications" "${KaSON_IS_TOP_LEVEL}")
option(KaSON_BUILD_BENCHMARK "Build the optional comparison benchmark" OFF)
set(KaSON_BENCHMARK_LTO_DEFAULT ON)
if(CMAKE_C_COMPILER_ID STREQUAL "AppleClang")
    set(KaSON_BENCHMARK_LTO_DEFAULT OFF)
endif()
option(KaSON_BENCHMARK_LTO "Enable supported Release LTO for benchmark targets"
    "${KaSON_BENCHMARK_LTO_DEFAULT}")
option(KaSON_BUILD_FUZZER "Build the Clang libFuzzer target" OFF)
option(KaSON_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(KaSON_BUILD_DOCS "Build the Doxygen API documentation" OFF)
option(BUILD_SHARED_LIBS "Build libraries as shared libraries" OFF)

if(KaSON_BUILD_DOCS)
    find_package(Doxygen REQUIRED)

    set(KaSON_DOXYGEN_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/docs")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/docs/Doxyfile.in"
        "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile"
        @ONLY
    )

    add_custom_target(docs
        COMMAND Doxygen::doxygen "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Generating KaSON API documentation"
        VERBATIM
    )
endif()

if(KaSON_IS_TOP_LEVEL OR KaSON_BUILD_TESTS)
    include(CTest)
endif()

function(kason_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

if(KaSON_ENABLE_SANITIZERS)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    else()
        message(FATAL_ERROR "KaSON_ENABLE_SANITIZERS requires a Clang or GCC-compatible compiler")
    endif()
endif()

add_library(kason kason.c)
add_library(kason::kason ALIAS kason)
target_include_directories(kason PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)
target_compile_features(kason PUBLIC c_std_99)
set_target_properties(kason PROPERTIES
    C_EXTENSIONS OFF
    VERSION "${PROJECT_VERSION}"
    SOVERSION "${PROJECT_VERSION_MAJOR}"
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)
kason_enable_warnings(kason)

add_library(kason_schema kason_schema.c)
add_library(kason::schema ALIAS kason_schema)
target_link_libraries(kason_schema PUBLIC kason::kason)
target_include_directories(kason_schema PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)
target_compile_features(kason_schema PUBLIC c_std_99)
set_target_properties(kason_schema PROPERTIES
    EXPORT_NAME schema
    C_EXTENSIONS OFF
    VERSION "${PROJECT_VERSION}"
    SOVERSION "${PROJECT_VERSION_MAJOR}"
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)
kason_enable_warnings(kason_schema)

set(KaSON_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/kason")

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/kasonConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/kasonConfig.cmake"
    INSTALL_DESTINATION "${KaSON_INSTALL_CMAKEDIR}"
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/kasonConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMinorVersion
)

install(TARGETS kason kason_schema
    EXPORT kasonTargets
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

install(FILES
    kason.h
    kason_schema.h
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

install(EXPORT kasonTargets
    FILE kasonTargets.cmake
    NAMESPACE kason::
    DESTINATION "${KaSON_INSTALL_CMAKEDIR}"
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/kasonConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/kasonConfigVersion.cmake"
    DESTINATION "${KaSON_INSTALL_CMAKEDIR}"
)

install(FILES LICENSE README.md
    DESTINATION "${CMAKE_INSTALL_DOCDIR}"
)
install(FILES
    docs/index.md
    docs/api-guide.md
    docs/tutorial.md
    docs/compatibility.md
    DESTINATION "${CMAKE_INSTALL_DOCDIR}/docs"
)
install(FILES
    docs/assets/kason-logo.png
    docs/assets/kason-logo-dark.png
    DESTINATION "${CMAKE_INSTALL_DOCDIR}/docs/assets"
)
install(FILES
    examples/regular.c
    examples/stream.c
    examples/schema.c
    examples/schema_config.c
    DESTINATION "${CMAKE_INSTALL_DOCDIR}/examples"
)
install(FILES CHANGELOG.md CONTRIBUTING.md
    DESTINATION "${CMAKE_INSTALL_DOCDIR}"
)

if(BUILD_TESTING AND KaSON_BUILD_TESTS)
    add_executable(test_kason tests/test_kason.c)
    target_link_libraries(test_kason PRIVATE kason::kason)
    kason_enable_warnings(test_kason)
    add_test(NAME kason COMMAND test_kason)

    add_executable(test_stream tests/test_stream.c)
    target_link_libraries(test_stream PRIVATE kason::kason)
    kason_enable_warnings(test_stream)
    add_test(NAME stream COMMAND test_stream)

    add_executable(test_schema tests/test_schema.c)
    target_link_libraries(test_schema PRIVATE kason::schema)
    kason_enable_warnings(test_schema)
    add_test(NAME schema COMMAND test_schema)

    add_executable(test_conformance tests/test_conformance.c)
    target_link_libraries(test_conformance PRIVATE kason::kason)
    kason_enable_warnings(test_conformance)

    add_executable(test_fuzz_smoke
        tests/test_fuzz_smoke.c
        tests/fuzz_kason.c
    )
    target_link_libraries(test_fuzz_smoke PRIVATE kason::schema)
    kason_enable_warnings(test_fuzz_smoke)
    add_test(NAME fuzz.smoke COMMAND test_fuzz_smoke)
    set_tests_properties(fuzz.smoke PROPERTIES LABELS fuzz)

    file(GLOB KaSON_CONFORMANCE_VALID CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/conformance/valid/*.json"
    )
    foreach(case_file IN LISTS KaSON_CONFORMANCE_VALID)
        get_filename_component(case_name "${case_file}" NAME_WE)
        add_test(NAME "conformance.valid.${case_name}"
            COMMAND test_conformance valid "${case_file}"
        )
        set_tests_properties("conformance.valid.${case_name}"
            PROPERTIES LABELS conformance
        )
    endforeach()

    file(GLOB KaSON_CONFORMANCE_INVALID CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/conformance/invalid/*.json"
    )
    foreach(case_file IN LISTS KaSON_CONFORMANCE_INVALID)
        get_filename_component(case_name "${case_file}" NAME_WE)
        add_test(NAME "conformance.invalid.${case_name}"
            COMMAND test_conformance invalid "${case_file}"
        )
        set_tests_properties("conformance.invalid.${case_name}"
            PROPERTIES LABELS conformance
        )
    endforeach()

    add_executable(test_safety tests/test_safety.c)
    target_link_libraries(test_safety PRIVATE kason::kason)
    kason_enable_warnings(test_safety)

    set(KaSON_SAFETY_TESTS
        null-input
        get-value-scalar
        truncated-unicode
        bounded-whitespace
        get-value-null-key
        get-value-null-output
        parse-array-null-output
        deep-nesting
    )
    foreach(test_name IN LISTS KaSON_SAFETY_TESTS)
        add_test(NAME "safety.${test_name}" COMMAND test_safety "${test_name}")
        set_tests_properties("safety.${test_name}" PROPERTIES LABELS safety)
    endforeach()
endif()

if(KaSON_BUILD_FUZZER)
    if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "KaSON_BUILD_FUZZER requires Clang with the libFuzzer runtime")
    endif()
    if(BUILD_SHARED_LIBS)
        message(FATAL_ERROR "KaSON_BUILD_FUZZER requires a static library build")
    endif()

    target_compile_options(kason PRIVATE
        -fsanitize=fuzzer-no-link,address,undefined
        -fno-omit-frame-pointer
    )
    target_compile_options(kason_schema PRIVATE
        -fsanitize=fuzzer-no-link,address,undefined
        -fno-omit-frame-pointer
    )

    add_executable(fuzz_kason tests/fuzz_kason.c)
    target_link_libraries(fuzz_kason PRIVATE kason::schema)
    target_compile_features(fuzz_kason PRIVATE c_std_99)
    target_compile_options(fuzz_kason PRIVATE
        -fsanitize=fuzzer,address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(fuzz_kason PRIVATE
        -fsanitize=fuzzer,address,undefined
        -fno-omit-frame-pointer
    )
    kason_enable_warnings(fuzz_kason)
endif()

if(KaSON_BUILD_EXAMPLES)
    foreach(example regular stream)
        add_executable(${example} examples/${example}.c)
        target_link_libraries(${example} PRIVATE kason::kason)
        kason_enable_warnings(${example})
    endforeach()

    add_executable(schema examples/schema.c)
    target_link_libraries(schema PRIVATE kason::schema)
    kason_enable_warnings(schema)

    add_executable(schema_config examples/schema_config.c)
    target_link_libraries(schema_config PRIVATE kason::schema)
    kason_enable_warnings(schema_config)

    foreach(example json_to_yaml json_pretty json_compact)
        add_executable(${example} examples/${example}.c examples/example_io.c)
        if(NOT example STREQUAL "json_to_yaml")
            target_sources(${example} PRIVATE examples/json_format_common.c)
        endif()
        target_link_libraries(${example} PRIVATE kason::kason)
        kason_enable_warnings(${example})
    endforeach()

    add_custom_target(examples
        DEPENDS regular stream schema schema_config
            json_to_yaml json_pretty json_compact
    )
endif()

if(KaSON_BUILD_BENCHMARK)
    enable_language(CXX)
    include(ExternalProject)
    if(KaSON_BENCHMARK_LTO)
        include(CheckIPOSupported)
        check_ipo_supported(RESULT KaSON_BENCHMARK_IPO_SUPPORTED
            OUTPUT KaSON_BENCHMARK_IPO_ERROR
            LANGUAGES C CXX
        )
        if(KaSON_BENCHMARK_IPO_SUPPORTED)
            message(STATUS "Benchmark Release LTO: enabled")
            set_property(TARGET kason kason_schema PROPERTY
                INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
            )
        else()
            message(STATUS "Benchmark Release LTO: unavailable (${KaSON_BENCHMARK_IPO_ERROR})")
        endif()
    else()
        message(STATUS "Benchmark Release LTO: disabled")
    endif()
    if(POLICY CMP0135)
        cmake_policy(SET CMP0135 NEW)
    endif()
    find_package(PkgConfig QUIET)

    message(STATUS "Benchmark kason: available (required)")
    message(STATUS "Benchmark cJSON: external v1.7.19")
    message(STATUS "Benchmark simdjson: external v4.6.4")
    message(STATUS "Benchmark json-c: external json-c-0.18-20240915")
    message(STATUS "Benchmark Jansson: external v2.14.1")
    message(STATUS "Benchmark yyjson: external 0.12.0")
    message(STATUS "Benchmark jsmn: external v1.1.0")
    message(STATUS "Benchmark Parson: external ba29f4e")

    if(PkgConfig_FOUND)
        pkg_check_modules(KaSON_BENCH_JSON_GLIB QUIET IMPORTED_TARGET json-glib-1.0)
    endif()
    if(PkgConfig_FOUND AND KaSON_BENCH_JSON_GLIB_FOUND)
        set(KaSON_BENCH_HAVE_JSON_GLIB ON)
        message(STATUS "Benchmark JSON-GLib: available")
    else()
        set(KaSON_BENCH_HAVE_JSON_GLIB OFF)
        message(STATUS "Benchmark JSON-GLib: MISSING (benchmark skipped)")
    endif()

    set(KaSON_BENCH_DEPS_ROOT "${CMAKE_CURRENT_BINARY_DIR}/benchmark-deps")
    set(KaSON_BENCH_LEGACY_CMAKE_ARGS)
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 4.0)
        list(APPEND KaSON_BENCH_LEGACY_CMAKE_ARGS
            -DCMAKE_POLICY_VERSION_MINIMUM=3.10
        )
    endif()

    function(kason_import_benchmark_static target external_target install_dir library_name)
        file(MAKE_DIRECTORY "${install_dir}/include")
        add_library(${target} STATIC IMPORTED)
        set_target_properties(${target} PROPERTIES
            IMPORTED_LOCATION
                "${install_dir}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}${library_name}${CMAKE_STATIC_LIBRARY_SUFFIX}"
            INTERFACE_INCLUDE_DIRECTORIES "${install_dir}/include"
        )
        add_dependencies(${target} ${external_target})
    endfunction()

    set(KaSON_BENCH_CJSON_INSTALL "${KaSON_BENCH_DEPS_ROOT}/cjson-install")
    ExternalProject_Add(benchmark_cjson
        PREFIX "${KaSON_BENCH_DEPS_ROOT}/cjson"
        INSTALL_DIR "${KaSON_BENCH_CJSON_INSTALL}"
        GIT_REPOSITORY https://github.com/DaveGamble/cJSON.git
        GIT_TAG v1.7.19
        GIT_SHALLOW TRUE
        UPDATE_DISCONNECTED TRUE
        CMAKE_ARGS
            ${KaSON_BENCH_LEGACY_CMAKE_ARGS}
            -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
            -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
            -DCMAKE_INSTALL_LIBDIR=lib
            -DBUILD_SHARED_LIBS=OFF
            -DENABLE_CJSON_TEST=OFF
            -DENABLE_CJSON_UTILS=OFF
        BUILD_BYPRODUCTS
            "${KaSON_BENCH_CJSON_INSTALL}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}cjson${CMAKE_STATIC_LIBRARY_SUFFIX}"
    )
    kason_import_benchmark_static(
        kason_bench_cjson benchmark_cjson "${KaSON_BENCH_CJSON_INSTALL}" cjson
    )
    if(NOT WIN32)
        set_property(TARGET kason_bench_cjson APPEND PROPERTY
            INTERFACE_LINK_LIBRARIES m
        )
    endif()

    set(KaSON_BENCH_SIMDJSON_INSTALL "${KaSON_BENCH_DEPS_ROOT}/simdjson-install")
    ExternalProject_Add(benchmark_simdjson
        PREFIX "${KaSON_BENCH_DEPS_ROOT}/simdjson"
        INSTALL_DIR "${KaSON_BENCH_SIMDJSON_INSTALL}"
        GIT_REPOSITORY https://github.com/simdjson/simdjson.git
        GIT_TAG v4.6.4
        GIT_SHALLOW TRUE
        UPDATE_DISCONNECTED TRUE
        CMAKE_ARGS
            -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
            -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
            -DCMAKE_INSTALL_LIBDIR=lib
            -DBUILD_SHARED_LIBS=OFF
            -DSIMDJSON_DEVELOPER_MODE=OFF
            -DSIMDJSON_ENABLE_THREADS=OFF
        BUILD_BYPRODUCTS
            "${KaSON_BENCH_SIMDJSON_INSTALL}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}simdjson${CMAKE_STATIC_LIBRARY_SUFFIX}"
    )
    kason_import_benchmark_static(
        kason_bench_simdjson benchmark_simdjson "${KaSON_BENCH_SIMDJSON_INSTALL}" simdjson
    )

    set(KaSON_BENCH_JSON_C_INSTALL "${KaSON_BENCH_DEPS_ROOT}/json-c-install")
    ExternalProject_Add(benchmark_json_c
        PREFIX "${KaSON_BENCH_DEPS_ROOT}/json-c"
        INSTALL_DIR "${KaSON_BENCH_JSON_C_INSTALL}"
        URL https://github.com/json-c/json-c/archive/refs/tags/json-c-0.18-20240915.tar.gz
        URL_HASH SHA256=3112c1f25d39eca661fe3fc663431e130cc6e2f900c081738317fba49d29e298
        CMAKE_ARGS
            -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
            -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
            -DCMAKE_INSTALL_LIBDIR=lib
            -DBUILD_SHARED_LIBS=OFF
            -DBUILD_STATIC_LIBS=ON
            -DBUILD_TESTING=OFF
            -DBUILD_APPS=OFF
            -DDISABLE_WERROR=ON
        BUILD_BYPRODUCTS
            "${KaSON_BENCH_JSON_C_INSTALL}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}json-c${CMAKE_STATIC_LIBRARY_SUFFIX}"
    )
    kason_import_benchmark_static(
        kason_bench_json_c benchmark_json_c "${KaSON_BENCH_JSON_C_INSTALL}" json-c
    )
    if(NOT WIN32)
        set_property(TARGET kason_bench_json_c APPEND PROPERTY
            INTERFACE_LINK_LIBRARIES m
        )
    endif()

    set(KaSON_BENCH_JANSSON_INSTALL "${KaSON_BENCH_DEPS_ROOT}/jansson-install")
    ExternalProject_Add(benchmark_jansson
        PREFIX "${KaSON_BENCH_DEPS_ROOT}/jansson"
        INSTALL_DIR "${KaSON_BENCH_JANSSON_INSTALL}"
        URL https://github.com/akheron/jansson/archive/refs/tags/v2.14.1.tar.gz
        URL_HASH SHA256=979210eaffdffbcf54cfc34d047fccde13f21b529a381df26db871d886f729a4
        CMAKE_ARGS
            ${KaSON_BENCH_LEGACY_CMAKE_ARGS}
            -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
            -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
            -DCMAKE_INSTALL_LIBDIR=lib
            -DJANSSON_BUILD_SHARED_LIBS=OFF
            -DJANSSON_EXAMPLES=OFF
            -DJANSSON_BUILD_DOCS=OFF
            -DJANSSON_WITHOUT_TESTS=ON
            -DJANSSON_INSTALL=ON
        BUILD_BYPRODUCTS
            "${KaSON_BENCH_JANSSON_INSTALL}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}jansson${CMAKE_STATIC_LIBRARY_SUFFIX}"
    )
    kason_import_benchmark_static(
        kason_bench_jansson benchmark_jansson "${KaSON_BENCH_JANSSON_INSTALL}" jansson
    )

    set(KaSON_BENCH_YYJSON_INSTALL "${KaSON_BENCH_DEPS_ROOT}/yyjson-install")
    ExternalProject_Add(benchmark_yyjson
        PREFIX "${KaSON_BENCH_DEPS_ROOT}/yyjson"
        INSTALL_DIR "${KaSON_BENCH_YYJSON_INSTALL}"
        URL https://github.com/ibireme/yyjson/archive/refs/tags/0.12.0.tar.gz
        URL_HASH SHA256=b16246f617b2a136c78d73e5e2647c6f1de1313e46678062985bdcf1f40bb75d
        CMAKE_ARGS
            -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
            -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
            -DCMAKE_INSTALL_LIBDIR=lib
            -DBUILD_SHARED_LIBS=OFF
            -DYYJSON_BUILD_TESTS=OFF
            -DYYJSON_BUILD_MISC=OFF
            -DYYJSON_BUILD_DOC=OFF
            -DYYJSON_INSTALL=ON
        BUILD_BYPRODUCTS
            "${KaSON_BENCH_YYJSON_INSTALL}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}yyjson${CMAKE_STATIC_LIBRARY_SUFFIX}"
    )
    kason_import_benchmark_static(
        kason_bench_yyjson benchmark_yyjson "${KaSON_BENCH_YYJSON_INSTALL}" yyjson
    )

    set(KaSON_BENCH_JSMN_SOURCE "${KaSON_BENCH_DEPS_ROOT}/jsmn/src/benchmark_jsmn")
    ExternalProject_Add(benchmark_jsmn
        PREFIX "${KaSON_BENCH_DEPS_ROOT}/jsmn"
        SOURCE_DIR "${KaSON_BENCH_JSMN_SOURCE}"
        GIT_REPOSITORY https://github.com/zserge/jsmn.git
        GIT_TAG v1.1.0
        GIT_SHALLOW TRUE
        UPDATE_DISCONNECTED TRUE
        CONFIGURE_COMMAND ""
        BUILD_COMMAND ""
        INSTALL_COMMAND ""
    )

    set(KaSON_BENCH_PARSON_INSTALL "${KaSON_BENCH_DEPS_ROOT}/parson-install")
    ExternalProject_Add(benchmark_parson
        PREFIX "${KaSON_BENCH_DEPS_ROOT}/parson"
        INSTALL_DIR "${KaSON_BENCH_PARSON_INSTALL}"
        URL https://github.com/kgabis/parson/archive/ba29f4eda9ea7703a9f6a9cf2b0532a2605723c3.tar.gz
        URL_HASH SHA256=d311cd3d051965485bff1c89a3980df7ad0ee3f9c0f33c18964d8c1cc32ef2cf
        CMAKE_ARGS
            ${KaSON_BENCH_LEGACY_CMAKE_ARGS}
            -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
            -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
            -DCMAKE_INSTALL_LIBDIR=lib
            -DBUILD_SHARED_LIBS=OFF
        BUILD_BYPRODUCTS
            "${KaSON_BENCH_PARSON_INSTALL}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}parson${CMAKE_STATIC_LIBRARY_SUFFIX}"
    )
    kason_import_benchmark_static(
        kason_bench_parson benchmark_parson "${KaSON_BENCH_PARSON_INSTALL}" parson
    )

    add_executable(bench_kason bench/bench_kason.cpp)
    target_compile_features(bench_kason PRIVATE cxx_std_17)
    set_target_properties(bench_kason PROPERTIES CXX_EXTENSIONS OFF)
    if(KaSON_BENCHMARK_IPO_SUPPORTED)
        set_property(TARGET bench_kason PROPERTY
            INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
        )
    endif()
    target_link_libraries(bench_kason PRIVATE kason::schema)
    target_compile_definitions(bench_kason PRIVATE
        KaSON_BENCH_CJSON
        KaSON_BENCH_SIMDJSON
        KaSON_BENCH_HAVE_JSON_C
        KaSON_BENCH_HAVE_JANSSON
        KaSON_BENCH_HAVE_YYJSON
        KaSON_BENCH_HAVE_JSMN
        KaSON_BENCH_HAVE_PARSON
    )
    target_link_libraries(bench_kason PRIVATE
        kason_bench_cjson
        kason_bench_simdjson
    )
    if(KaSON_BENCH_HAVE_JSON_GLIB)
        target_compile_definitions(bench_kason PRIVATE KaSON_BENCH_JSON_GLIB)
        target_link_libraries(bench_kason PRIVATE PkgConfig::KaSON_BENCH_JSON_GLIB)
    endif()
    kason_enable_warnings(bench_kason)

    function(kason_add_external_benchmark target parser_definition)
        add_executable(${target}
            bench/bench_external.cpp
            bench/bench_external_parser.cpp
        )
        target_compile_definitions(${target} PRIVATE ${parser_definition})
        target_compile_features(${target} PRIVATE cxx_std_17)
        set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)
        if(KaSON_BENCHMARK_IPO_SUPPORTED)
            set_property(TARGET ${target} PROPERTY
                INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
            )
        endif()
        kason_enable_warnings(${target})
    endfunction()

    kason_add_external_benchmark(bench_json_c KaSON_BENCH_JSON_C)
    target_include_directories(bench_json_c PRIVATE
        "${KaSON_BENCH_JSON_C_INSTALL}/include/json-c"
    )
    target_link_libraries(bench_json_c PRIVATE kason_bench_json_c)

    kason_add_external_benchmark(bench_jansson KaSON_BENCH_JANSSON)
    target_link_libraries(bench_jansson PRIVATE kason_bench_jansson)

    kason_add_external_benchmark(bench_yyjson KaSON_BENCH_YYJSON)
    target_link_libraries(bench_yyjson PRIVATE kason_bench_yyjson)

    kason_add_external_benchmark(bench_jsmn KaSON_BENCH_JSMN)
    target_compile_definitions(bench_jsmn PRIVATE JSMN_PARENT_LINKS)
    target_include_directories(bench_jsmn PRIVATE "${KaSON_BENCH_JSMN_SOURCE}")
    add_dependencies(bench_jsmn benchmark_jsmn)

    kason_add_external_benchmark(bench_parson KaSON_BENCH_PARSON)
    target_link_libraries(bench_parson PRIVATE kason_bench_parson)

    add_dependencies(bench_kason
        bench_json_c
        bench_jansson
        bench_yyjson
        bench_jsmn
        bench_parson
    )
endif()
