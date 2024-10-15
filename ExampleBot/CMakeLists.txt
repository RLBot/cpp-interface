cmake_minimum_required(VERSION 3.22)

project(ExampleBot VERSION 1.0.0)

include(CheckIPOSupported)
check_ipo_supported(RESULT ENABLE_LTO)

###########################################################################
add_executable(${PROJECT_NAME})

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_20)

target_sources(${PROJECT_NAME} PRIVATE
	ExampleBot.cpp
	ExampleBot.h
	main.cpp
)

if(ENABLE_LTO)
	set_target_properties(${PROJECT_NAME} PROPERTIES
		INTERPROCEDURAL_OPTIMIZATION TRUE
		INTERPROCEDURAL_OPTIMIZATION_DEBUG FALSE
	)
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE RLBotCPP-static)

###########################################################################
add_executable(${PROJECT_NAME}-Launcher)

target_compile_features(${PROJECT_NAME}-Launcher PRIVATE cxx_std_20)

target_sources(${PROJECT_NAME}-Launcher PRIVATE
	launcher.cpp
)

target_link_libraries(${PROJECT_NAME}-Launcher PRIVATE RLBotCPP-static)

if(ENABLE_LTO)
	set_target_properties(${PROJECT_NAME}-Launcher PROPERTIES
		INTERPROCEDURAL_OPTIMIZATION TRUE
		INTERPROCEDURAL_OPTIMIZATION_DEBUG FALSE
	)
endif()


