painttyWidget (Legacy 单人版)
==============================

本项目是 [painttyWidget](https://github.com/liuyanghejerry/painttyWidget) 的单人分支版本。

原版 painttyWidget 是茶绘君（Mr.Paint, http://mrspaint.com）的客户端，需要配合服务端（[painttyServer](https://github.com/liuyanghejerry/painttyServer)）才能使用。

本单人版剥离了与服务端的通信依赖，保留本地绘画功能、画笔手感和图层工作流，可在无网络的情况下独立使用。

Linux 编译使用 Qt 6.6.3，安装及构建步骤见 [Qt 开发环境配置指南](docs/Qt6-Setup-Guide.md#linuxubuntu--debian)。

直接启动即可绘画；支持独立 `.paintty` 项目的保存/打开、未保存提示、撤销重做、图层管理、
图片导入和 PNG/PSD 导出。服务端配置、远程笔画和房间历史重放已移除。
使用方法与回归测试见 [构建与使用指南](README-BUILD.md)。

LICENSE
=======

Code of painttyWidget is under LGPLv2.

More details can be found at LICENSE and COPYING.
