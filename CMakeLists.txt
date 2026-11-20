cmake_minimum_required(VERSION 3.21)
project(HP8620-Control LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Network SerialPort LinguistTools)
qt_standard_project_setup()
set(QT_I18N_SOURCE_LANGUAGE de)
set(CMAKE_AUTORCC ON)

qt_add_executable(HP8620-Control WIN32
    main.cpp
    mainwindow.cpp mainwindow.h updates.cpp
    hp8620.cpp hp8620.h
    gpib.cpp gpib.h
    version.h
    assets/app.qrc
    assets/app.rc
)
target_link_libraries(HP8620-Control PRIVATE Qt6::Widgets Qt6::Network Qt6::SerialPort)
target_include_directories(HP8620-Control PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/assets")
if(MSVC)
    target_compile_options(HP8620-Control PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/utf-8>)
endif()

qt_add_translations(HP8620-Control TS_FILES
    translations/hp8620_en.ts
    translations/hp8620_fr.ts
)
foreach(locale IN ITEMS de fr)
    set(qt_catalog "${Qt6_DIR}/../../../translations/qtbase_${locale}.qm")
    if(EXISTS "${qt_catalog}")
        get_filename_component(qt_catalog "${qt_catalog}" ABSOLUTE)
        set_source_files_properties("${qt_catalog}" PROPERTIES QT_RESOURCE_ALIAS "qtbase_${locale}.qm")
        qt_add_resources(HP8620-Control "qt_i18n_${locale}" PREFIX "/qt-i18n" FILES "${qt_catalog}")
    endif()
endforeach()
