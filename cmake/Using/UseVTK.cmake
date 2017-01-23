MACRO(USE_VTK)
  IF ((NOT VTK_FOUND) AND (${ARGC} LESS 1))
    USING_MESSAGE("Skipping because of missing VTK")
    RETURN()
  ENDIF((NOT VTK_FOUND) AND (${ARGC} LESS 1))
  IF(NOT VTK_USED AND VTK_FOUND)
    SET(VTK_USED TRUE)
    IF(MSVC)
	  IF(BASEARCHSUFFIX STREQUAL "zebu")
	  SET(VTK_VERSION "-7.1")
	  SET(VTK_LIBRARIES debug ${VTK_DIR}/../../vtkCommonDataModel${VTK_VERSION}d.lib optimized ${VTK_DIR}/../../vtkCommonDataModel${VTK_VERSION}.lib debug ${VTK_DIR}/../../vtkCommonExecutionModel${VTK_VERSION}d.lib optimized ${VTK_DIR}/../../vtkCommonExecutionModel${VTK_VERSION}.lib debug ${VTK_DIR}/../../vtkCommonCore${VTK_VERSION}d.lib optimized ${VTK_DIR}/../../vtkCommonCore${VTK_VERSION}.lib debug ${VTK_DIR}/../../vtkIOLegacy${VTK_VERSION}d.lib optimized ${VTK_DIR}/../../vtkIOLegacy${VTK_VERSION}.lib debug ${VTK_DIR}/../../vtkIOXML${VTK_VERSION}d.lib optimized ${VTK_DIR}/../../vtkIOXML${VTK_VERSION}.lib debug ${VTK_DIR}/../../vtkImagingCore${VTK_VERSION}d.lib optimized ${VTK_DIR}/../../vtkImagingCore${VTK_VERSION}.lib debug ${VTK_DIR}/../../vtkIOCore${VTK_VERSION}d.lib optimized ${VTK_DIR}/../../vtkIOCore${VTK_VERSION}.lib debug ${VTK_DIR}/../../vtkfreetype${VTK_VERSION}d.lib optimized ${VTK_DIR}/../../vtkfreetype${VTK_VERSION}.lib general opengl32)
	  ELSE()
	  SET(VTK_VERSION "-6.2")
	  SET(VTK_LIBRARIES debug ${VTK_DIR}/../../vtkCommonDataModel${VTK_VERSION}d.lib optimized ${VTK_DIR}/../../vtkCommonDataModel${VTK_VERSION}.lib debug ${VTK_DIR}/../../vtkCommonExecutionModel${VTK_VERSION}d.lib optimized ${VTK_DIR}/../../vtkCommonExecutionModel${VTK_VERSION}.lib debug ${VTK_DIR}/../../vtkCommonCore${VTK_VERSION}d.lib optimized ${VTK_DIR}/../../vtkCommonCore${VTK_VERSION}.lib debug ${VTK_DIR}/../../vtkIOLegacy${VTK_VERSION}d.lib optimized ${VTK_DIR}/../../vtkIOLegacy${VTK_VERSION}.lib debug ${VTK_DIR}/../../vtkIOXML${VTK_VERSION}d.lib optimized ${VTK_DIR}/../../vtkIOXML${VTK_VERSION}.lib debug ${VTK_DIR}/../../vtkImagingCore${VTK_VERSION}d.lib optimized ${VTK_DIR}/../../vtkImagingCore${VTK_VERSION}.lib debug ${VTK_DIR}/../../vtkIOCore${VTK_VERSION}d.lib optimized ${VTK_DIR}/../../vtkIOCore${VTK_VERSION}.lib debug ${VTK_DIR}/../../vtkftgl${VTK_VERSION}d.lib optimized ${VTK_DIR}/../../vtkftgl${VTK_VERSION}.lib debug ${VTK_DIR}/../../vtkfreetype${VTK_VERSION}d.lib optimized ${VTK_DIR}/../../vtkfreetype${VTK_VERSION}.lib general opengl32)
	  ENDIF()
      SET(VTK_LIBRARIESIO debug ${VTK_DIR}/../../vtkFiltersCore${VTK_VERSION}d.lib optimized ${VTK_DIR}/../../vtkFiltersCore${VTK_VERSION}.lib debug ${VTK_DIR}/../../vtkDICOMParser${VTK_VERSION}d.lib optimized ${VTK_DIR}/../../vtkDICOMParser${VTK_VERSION}.lib debug ${VTK_DIR}/../../vtkNetCDF${VTK_VERSION}d.lib optimized ${VTK_DIR}/../../vtkNetCDF${VTK_VERSION}.lib debug ${VTK_DIR}/../../vtkmetaio${VTK_VERSION}d.lib optimized ${VTK_DIR}/../../vtkmetaio${VTK_VERSION}.lib debug ${VTK_DIR}/../../vtksqlite${VTK_VERSION}d.lib optimized ${VTK_DIR}/../../vtksqlite${VTK_VERSION}.lib debug ${VTK_DIR}/../../vtkpng${VTK_VERSION}d.lib optimized ${VTK_DIR}/../../vtkpng${VTK_VERSION}.lib debug ${VTK_DIR}/../../vtkzlib${VTK_VERSION}d.lib optimized ${VTK_DIR}/../../vtkzlib${VTK_VERSION}.lib debug ${VTK_DIR}/../../vtkjpeg${VTK_VERSION}d.lib optimized ${VTK_DIR}/../../vtkjpeg${VTK_VERSION}.lib debug ${VTK_DIR}/../../vtktiff${VTK_VERSION}d.lib optimized ${VTK_DIR}/../../vtktiff${VTK_VERSION}.lib debug ${VTK_DIR}/../../vtkexpat${VTK_VERSION}d.lib optimized ${VTK_DIR}/../../vtkexpat${VTK_VERSION}.lib debug ${VTK_DIR}/../../vtksys${VTK_VERSION}d.lib optimized ${VTK_DIR}/../../vtksys${VTK_VERSION}.lib general vfw32)
      
      SET(VTK_LIBRARIESFILTERING debug ${VTK_DIR}/../../vtkCommonCore${VTK_VERSION}d.lib optimized ${VTK_DIR}/../../vtkCommonCore${VTK_VERSION}.lib)
      SET(VTK_LIBRARIESCOMMON debug ${VTK_DIR}/../../vtksys${VTK_VERSION}d.lib optimized ${VTK_DIR}/../../vtksys${VTK_VERSION}.lib)
    #MESSAGE(${VTK_LIBRARIESCOMMON})
      INCLUDE_DIRECTORIES(${VTK_INCLUDE_DIRS})
      ADD_DEFINITIONS(-DHAVE_VTK)
      SET(EXTRA_LIBS ${EXTRA_LIBS} ${VTK_LIBRARIES} ${VTK_LIBRARIESIO} ${VTK_LIBRARIESFILTERING} ${VTK_LIBRARIESCOMMON})
    ELSE(MSVC)
      add_definitions(-DHAVE_VTK)
      include_directories(${VTK_INCLUDE_DIRS})
      set_property(DIRECTORY APPEND PROPERTY COMPILE_DEFINITIONS ${VTK_DEFINITIONS})
      IF(MINGW)
          SET(EXTRA_LIBS ${EXTRA_LIBS} vtkCommon vtkGraphics vtkIO vtkFiltering)
      ELSE(MINGW)
         set(EXTRA_LIBS ${EXTRA_LIBS} ${VTK_LIBRARIES})
      ENDIF(MINGW)
      if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
         SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-deprecated")
      elseif (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
         SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-deprecated -Wno-inconsistent-missing-override")
      endif()
    ENDIF(MSVC)
  ENDIF()
ENDMACRO(USE_VTK)

