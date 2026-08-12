find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(PC_LiquidDSP QUIET liquid)
endif()

find_path(LiquidDSP_INCLUDE_DIR
    NAMES liquid/liquid.h
    HINTS ${PC_LiquidDSP_INCLUDE_DIRS}
)
find_library(LiquidDSP_LIBRARY
    NAMES liquid
    HINTS ${PC_LiquidDSP_LIBRARY_DIRS}
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LiquidDSP
    REQUIRED_VARS LiquidDSP_LIBRARY LiquidDSP_INCLUDE_DIR
    VERSION_VAR PC_LiquidDSP_VERSION
)

if(LiquidDSP_FOUND AND NOT TARGET LiquidDSP::LiquidDSP)
    add_library(LiquidDSP::LiquidDSP UNKNOWN IMPORTED)
    set_target_properties(LiquidDSP::LiquidDSP PROPERTIES
        IMPORTED_LOCATION "${LiquidDSP_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${LiquidDSP_INCLUDE_DIR}"
    )
endif()

mark_as_advanced(LiquidDSP_INCLUDE_DIR LiquidDSP_LIBRARY)
