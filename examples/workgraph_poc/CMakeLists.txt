set(TARGET workgraph_poc)

add_executable(${TARGET} workgraph_poc.cpp)

target_link_libraries(${TARGET} base)

set_target_properties(${TARGET} PROPERTIES FOLDER "Examples")

if(MSVC)
	set_target_properties(${TARGET} PROPERTIES LINK_FLAGS "/ENTRY:mainCRTStartup")
endif()

if (RESOURCE_INSTALL_DIR)
	install(TARGETS ${TARGET} DESTINATION ${RESOURCE_INSTALL_DIR})
endif()
