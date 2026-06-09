cmake_minimum_required(VERSION 3.16)

project(nanoev VERSION 0.1.0 LANGUAGES C)

option(NANOEV_BUILD_TESTS "Build nanoev example test programs" ON)

set(NANOEV_COMMON_SOURCES
    source/nanoev_lib.c
    source/nanoev_event.c
    source/nanoev_timer.c
    source/nanoev_misc.c
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

target_include_directories(nanoev
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

set_target_properties(nanoev PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS YES
)

if(WIN32)
    target_link_libraries(nanoev PUBLIC ws2_32)
endif()

if(NANOEV_BUILD_TESTS)
    enable_testing()

    add_executable(nanoev_test
        test/nanoev_test/main.c
        test/nanoev_test/addr_test.c
        test/nanoev_test/async_test.c
        test/nanoev_test/event_test.c
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
