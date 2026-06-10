cmake_minimum_required(VERSION 3.16)

project(nanoev VERSION 0.1.0 LANGUAGES C)

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

option(NANOEV_BUILD_TESTS "Build nanoev example test programs" ON)

set(NANOEV_COMMON_SOURCES
    source/nanoev_lib.c
    source/nanoev_event.c
    source/nanoev_timer.c
    source/nanoev_misc.c
    source/nanoev_dns.c
    source/nanoev_tcp.c
    source/nanoev_udp.c
    source/nanoev_async.c
    source/nanoev_loop.c
    source/nanoev_poller.c
)

if(WIN32)
    list(APPEND NANOEV_PLATFORM_SOURCES
        source/nanoev_internal_windows.c
        source/nanoev_poller_iocp.c
    )
elseif(APPLE)
    list(APPEND NANOEV_PLATFORM_SOURCES
        source/nanoev_internal_unix.c
        source/nanoev_poller_kqueue.c
    )
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND NANOEV_PLATFORM_SOURCES
        source/nanoev_internal_unix.c
        source/nanoev_poller_epoll.c
    )
else()
    message(FATAL_ERROR "nanoev currently supports Windows, macOS, and Linux")
endif()

add_library(nanoev STATIC
    ${NANOEV_COMMON_SOURCES}
    ${NANOEV_PLATFORM_SOURCES}
)
add_library(nanoev::nanoev ALIAS nanoev)

target_include_directories(nanoev
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

set_target_properties(nanoev PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS YES
)

if(WIN32)
    target_link_libraries(nanoev PUBLIC ws2_32)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(nanoev PUBLIC Threads::Threads)
endif()

install(TARGETS nanoev
    EXPORT nanoevTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(FILES
    include/nanoev.h
    include/nanoev.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(EXPORT nanoevTargets
    FILE nanoevTargets.cmake
    NAMESPACE nanoev::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/nanoev
)

configure_package_config_file(
    cmake/nanoevConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/nanoevConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/nanoev
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/nanoevConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/nanoevConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/nanoevConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/nanoev
)

if(NANOEV_BUILD_TESTS)
    enable_testing()

    add_executable(nanoev_test
        test/nanoev_test/main.c
        test/nanoev_test/addr_test.c
        test/nanoev_test/async_test.c
        test/nanoev_test/dns_test.c
        test/nanoev_test/event_test.c
        test/nanoev_test/loop_test.c
        test/nanoev_test/tcp_test.c
        test/nanoev_test/udp_test.c
        test/nanoev_test/thread_test.c
        test/nanoev_test/timer_test.c
    )
    target_link_libraries(nanoev_test PRIVATE nanoev)
    add_test(NAME nanoev_test COMMAND nanoev_test)

    add_executable(udp_test test/udp_test/udp_test.c)
    target_link_libraries(udp_test PRIVATE nanoev)

    add_executable(test_client test/test_client/test_client.c)
    target_link_libraries(test_client PRIVATE nanoev)

    add_executable(test_server test/test_server/test_server.c)
    target_link_libraries(test_server PRIVATE nanoev)
endif()
