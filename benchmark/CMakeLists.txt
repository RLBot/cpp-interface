cmake_minimum_required(VERSION 3.22)

project(RLBotCPP-Benchmark VERSION 1.0.0)

FetchContent_Declare(RocketSim
	GIT_REPOSITORY https://github.com/ZealanL/RocketSim.git
	GIT_TAG 9b36a351d86fcb15fd69154c7943821658b16b09
)
FetchContent_MakeAvailable(RocketSim)

###########################################################################
add_executable(${PROJECT_NAME})

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_20)

target_sources(${PROJECT_NAME} PRIVATE
	../library/Log.cpp
	../library/Log.h
	../library/Message.cpp
	../library/Message.h
	../library/Pool.cpp
	../library/Pool.h
	$<$<BOOL:${WIN32}>:../library/WsaData.cpp>
	$<$<BOOL:${WIN32}>:../library/WsaData.h>

	main.cpp
	Simulator.cpp
	Simulator.h
)

target_link_libraries(${PROJECT_NAME} PRIVATE RLBotCPP-static RocketSim)

target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE
	../library
	${RocketSim_SOURCE_DIR}/src
)

add_dependencies(${PROJECT_NAME} rlbot-generated)
target_include_directories(${PROJECT_NAME} SYSTEM PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/..)

if(WIN32)
	target_link_libraries(${PROJECT_NAME} PRIVATE wsock32 ws2_32)
	target_compile_definitions(${PROJECT_NAME}
		PRIVATE
			_CRT_SECURE_NO_WARNINGS
			_WIN32_WINNT=_WIN32_WINNT_WINBLUE
	)
endif()

if(MSVC)
	# standard definitions to enable
	target_compile_definitions(${PROJECT_NAME} PRIVATE
		NOMINMAX                 # no min/max macros
		_CRT_SECURE_NO_DEPRECATE # don't deprecate functions in favor of _s versions
		WIN32_LEAN_AND_MEAN      # minimal headers
	)

	# standard compile options
	target_compile_options(${PROJECT_NAME} PRIVATE
		/utf-8           # utf-8
		/EHsc            # fix up exception handling model
		/W3              # level 3 warnings
		/WX              # warnings as errors
		/wd4251          # 'identifier' : class 'type' needs to have dll-interface
		/wd4267          # conversion integer type to smaller type
		/Zc:preprocessor # use c preprocessor
	)
else()
	if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
		# work around gcc _Pragma problem
		target_compile_options(${PROJECT_NAME} PRIVATE
			-no-integrated-cpp
		)
	elseif(("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang") AND ("${CMAKE_CXX_COMPILER_FRONTEND_VARIANT}" STREQUAL "GNU"))
		# make clang a little more like gcc's set of warnings
		target_compile_options(${PROJECT_NAME} PRIVATE
			-Wno-unused-const-variable
			-Wno-deprecated-anon-enum-enum-conversion
			-Winconsistent-missing-override
			-Wno-error=inconsistent-missing-override
			-Winconsistent-missing-destructor-override
		)
	endif()

	# standard warnings to enable
	target_compile_options(${PROJECT_NAME} PRIVATE
		-Wall
		-Wextra
		-Wno-unknown-pragmas
		-Wno-error=deprecated-declarations
		-Wsuggest-override
		-Wno-error=suggest-override
		-Wzero-as-null-pointer-constant
		-Wno-error=zero-as-null-pointer-constant
		-Wno-missing-field-initializers
	)
endif()

if(RLBOT_CPP_ENABLE_TRACY)
	target_compile_definitions(${PROJECT_NAME} PUBLIC TRACY_ENABLE)
	target_sources(${PROJECT_NAME} PRIVATE ${tracy_SOURCE_DIR}/public/TracyClient.cpp)
endif()

target_compile_definitions(${PROJECT_NAME} PUBLIC
	FLATBUFFERS_USE_STD_OPTIONAL=1
	FLATBUFFERS_USE_STD_SPAN=1
)

if(LTO_SUPPORTED)
	set_target_properties(${PROJECT_NAME} PROPERTIES
		INTERPROCEDURAL_OPTIMIZATION $<BOOL:${RLBOT_CPP_ENABLE_LTO}>
		INTERPROCEDURAL_OPTIMIZATION_DEBUG FALSE
	)
endif()

