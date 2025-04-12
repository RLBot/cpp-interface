cmake_minimum_required(VERSION 3.22)

project(RLBotCPP VERSION 2.0.0)

option(RLBOT_CPP_ENABLE_LTO "Enable RLBotCPP link-time optimization" ON)
option(RLBOT_CPP_ENABLE_TRACY "Enable tracy profiler" OFF)

include(CheckIPOSupported)
check_ipo_supported(RESULT LTO_SUPPORTED)

include(FetchContent)

FetchContent_Declare(flatbuffers
	GIT_REPOSITORY https://github.com/google/flatbuffers.git
	GIT_TAG v25.2.10
)
FetchContent_Populate(flatbuffers)

execute_process(COMMAND
	${CMAKE_COMMAND}
		-B ${flatbuffers_BINARY_DIR}
		-S ${flatbuffers_SOURCE_DIR}
		-DFLATBUFFERS_CODE_COVERAGE=OFF
		-DFLATBUFFERS_BUILD_TESTS=OFF
		-DFLATBUFFERS_INSTALL=OFF
		-DFLATBUFFERS_BUILD_FLATLIB=OFF
		-DFLATBUFFERS_BUILD_FLATC=ON
		-DFLATBUFFERS_STATIC_FLATC=OFF
		-DFLATBUFFERS_BUILD_FLATHASH=OFF
		-DFLATBUFFERS_BUILD_BENCHMARKS=OFF
		-DFLATBUFFERS_BUILD_GRPCTEST=OFF
		-DFLATBUFFERS_BUILD_SHAREDLIB=OFF
		-DFLATBUFFERS_LIBCXX_WITH_CLANG=OFF
		-DFLATBUFFERS_CODE_SANITIZE=OFF
		-DFLATBUFFERS_PACKAGE_REDHAT=OFF
		-DFLATBUFFERS_PACKAGE_DEBIAN=OFF
		-DFLATBUFFERS_BUILD_CPP17=ON
		-DFLATBUFFERS_BUILD_LEGACY=OFF
		-DFLATBUFFERS_ENABLE_PCH=OFF
		-DFLATBUFFERS_SKIP_MONSTER_EXTRA=ON
		-DFLATBUFFERS_STRICT_MODE=OFF
)
execute_process(COMMAND
	${CMAKE_COMMAND}
		--build ${flatbuffers_BINARY_DIR}
		--target flatc
)

FetchContent_Declare(flatbuffers_schema
	GIT_REPOSITORY https://github.com/RLBot/flatbuffers-schema.git
	GIT_TAG 45edf765638cd54de9c5631ecbf0a906088430e9
)
FetchContent_Populate(flatbuffers_schema)

FetchContent_Declare(tracy
	GIT_REPOSITORY https://github.com/wolfpld/tracy.git
	GIT_TAG v0.11.1
)
FetchContent_Populate(tracy)

if(LINUX OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
	include(ProcessorCount)
	ProcessorCount(NPROC)

	FetchContent_Declare(liburing
		GIT_REPOSITORY https://github.com/axboe/liburing.git
		GIT_TAG liburing-2.7
	)
	FetchContent_Populate(liburing)

	execute_process(
		COMMAND ./configure
		WORKING_DIRECTORY ${liburing_SOURCE_DIR}
	)

	execute_process(
		COMMAND make -j ${NPROC} library
		WORKING_DIRECTORY ${liburing_SOURCE_DIR}
	)
endif()

find_program(FLATC flatc REQUIRED NO_DEFAULT_PATH PATHS ${flatbuffers_BINARY_DIR})

add_custom_command(
	OUTPUT
		${CMAKE_CURRENT_BINARY_DIR}/rlbot_generated.h
	COMMAND
		${FLATC} --cpp --gen-object-api --gen-all --cpp-std c++17 ${flatbuffers_schema_SOURCE_DIR}/rlbot.fbs
	DEPENDS
		${flatbuffers_schema_SOURCE_DIR}/comms.fbs
		${flatbuffers_schema_SOURCE_DIR}/gamedata.fbs
		${flatbuffers_schema_SOURCE_DIR}/gamestatemanip.fbs
		${flatbuffers_schema_SOURCE_DIR}/matchconfig.fbs
		${flatbuffers_schema_SOURCE_DIR}/rlbot.fbs
		${flatbuffers_schema_SOURCE_DIR}/rendering.fbs
)

add_custom_target(rlbot-generated DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/rlbot_generated.h)

add_subdirectory(library)
add_subdirectory(benchmark)
add_subdirectory(examples/ATBA)
add_subdirectory(examples/ExampleBot)
