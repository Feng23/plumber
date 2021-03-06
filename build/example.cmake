#Build the examples
if(NOT "${build_examples}" STREQUAL "no")
	file(GLOB examples RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/${EXAMPLE_DIR} "${CMAKE_CURRENT_SOURCE_DIR}/${EXAMPLE_DIR}/*")
	foreach(example in ${examples})
		if(IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${EXAMPLE_DIR}/${example})
			if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${EXAMPLE_DIR}/${example}/build.cmake)
				set(SOURCE "")
				set(LOCAL_CFLAGS )
				set(LOCAL_LIBS )
				set(TARGET_PREFIX ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${EXAMPLE_DIR}/${example}/)
				set(SOURCE_PATH   ${CMAKE_CURRENT_SOURCE_DIR}/${EXAMPLE_DIR}/${example})
				file(MAKE_DIRECTORY ${TARGET_PREFIX})
				include(${SOURCE_PATH}/build.cmake)
				aux_source_directory(${SOURCE_PATH} SOURCE)
				set_source_files_properties(${SOURCE} PROPERTIES COMPILE_FLAGS "${CFLAGS} ${LOCAL_CFLAGS}")
				add_executable(${example} ${SOURCE})
				target_link_libraries(${example} ${LOCAL_LIBS} ${GLOBAL_LIBS})
				target_include_directories(${example} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/${EXAMPLE_DIR}/${example}/include)
				set_target_properties(${example} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${TARGET_PREFIX})
			endif(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${EXAMPLE_DIR}/${example}/build.cmake)
		endif(IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${EXAMPLE_DIR}/${example})
	endforeach(example in ${examples})
endif(NOT "${build_examples}" STREQUAL "no")
