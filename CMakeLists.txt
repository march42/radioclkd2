cmake_minimum_required(VERSION 3.13.4)
project(radioclkd2 VERSION 0.8)
add_compile_definitions(CMAKE_PROJECT_VERSION="${CMAKE_PROJECT_VERSION}")

set(CMAKE_C_STANDARD 11)
add_compile_definitions(HAVE_STRCASECMP=0)
add_compile_definitions(HAVE_STRICMP=0)

add_compile_definitions(HAVE_ALARM=1)

add_compile_definitions(HAVE_SYS_TIMEPPS_H=1)
add_compile_definitions(HAVE_DECL_TIOCMIWAIT=0)

add_compile_definitions(HAVE_MLOCKALL=1)
add_compile_definitions(HAVE_SYS_MMAN_H=1)

add_compile_definitions(ENABLE_GPIO=1)
add_compile_definitions(ENABLE_GPIO_CHARDEV=1)

add_compile_definitions(HAVE_SCHED_H=1)
add_compile_definitions(HAVE_SCHED_GET_PRIORITY_LEVEL=1)
add_compile_definitions(HAVE_SCHED_SETSCHEDULER=1)

add_executable(radioclkd2
#        src/test.c
        src/main.c src/memory.c src/logger.c
        src/serial.c src/clock.c src/shm.c
        src/settings.c src/utctime.c
        src/decode_msf.c src/decode_dcf77.c src/decode_wwvb.c
        src/config.h src/memory.h src/logger.h src/systime.h
        src/serial.h src/timef.h src/clock.h src/shm.h src/settings.h
        src/utctime.h src/decode_msf.h src/decode_dcf77.h src/decode_wwvb.h)

# For debugging
add_definitions(-g3)
add_definitions(-ggdb)
add_definitions(-fno-omit-frame-pointer)

# Enable optimizations
add_definitions(-O3)
add_definitions(-march=native)
add_definitions(-mtune=native)

# Enable warnings
add_definitions(-Wall)
add_definitions(-Wextra)
add_definitions(-Wconversion)
add_definitions(-Wsign-conversion)
add_definitions(-Winit-self)
add_definitions(-Wparentheses)
add_definitions(-Wpointer-arith)

# Safety
add_definitions(-fPIE)
add_definitions(-fstack-protector-all)
add_definitions(-fstack-clash-protection)

# Sanitizers
#add_definitions(-fsanitize=address)
#add_definitions(-fsanitize=thread)
#target_link_libraries(radioclkd2 asan)
#target_link_libraries(radioclkd2 tsan)

target_link_libraries(radioclkd2 m)
target_link_libraries(radioclkd2 gpiod)