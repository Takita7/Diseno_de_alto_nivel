# FindSystemC.cmake - Locate SystemC installation
#
# This module defines:
#   SystemC_FOUND - whether SystemC was found
#   SystemC_INCLUDE_DIRS - the include directories
#   SystemC_LIBRARIES - the libraries to link
#   SystemC_VERSION - the SystemC version

find_package(PkgConfig QUIET)

if(PkgConfig_FOUND)
    pkg_check_modules(PC_SystemC systemc)
endif()

find_path(SystemC_INCLUDE_DIR
    NAMES systemc systemc.h
    PATHS ${PC_SystemC_INCLUDE_DIRS}
          /usr/include
          /usr/local/include
          $ENV{SYSTEMC_HOME}/include
)

find_library(SystemC_LIBRARY
    NAMES systemc
    PATHS ${PC_SystemC_LIBRARY_DIRS}
          /usr/lib
          /usr/local/lib
          /usr/local/lib-linux64
          $ENV{SYSTEMC_HOME}/lib
          $ENV{SYSTEMC_HOME}/lib-linux64
)

set(SystemC_VERSION ${PC_SystemC_VERSION})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(SystemC
    REQUIRED_VARS SystemC_LIBRARY SystemC_INCLUDE_DIR
    VERSION_VAR SystemC_VERSION
)

if(SystemC_FOUND)
    set(SystemC_LIBRARIES ${SystemC_LIBRARY})
    set(SystemC_INCLUDE_DIRS ${SystemC_INCLUDE_DIR})
    
    if(NOT TARGET SystemC::SystemC)
        add_library(SystemC::SystemC UNKNOWN IMPORTED)
        set_target_properties(SystemC::SystemC PROPERTIES
            IMPORTED_LOCATION "${SystemC_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${SystemC_INCLUDE_DIR}"
        )
    endif()
    
    message(STATUS "Found SystemC: ${SystemC_LIBRARY}")
endif()

mark_as_advanced(SystemC_INCLUDE_DIR SystemC_LIBRARY)
