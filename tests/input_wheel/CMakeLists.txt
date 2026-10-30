# input_wheel - a racing wheel as a port-0 pad (src/input/wheel_pad.h): the
# binding text, the steering and pedal arithmetic, and the merge into port 0
# (src/input/input_core.c) against a fake backend and a fake reader. No SDL,
# no device, so it builds and runs on every host:
#   cmake --build <build> --target input_wheel_test && ctest -R input_wheel

set(INPUT_SRC ${CMAKE_CURRENT_LIST_DIR}/../../src/input)
add_executable(input_wheel_test
    test_main.c
    ${INPUT_SRC}/input_map.c
    ${INPUT_SRC}/input_core.c
    ${INPUT_SRC}/rumble_curve.c
    ${INPUT_SRC}/pad_glyphs.c
    ${INPUT_SRC}/keyboard.c
    ${INPUT_SRC}/touch_pad.c)
target_include_directories(input_wheel_test PRIVATE ${INPUT_SRC})
target_link_libraries(input_wheel_test PRIVATE platform)
if(NOT WIN32)
    target_link_libraries(input_wheel_test PRIVATE m)
endif()
enable_testing()
add_test(NAME input_wheel COMMAND input_wheel_test)
