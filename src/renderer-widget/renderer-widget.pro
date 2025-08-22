QT += core widgets

TARGET = renderer-widget
TEMPLATE = app

CONFIG += c++17

SOURCES += main.cpp

# BasicBrushV3 相关源文件
SOURCES += ../painttyDesktop/paintingTools/brush/abstractbrushv3.cpp
SOURCES += ../painttyDesktop/paintingTools/brush/basicbrushv3.cpp
SOURCES += ../painttyDesktop/paintingTools/brush/basic-stamp.cpp
SOURCES += ../painttyDesktop/paintingTools/brush/basicbrushv3-simd.cpp
SOURCES += ../painttyDesktop/paintingTools/brush/basic-stamp-simd.cpp
SOURCES += ../painttyDesktop/paintingTools/brush/basic-trail.cpp
SOURCES += ../painttyDesktop/paintingTools/brush/basic-color-system.cpp

# 包含路径
INCLUDEPATH += ../painttyDesktop/
INCLUDEPATH += ../painttyDesktop/paintingTools/brush/

# 编译选项
DEFINES += QT_DEPRECATED_WARNINGS

# 输出目录
DESTDIR = ../../../build/build 


include(../../simd.pri)