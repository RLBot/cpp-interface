cmake_minimum_required(VERSION 3.22)

project(RLBotCPP VERSION 1.0.0)

add_library(${PROJECT_NAME} SHARED)
add_library(${PROJECT_NAME}-static STATIC)

foreach(TARGET ${PROJECT_NAME} ${PROJECT_NAME}-static)
	target_sources(${TARGET} PRIVATE
		include/rlbot/Bot.h
		include/rlbot/BotManager.h
		include/rlbot/RLBotCPP.h

		Bot.cpp
		BotContext.cpp
		BotContext.h
		BotManager.cpp
		BotManagerImpl.cpp
		BotManagerImpl.h
		Log.cpp
		Log.h
		Message.cpp
		Message.h
		Pool.cpp
		Pool.h
		Socket.cpp
		Socket.h

		$<$<BOOL:${WIN32}>:WsaData.cpp>
		$<$<BOOL:${WIN32}>:WsaData.h>
	)

	target_compile_features(${TARGET} PRIVATE cxx_std_20)

	set_target_properties(${TARGET} PROPERTIES
		C_VISIBILITY_PRESET hidden
		CXX_VISIBILITY_PRESET hidden
		SOVERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
		VERSION ${PROJECT_VERSION}
		DEBUG_POSTFIX d
	)

	if(WIN32)
		# attach version number to output name
		set_target_properties(${PROJECT_NAME} PROPERTIES
			OUTPUT_NAME ${PROJECT_NAME}-${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
		)
	endif()

	target_include_directories(${TARGET} PUBLIC include)

	target_include_directories(${TARGET} SYSTEM PUBLIC
		${flatbuffers_SOURCE_DIR}/include
		${tracy_SOURCE_DIR}/public
	)

	add_dependencies(${TARGET} rlbot-generated)
	target_include_directories(${TARGET} SYSTEM PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/..)

	if(LINUX)
		target_include_directories(${TARGET} SYSTEM PRIVATE ${liburing_SOURCE_DIR}/src/include)
		target_link_libraries(${TARGET} PRIVATE ${liburing_SOURCE_DIR}/src/liburing.a)
	endif()

	if(WIN32)
		target_link_libraries(${TARGET} PRIVATE wsock32 ws2_32)
		target_compile_definitions(${TARGET}
			PRIVATE
				_CRT_SECURE_NO_WARNINGS
				_WIN32_WINNT=_WIN32_WINNT_WINBLUE
		)
	endif()

	if(MSVC)
		# standard definitions to enable
		target_compile_definitions(${TARGET} PRIVATE
			NOMINMAX                 # no min/max macros
			_CRT_SECURE_NO_DEPRECATE # don't deprecate functions in favor of _s versions
			WIN32_LEAN_AND_MEAN      # minimal headers
		)

		# standard compile options
		target_compile_options(${TARGET} PRIVATE
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
			target_compile_options(${TARGET} PRIVATE
				-no-integrated-cpp
			)
		elseif(("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang") AND ("${CMAKE_CXX_COMPILER_FRONTEND_VARIANT}" STREQUAL "GNU"))
			# make clang a little more like gcc's set of warnings
			target_compile_options(${TARGET} PRIVATE
				-Wno-unused-const-variable
				-Wno-deprecated-anon-enum-enum-conversion
				-Winconsistent-missing-override
				-Wno-error=inconsistent-missing-override
				-Winconsistent-missing-destructor-override
			)
		endif()

		# standard warnings to enable
		target_compile_options(${TARGET} PRIVATE
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
		target_compile_definitions(${TARGET} PUBLIC TRACY_ENABLE)
		target_sources(${TARGET} PRIVATE ${tracy_SOURCE_DIR}/public/TracyClient.cpp)
	endif()

	target_compile_definitions(${TARGET} PUBLIC
		FLATBUFFERS_USE_STD_OPTIONAL=1
		FLATBUFFERS_USE_STD_SPAN=1
	)

	if(LTO_SUPPORTED)
		set_target_properties(${TARGET} PROPERTIES
			INTERPROCEDURAL_OPTIMIZATION $<BOOL:${RLBOT_CPP_ENABLE_LTO}>
			INTERPROCEDURAL_OPTIMIZATION_DEBUG FALSE
		)
	endif()
endforeach()
