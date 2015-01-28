set(CMAKE_BUILD_TYPE Debug)

find_package(Threads REQUIRED)
#find_package(GTest REQUIRED)
#find_package(GTest)

#if(NOT GTEST_FOUND)
#  #message(FATAL_ERROR "GTest/GMock was not found")
#  message("GTest/GMock was not found")
#  return()
#endif()

include_directories(${GTEST_INCLUDE_DIRS})

set(GMOCK_LIBRARIES "gmock")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -W -Wall")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O0") # debug, no optimisation
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --coverage") # enabling coverage

# adding source to test executable
add_executable(tests
	test.cpp # main
)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../lib//)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../res_config_redis/)

target_link_libraries(tests ${GTEST_BOTH_LIBRARIES})
target_link_libraries(tests ${GMOCK_LIBRARIES})
target_link_libraries(tests ${CMAKE_THREAD_LIBS_INIT})
add_test(AllTests tests)

