cmake_minimum_required(VERSION 3.10)

project(EmbedProjectSmartRetail LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(retail_core
    src/answer_engine.cpp
    src/cloud_asr.cpp
    src/cloud_llm.cpp
    src/cloud_tts.cpp
    src/catalog.cpp
    src/cart.cpp
    src/microphone_driver.cpp
    src/payment.cpp
    src/recognizer.cpp
    src/voice.cpp
)

target_include_directories(retail_core PUBLIC include)

add_executable(embed_project src/main.cpp)
target_link_libraries(embed_project PRIVATE retail_core)

enable_testing()

add_executable(test_core tests/test_core.cpp)
target_link_libraries(test_core PRIVATE retail_core)

add_test(NAME retail_core_tests COMMAND test_core)
