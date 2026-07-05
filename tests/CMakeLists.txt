add_executable(test_engine
    engine_test.cpp
)

target_link_libraries(test_engine PRIVATE lob_engine_lib)

enable_testing()
add_test(NAME engine_test COMMAND test_engine)
