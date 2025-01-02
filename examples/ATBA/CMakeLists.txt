cmake_minimum_required(VERSION 3.22)

project(ATBA VERSION 1.0.0)

###########################################################################
add_executable(${PROJECT_NAME})

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_20)

target_sources(${PROJECT_NAME} PRIVATE
	ATBA.cpp
	ATBA.h
	main.cpp
)

if(LTO_SUPPORTED)
	set_target_properties(${PROJECT_NAME} PROPERTIES
		INTERPROCEDURAL_OPTIMIZATION $<BOOL:${RLBOT_CPP_ENABLE_LTO}>
		INTERPROCEDURAL_OPTIMIZATION_DEBUG FALSE
	)
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE RLBotCPP-static)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_if_different  $<TARGET_FILE:${PROJECT_NAME}> ${CMAKE_CURRENT_SOURCE_DIR}
)
