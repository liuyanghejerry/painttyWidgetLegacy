QT += core widgets

TARGET = canvas-renderer
TEMPLATE = app

CONFIG += c++17

SOURCES += main.cpp \
    ../painttyDesktop/paintingTools/brush/abstractbrushv3.cpp \
    ../painttyDesktop/paintingTools/brush/basic-stamp.cpp \
    ../painttyDesktop/paintingTools/brush/basic-trail.cpp \
    ../painttyDesktop/paintingTools/brush/basic-color-system.cpp \
    ../painttyDesktop/paintingTools/brush/basicbrushv3.cpp

# 包含路径
INCLUDEPATH += ../painttyDesktop/

# 编译选项
DEFINES += QT_DEPRECATED_WARNINGS

# 输出目录
DESTDIR = ../../../build/build