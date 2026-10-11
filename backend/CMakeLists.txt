cmake_minimum_required(VERSION 3.16)

project(TaskFlowEngine LANGUAGES C CXX)

find_package(Threads REQUIRED)

include(FetchContent)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(HTTPLIB_USE_OPENSSL_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_ZLIB_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_BROTLI_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_INSTALL OFF CACHE BOOL "" FORCE)
set(HTTPLIB_TEST OFF CACHE BOOL "" FORCE)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(ENABLE_UNINSTALL OFF CACHE BOOL "" FORCE)
set(BUILD_VERSION "3.11.1" CACHE STRING "" FORCE)
set(MONGOCXX_OVERRIDE_DEFAULT_INSTALL_PREFIX OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
	cpp_httplib
	URL https://github.com/yhirose/cpp-httplib/archive/refs/tags/v0.18.3.tar.gz
	DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_Declare(
	nlohmann_json
	URL https://github.com/nlohmann/json/archive/refs/tags/v3.12.0.tar.gz
	DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_Declare(
	mongo_cxx_driver
	URL https://github.com/mongodb/mongo-cxx-driver/archive/refs/tags/r3.11.1.tar.gz
	DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(cpp_httplib nlohmann_json mongo_cxx_driver)

add_executable(taskflow_engine
	src/main.cpp
	src/Job.cpp
	src/JobQueue.cpp
	src/JobRegistry.cpp
	src/MongoJobRepository.cpp
	src/Worker.cpp
	src/WorkerPool.cpp
	src/Api.cpp
)

target_include_directories(taskflow_engine PRIVATE
	include
	${mongo_cxx_driver_SOURCE_DIR}/src/mongocxx/include/mongocxx/v_noabi
	${mongo_cxx_driver_SOURCE_DIR}/src/bsoncxx/include/bsoncxx/v_noabi
	${mongo_cxx_driver_BINARY_DIR}/src/mongocxx/lib/mongocxx/v_noabi
	${mongo_cxx_driver_BINARY_DIR}/src/bsoncxx/lib/bsoncxx/v_noabi
)
target_link_libraries(taskflow_engine PRIVATE
	Threads::Threads
	httplib::httplib
	nlohmann_json::nlohmann_json
	mongocxx_static
)

option(TASKFLOW_BUILD_TESTS "Build TaskFlow API tests" ON)
if(TASKFLOW_BUILD_TESTS)
	enable_testing()
	add_executable(taskflow_api_tests
		../tests/api_tests.cpp
		src/Api.cpp
		src/Job.cpp
		src/JobQueue.cpp
		src/JobRegistry.cpp
		src/Worker.cpp
		src/WorkerPool.cpp
	)
	target_include_directories(taskflow_api_tests PRIVATE include)
	target_link_libraries(taskflow_api_tests PRIVATE
		Threads::Threads
		httplib::httplib
		nlohmann_json::nlohmann_json
	)
	add_test(NAME taskflow_api_tests COMMAND taskflow_api_tests)
	set_tests_properties(taskflow_api_tests PROPERTIES TIMEOUT 10)
endif()