cmake_minimum_required(VERSION 3.24)
project(CinemaLove VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Qml Quick OpenGL Sql Network)
qt_standard_project_setup(REQUIRES 6.8)

include(cmake/Libmpv.cmake) # define el destino mpv::libmpv

qt_add_executable(cinemalove
    src/main.cpp
)

set_source_files_properties(qml/theme/Tokens.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(cinemalove
    URI CinemaLove
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/CatalogView.qml
        qml/PosterCard.qml
        qml/DetailView.qml
        qml/theme/Tokens.qml
    SOURCES
        src/catalog/MovieModel.h
        src/catalog/MovieModel.cpp
        src/player/MpvItem.h
        src/player/MpvItem.cpp
)

target_include_directories(cinemalove PRIVATE src/player src/catalog)

target_link_libraries(cinemalove PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::OpenGL Qt6::Sql Qt6::Network
    mpv::libmpv
)

if(WIN32)
    set_target_properties(cinemalove PROPERTIES WIN32_EXECUTABLE OFF) # con consola durante el spike (logs)

    # libmpv junto al ejecutable
    add_custom_command(TARGET cinemalove POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${MPV_DLL}" "$<TARGET_FILE_DIR:cinemalove>"
    )

    # Copia las DLL y plugins de Qt junto al ejecutable (solo desarrollo)
    get_target_property(_qmake_exe Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin_dir "${_qmake_exe}" DIRECTORY)
    find_program(WINDEPLOYQT windeployqt HINTS "${_qt_bin_dir}")
    if(WINDEPLOYQT)
        add_custom_command(TARGET cinemalove POST_BUILD
            COMMAND "${WINDEPLOYQT}" --no-translations --no-system-d3d-compiler --no-opengl-sw
                    --qmldir "${CMAKE_SOURCE_DIR}/qml" "$<TARGET_FILE:cinemalove>"
            COMMENT "windeployqt"
        )
    endif()
endif()
