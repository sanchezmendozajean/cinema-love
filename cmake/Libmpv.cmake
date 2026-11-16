# Localiza libmpv y define el destino importado mpv::libmpv.
#
# Windows: espera third_party/libmpv/{include/mpv/*.h, libmpv-2.dll} (ver el script fetch de dependencias).
#          MSVC necesita una biblioteca de importación .lib: se genera a partir de las exportaciones de la DLL.
# Linux:   usa libmpv del sistema (pkg-config).

add_library(mpv::libmpv INTERFACE IMPORTED)

if(WIN32)
    set(MPV_ROOT "${CMAKE_SOURCE_DIR}/third_party/libmpv")
    set(MPV_DLL "${MPV_ROOT}/libmpv-2.dll")
    if(NOT EXISTS "${MPV_DLL}")
        message(FATAL_ERROR "No se encontró ${MPV_DLL}. Descomprime mpv-dev-x86_64 en third_party/libmpv.")
    endif()

    set(MPV_LIB "${CMAKE_BINARY_DIR}/mpvlib/mpv.lib")
    if(NOT EXISTS "${MPV_LIB}")
        get_filename_component(_linkdir "${CMAKE_LINKER}" DIRECTORY)
        find_program(MPV_DUMPBIN dumpbin HINTS "${_linkdir}" REQUIRED)
        find_program(MPV_LIBEXE lib HINTS "${_linkdir}" REQUIRED)
        file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/mpvlib")
        execute_process(COMMAND "${MPV_DUMPBIN}" /nologo /exports "${MPV_DLL}"
                        OUTPUT_VARIABLE _exports RESULT_VARIABLE _rc)
        if(NOT _rc EQUAL 0)
            message(FATAL_ERROR "dumpbin falló al leer ${MPV_DLL}")
        endif()
        string(REGEX MATCHALL "[ ]+[0-9]+[ ]+[0-9A-F]+[ ]+[0-9A-F]+[ ]+mpv_[A-Za-z0-9_]+" _lines "${_exports}")
        set(_def "LIBRARY libmpv-2.dll\nEXPORTS\n")
        foreach(_l IN LISTS _lines)
            string(REGEX REPLACE ".*[ ](mpv_[A-Za-z0-9_]+)$" "\\1" _name "${_l}")
            string(APPEND _def "    ${_name}\n")
        endforeach()
        file(WRITE "${CMAKE_BINARY_DIR}/mpvlib/mpv.def" "${_def}")
        execute_process(COMMAND "${MPV_LIBEXE}" /nologo /machine:x64 "/def:${CMAKE_BINARY_DIR}/mpvlib/mpv.def"
                        "/out:${MPV_LIB}" RESULT_VARIABLE _rc)
        if(NOT _rc EQUAL 0)
            message(FATAL_ERROR "lib.exe no pudo generar ${MPV_LIB}")
        endif()
    endif()

    target_include_directories(mpv::libmpv INTERFACE "${MPV_ROOT}/include")
    target_link_libraries(mpv::libmpv INTERFACE "${MPV_LIB}")
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)
    target_link_libraries(mpv::libmpv INTERFACE PkgConfig::MPV)
endif()
