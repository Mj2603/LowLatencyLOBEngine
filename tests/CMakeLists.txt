add_executable(test_engine
    engine_test.cpp
)
target_link_libraries(test_engine PRIVATE lob_engine_lib)

add_executable(test_differential
    differential_test.cpp
)
target_link_libraries(test_differential PRIVATE lob_engine_lib)

foreach(test_target test_engine test_differential)
    target_compile_options(${test_target} PRIVATE
        $<$<CXX_COMPILER_ID:GNU>:-Wall;-Wextra;-Wpedantic>
        $<$<CXX_COMPILER_ID:Clang>:-Wall;-Wextra;-Wpedantic>
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
    )
endforeach()

add_test(NAME engine_test COMMAND test_engine)
add_test(NAME differential_test COMMAND test_differential)
