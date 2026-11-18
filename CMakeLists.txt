cmake_minimum_required(VERSION 3.24)
project(CinemaLove VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 QuickDialogs2 OpenGL Sql Network)
qt_standard_project_setup(REQUIRES 6.8)

include(cmake/Libmpv.cmake) # define el destino mpv::libmpv

qt_add_executable(cinemalove
    src/main.cpp
)

set_source_files_properties(qml/theme/Tokens.qml qml/Images.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(cinemalove
    URI CinemaLove
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/CatalogView.qml
        qml/PosterCard.qml
        qml/DetailView.qml
        qml/AdminView.qml
        qml/AdminButton.qml
        qml/Modal.qml
        qml/ItemMenu.qml
        qml/PlayerOverlay.qml
        qml/PersonView.qml
        qml/ClientView.qml
        qml/Pager.qml
        qml/SmoothWheel.qml
        qml/RoundedCorners.qml
        qml/Sidebar.qml
        qml/NavIcon.qml
        qml/StartupView.qml
        qml/DataPanel.qml
        qml/AboutDialog.qml
        qml/Check.qml
        qml/ServerPanel.qml
        qml/CollectionsView.qml
        qml/SeriesDetailView.qml
        qml/HomeView.qml
        qml/HomeRow.qml
        qml/LandscapeCard.qml
        qml/theme/Tokens.qml
        qml/Images.qml
    SOURCES
        src/catalog/MovieModel.h
        src/catalog/MovieModel.cpp
        src/catalog/SeriesCatalog.h
        src/catalog/SeriesCatalog.cpp
        src/catalog/ProgressWriter.h
        src/catalog/ProgressWriter.cpp
        src/backend/BackendLauncher.h
        src/backend/BackendLauncher.cpp
        src/AppSettings.h
        src/server/HttpServer.h
        src/server/HttpServer.cpp
        src/server/Mappers.h
        src/server/Mappers.cpp
        src/server/Bcrypt.h
        src/server/Bcrypt.cpp
        src/server/ImageApi.h
        src/server/ImageApi.cpp
        src/server/UserApi.h
        src/server/UserApi.cpp
        src/server/Catalog.h
        src/server/Catalog.cpp
        src/server/Store.h
        src/server/Store.cpp
        src/server/ApiServer.h
        src/server/ApiServer.cpp
        src/TestInput.h
        src/TestInput.cpp
        src/backend/DataFolder.h
        src/backend/DataFolder.cpp
        src/backend/ApiClient.h
        src/backend/ApiClient.cpp
        src/backend/ClImageProvider.h
        src/backend/ClImageProvider.cpp
        src/player/MpvItem.h
        src/player/MpvItem.cpp
)

target_include_directories(cinemalove PRIVATE src/player src/catalog src/backend)

# Linux: inhibir el apagado de pantalla vía D-Bus (org.freedesktop.ScreenSaver).
if(UNIX AND NOT APPLE)
    find_package(Qt6 6.8 QUIET COMPONENTS DBus)
    if(Qt6DBus_FOUND)
        target_link_libraries(cinemalove PRIVATE Qt6::DBus)
        target_compile_definitions(cinemalove PRIVATE CL_HAVE_QTDBUS=1)
    endif()
endif()

qt_add_resources(cinemalove "server_schema" PREFIX "/server" BASE "${CMAKE_SOURCE_DIR}/src/server" FILES src/server/schema-v11.sql)

target_link_libraries(cinemalove PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::OpenGL Qt6::Sql Qt6::Network Qt6::QuickControls2
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
