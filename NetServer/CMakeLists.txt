if(OSVRRM_HAVE_D3D11_SUPPORT)
    set(COMMON_D3D_SOURCES
        ../examples/D3DCube.h
        ../examples/D3DSimpleShader.h
        ../examples/pixelshader3d.h
        ../examples/vertexshader3d.h)
    set(COMMON_D3D_2D_SOURCES
        ../examples/pixelshader.h
        ../examples/vertexshader.h)

    include (GenerateExportHeader)
    
	# D3D Example program using Present mode, with a networked client
	add_executable(RM_NetServer RM_NetServer.cpp MRI_Wrapper.cpp NetMessages.h)
	target_link_libraries(RM_NetServer PRIVATE osvrRM::osvrRenderManagerCpp)
    
    set(RM_NetClient_PUBLIC_HEADERS RM_NetClient.h "${CMAKE_CURRENT_BINARY_DIR}/NetServer/RM_NetClient_Export.h")
    add_library(RM_NetClient RM_NetClient.cpp "${RM_NetClient_PUBLIC_HEADERS}" NetMessages.h)
    target_include_directories(RM_NetClient PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
    target_link_libraries(RM_NetClient PRIVATE osvrRM::osvrRenderManagerCpp)
    generate_export_header(RM_NetClient
        BASE_NAME OSVR_RENDERMANAGER_NETCLIENT
        EXPORT_FILE_NAME NetServer/RM_NetClient_Export.h)

    set_property(TARGET RM_NetClient
        PROPERTY PUBLIC_HEADER "${RM_NetClient_PUBLIC_HEADERS}")
    
    # D3D Example program using Present mode
	add_executable(RM_NetClientExample RM_NetClientExample.cpp ${COMMON_D3D_SOURCES})
	target_link_libraries(RM_NetClientExample PRIVATE RM_NetClient osvrRM::osvrRenderManagerCpp D3D11)

	if(OSVRRM_INSTALL_EXAMPLES)
		install(
            TARGETS RM_NetClient
            EXPORT RM_NetClient
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
            PUBLIC_HEADER DESTINATION include/NetServer
            )
        install(
            TARGETS
                RM_NetServer                
                RM_NetClientExample
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            )
        install(EXPORT
            RM_NetClient
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/RM_NetClient
            FILE RM_NetClientConfig.cmake
        )
	endif()
endif()
