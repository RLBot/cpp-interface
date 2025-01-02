cmake_minimum_required(VERSION 3.22)

project(ExampleBot VERSION 1.0.0)

###########################################################################
add_executable(${PROJECT_NAME})

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_20)

target_sources(${PROJECT_NAME} PRIVATE
	ExampleBot.cpp
	ExampleBot.h
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

###########################################################################
add_executable(${PROJECT_NAME}-Launcher)

target_compile_features(${PROJECT_NAME}-Launcher PRIVATE cxx_std_20)

target_sources(${PROJECT_NAME}-Launcher PRIVATE
	launcher.cpp
)

target_link_libraries(${PROJECT_NAME}-Launcher PRIVATE RLBotCPP-static)

if(LTO_SUPPORTED)
	set_target_properties(${PROJECT_NAME}-Launcher PROPERTIES
		INTERPROCEDURAL_OPTIMIZATION $<BOOL:${RLBOT_CPP_ENABLE_LTO}>
		INTERPROCEDURAL_OPTIMIZATION_DEBUG FALSE
	)
endif()

add_custom_command(TARGET ${PROJECT_NAME}-Launcher POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_if_different  $<TARGET_FILE:${PROJECT_NAME}-Launcher> ${CMAKE_CURRENT_SOURCE_DIR}
)
