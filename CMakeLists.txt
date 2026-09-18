cmake_minimum_required(VERSION 3.20)

project(CustomModel VERSION 0.2.0 LANGUAGES CXX)

include(CTest)

option(CUSTOMMODEL_BUILD_CLIENT "Build the Windows x86 ASI runtime" ${WIN32})
option(CUSTOMMODEL_BUILD_OPENMP_COMPONENT "Build the open.mp server component" OFF)
option(CUSTOMMODEL_BUILD_WINHTTP_PROBE "Build the development-only WinHTTP probe" OFF)
set(CUSTOMMODEL_OMP_SDK_DIR "" CACHE PATH "Path to a pinned open.mp-sdk checkout")
set(CUSTOMMODEL_OMP_NETWORK_DIR "" CACHE PATH "Path to a pinned open.mp-network checkout")

add_library(custommodel_shared STATIC
    shared/src/assets.cpp
    shared/src/pe_image.cpp
    shared/src/protocol.cpp
    shared/src/sha256.cpp
    shared/src/transport_frame.cpp
    shared/src/version.cpp
)

target_include_directories(custommodel_shared
    PUBLIC
        shared/include
    PRIVATE
        third_party
)

target_compile_features(custommodel_shared PUBLIC cxx_std_17)
set_target_properties(custommodel_shared PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    target_compile_options(custommodel_shared PRIVATE /W4 /permissive-)
else()
    target_compile_options(custommodel_shared PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(custommodel_server_core STATIC
    server/src/asset_publication.cpp
    server/src/asset_registry.cpp
    server/src/handshake_endpoint.cpp
    server/src/manifest_config.cpp
    server/src/player_capabilities.cpp
    server/src/transport.cpp
)

target_include_directories(custommodel_server_core
    PUBLIC
        server/include
)

target_link_libraries(custommodel_server_core PUBLIC custommodel_shared)
target_compile_features(custommodel_server_core PUBLIC cxx_std_17)
set_target_properties(custommodel_server_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    target_compile_options(custommodel_server_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(custommodel_server_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(CUSTOMMODEL_BUILD_OPENMP_COMPONENT)
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 4.0 AND
       NOT DEFINED CMAKE_POLICY_VERSION_MINIMUM)
        set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
    endif()

    if(NOT TARGET OMP-SDK)
        if(NOT EXISTS "${CUSTOMMODEL_OMP_SDK_DIR}/CMakeLists.txt")
            message(FATAL_ERROR
                "CUSTOMMODEL_BUILD_OPENMP_COMPONENT requires CUSTOMMODEL_OMP_SDK_DIR "
                "to point at the pinned open.mp-sdk checkout."
            )
        endif()
        add_subdirectory(
            "${CUSTOMMODEL_OMP_SDK_DIR}"
            "${CMAKE_BINARY_DIR}/dependencies/openmp-sdk"
            EXCLUDE_FROM_ALL
        )
    endif()

    if(NOT TARGET OMP-Network)
        if(NOT EXISTS "${CUSTOMMODEL_OMP_NETWORK_DIR}/CMakeLists.txt")
            message(FATAL_ERROR
                "CUSTOMMODEL_BUILD_OPENMP_COMPONENT requires CUSTOMMODEL_OMP_NETWORK_DIR "
                "to point at the pinned open.mp-network checkout."
            )
        endif()
        add_subdirectory(
            "${CUSTOMMODEL_OMP_NETWORK_DIR}"
            "${CMAKE_BINARY_DIR}/dependencies/openmp-network"
            EXCLUDE_FROM_ALL
        )
    endif()

    add_library(CustomModelServer SHARED
        server/openmp/component.cpp
        server/openmp/entry.cpp
    )

    target_include_directories(CustomModelServer PRIVATE server/openmp)
    target_link_libraries(CustomModelServer PRIVATE
        custommodel_server_core
        OMP-SDK
        OMP-Network
    )
    target_compile_features(CustomModelServer PRIVATE cxx_std_17)

    if(MSVC)
        target_compile_options(CustomModelServer PRIVATE /W4 /permissive-)
    else()
        target_compile_options(CustomModelServer PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    set_target_properties(CustomModelServer PROPERTIES
        PREFIX ""
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/server/components"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/server/components"
    )
endif()

if(CUSTOMMODEL_BUILD_CLIENT)
    if(NOT WIN32)
        message(FATAL_ERROR
            "The CustomModel client is a Windows ASI. Use the i686 MinGW-w64 "
            "toolchain or configure with -DCUSTOMMODEL_BUILD_CLIENT=OFF."
        )
    endif()

    if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
        message(FATAL_ERROR
            "The CustomModel client must be built for Windows x86 (32-bit). "
            "The selected toolchain produces ${CMAKE_SIZEOF_VOID_P}-byte pointers."
        )
    endif()

    add_library(CustomModel SHARED
        client/src/assets/cache.cpp
        client/src/assets/manager.cpp
        client/src/assets/winhttp_fetch.cpp
        client/src/core/dllmain.cpp
        client/src/core/logger.cpp
        client/src/core/module.cpp
        client/src/core/runtime.cpp
        client/src/protocol/handshake.cpp
        client/src/protocol/transport.cpp
        client/src/samp/raknet_transport.cpp
        client/src/samp/compatibility.cpp
    )

    target_include_directories(CustomModel
        PRIVATE
            client/include
    )

    target_link_libraries(CustomModel PRIVATE custommodel_shared winhttp)
    target_compile_features(CustomModel PRIVATE cxx_std_17)
    target_compile_definitions(CustomModel PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        WINVER=0x0601
        _WIN32_WINNT=0x0601
    )

    if(MSVC)
        target_compile_options(CustomModel PRIVATE /W4 /permissive-)
    else()
        target_compile_options(CustomModel PRIVATE -Wall -Wextra -Wpedantic)
        target_link_options(CustomModel PRIVATE -static -static-libgcc -static-libstdc++)
    endif()

    set_target_properties(CustomModel PROPERTIES
        PREFIX ""
        SUFFIX ".asi"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )

    install(TARGETS CustomModel
        RUNTIME DESTINATION .
        LIBRARY DESTINATION .
    )
endif()

if(CUSTOMMODEL_BUILD_WINHTTP_PROBE)
    if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
        message(FATAL_ERROR
            "The WinHTTP probe requires the Windows x86 MinGW-w64 toolchain."
        )
    endif()

    add_executable(CustomModelWinHttpProbe
        client/src/assets/winhttp_fetch.cpp
        tools/winhttp_probe.cpp
    )
    target_include_directories(CustomModelWinHttpProbe PRIVATE client/include)
    target_link_libraries(CustomModelWinHttpProbe PRIVATE custommodel_shared winhttp)
    target_compile_features(CustomModelWinHttpProbe PRIVATE cxx_std_17)
    target_compile_definitions(CustomModelWinHttpProbe PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        WINVER=0x0601
        _WIN32_WINNT=0x0601
    )
    if(MSVC)
        target_compile_options(CustomModelWinHttpProbe PRIVATE /W4 /permissive-)
    else()
        target_compile_options(CustomModelWinHttpProbe PRIVATE -Wall -Wextra -Wpedantic)
        target_link_options(CustomModelWinHttpProbe PRIVATE
            -static
            -static-libgcc
            -static-libstdc++
        )
    endif()
    set_target_properties(CustomModelWinHttpProbe PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools"
    )
endif()

if(BUILD_TESTING)
    add_executable(custommodel_tests
        client/src/samp/compatibility.cpp
        tests/phase1_tests.cpp
    )

    target_include_directories(custommodel_tests PRIVATE client/include)
    target_link_libraries(custommodel_tests PRIVATE custommodel_shared)
    target_compile_features(custommodel_tests PRIVATE cxx_std_17)

    if(MSVC)
        target_compile_options(custommodel_tests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(custommodel_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(NAME custommodel_phase1 COMMAND custommodel_tests)

    add_executable(custommodel_protocol_tests
        client/src/protocol/handshake.cpp
        client/src/protocol/transport.cpp
        tests/protocol_tests.cpp
    )

    target_include_directories(custommodel_protocol_tests PRIVATE client/include)
    target_link_libraries(custommodel_protocol_tests PRIVATE custommodel_shared)
    target_compile_features(custommodel_protocol_tests PRIVATE cxx_std_17)

    add_executable(custommodel_server_tests
        client/src/protocol/handshake.cpp
        tests/server_state_tests.cpp
    )

    target_include_directories(custommodel_server_tests PRIVATE client/include)
    target_link_libraries(custommodel_server_tests PRIVATE custommodel_server_core)
    target_compile_features(custommodel_server_tests PRIVATE cxx_std_17)

    add_executable(custommodel_asset_tests
        client/src/assets/cache.cpp
        client/src/assets/manager.cpp
        tests/asset_tests.cpp
    )

    target_include_directories(custommodel_asset_tests PRIVATE client/include)
    target_link_libraries(custommodel_asset_tests PRIVATE
        custommodel_server_core
        custommodel_shared
    )
    target_compile_features(custommodel_asset_tests PRIVATE cxx_std_17)

    if(MSVC)
        target_compile_options(custommodel_protocol_tests PRIVATE /W4 /permissive-)
        target_compile_options(custommodel_server_tests PRIVATE /W4 /permissive-)
        target_compile_options(custommodel_asset_tests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(custommodel_protocol_tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(custommodel_server_tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(custommodel_asset_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(NAME custommodel_protocol COMMAND custommodel_protocol_tests)
    add_test(NAME custommodel_server_state COMMAND custommodel_server_tests)
    add_test(NAME custommodel_assets COMMAND custommodel_asset_tests)
endif()
