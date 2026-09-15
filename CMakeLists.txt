cmake_minimum_required(VERSION 3.21)
project(Aspectra VERSION 3.0.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Concurrent Multimedia)
qt_add_executable(Aspectra WIN32 main.cpp MainWindow.cpp MainWindow.h
    GalleryTile.cpp GalleryTile.h AspectraGalleryScreen.cpp AspectraGalleryScreen.h
    ModernUi.cpp ModernUi.h
    Processing.cpp Processing.h PreviewWidget.cpp PreviewWidget.h
    ScreenCaptureManager.cpp ScreenCaptureManager.h resources.qrc
    PowerFeatures.cpp AnnotationEditor.cpp Compressor.cpp PowerTests.cpp UpgradeTests.cpp ExportPipeline.cpp LayerEffects.cpp PsdImporter.cpp TimelineWidget.cpp TimelineWidget.h MaterialViewport.cpp MaterialViewport.h VectorTracer.cpp VectorTracer.h ModelImporter.cpp ModelImporter.h RasterToolEngine.cpp RasterToolEngine.h)
target_link_libraries(Aspectra PRIVATE Qt6::Widgets Qt6::Concurrent Qt6::Multimedia)
if(WIN32)
  target_link_libraries(Aspectra PRIVATE dwmapi)
endif()
if(MSVC)
  target_compile_options(Aspectra PRIVATE /W4 /utf-8 /openmp)
endif()
enable_testing()
add_test(NAME processing COMMAND Aspectra --self-test)
set_tests_properties(processing PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
install(TARGETS Aspectra RUNTIME DESTINATION .)
