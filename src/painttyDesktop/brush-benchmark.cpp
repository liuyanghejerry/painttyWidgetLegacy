#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QPainter>
#include <QPixmap>
#include <QList>
#include <QRandomGenerator>
#include <chrono>
#include <iostream>

#include "paintingTools/brush/basicbrushv3.h"
#include "paintingTools/brush/basicbrushv3-simd.h"

// 生成测试数据
QList<PressurePoint> generateTestPoints(int count, QSize canvasSize)
{
    QList<PressurePoint> points;
    points.reserve(count);
    
    QRandomGenerator* rng = QRandomGenerator::global();
    
    for (int i = 0; i < count; ++i) {
        qreal x = rng->bounded(canvasSize.width());
        qreal y = rng->bounded(canvasSize.height());
        qreal pressure = 0.3 + rng->generateDouble() * 0.7; // 0.3-1.0
        qreal tiltX = (rng->generateDouble() - 0.5) * 0.4; // -0.2 to 0.2
        qreal tiltY = (rng->generateDouble() - 0.5) * 0.4; // -0.2 to 0.2
        
        points.append(PressurePoint(QPointF(x, y), pressure, tiltX, tiltY));
    }
    
    return points;
}

// 基准测试函数
void benchmarkBrush(AbstractBrushV3* brush, const QString& brushName, 
                   const QList<PressurePoint>& points, QSize canvasSize, int iterations)
{
    std::cout << "Testing " << brushName.toStdString() << "..." << std::endl;
    
    QElapsedTimer timer;
    qint64 totalTime = 0;
    
    for (int iter = 0; iter < iterations; ++iter) {
        // 创建画布
        QPixmap canvas(canvasSize);
        canvas.fill(Qt::white);
        QPainter painter(&canvas);
        
        // 设置笔刷
        brush->setWidth(15);
        brush->setColor(QColor(0, 100, 200, 180));
        
        // 开始计时
        timer.start();
        
        // 绘制路径
        brush->clearAllPaths();
        for (const auto& point : points) {
            brush->addPointToCurrentPath(point);
        }
        brush->endStroke();
        brush->drawAllPaths(&painter);
        
        // 结束计时
        totalTime += timer.elapsed();
        
        painter.end();
    }
    
    double avgTime = totalTime / static_cast<double>(iterations);
    std::cout << "  Average time: " << avgTime << " ms" << std::endl;
    std::cout << "  Total points processed: " << points.size() * iterations << std::endl;
    std::cout << "  Points per second: " << (points.size() * iterations * 1000.0) / totalTime << std::endl;
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    
    std::cout << "Brush V3 SIMD Performance Benchmark" << std::endl;
    std::cout << "====================================" << std::endl;
    
    // 测试参数
    const QSize canvasSize(1920, 1080);
    const int pointCount = 1000;
    const int iterations = 50;
    
    std::cout << "Canvas size: " << canvasSize.width() << "x" << canvasSize.height() << std::endl;
    std::cout << "Points per stroke: " << pointCount << std::endl;
    std::cout << "Iterations: " << iterations << std::endl;
    std::cout << std::endl;
    
    // 生成测试数据
    QList<PressurePoint> testPoints = generateTestPoints(pointCount, canvasSize);
    std::cout << "Generated " << testPoints.size() << " test points." << std::endl;
    std::cout << std::endl;
    
    // 测试原版笔刷
    BasicBrushV3 originalBrush;
    benchmarkBrush(&originalBrush, "BasicBrushV3 (Original)", testPoints, canvasSize, iterations);
    std::cout << std::endl;
    
    // 测试SIMD优化笔刷
    BasicBrushV3SIMD simdBrush;
    benchmarkBrush(&simdBrush, "BasicBrushV3SIMD (Optimized)", testPoints, canvasSize, iterations);
    std::cout << std::endl;
    
    std::cout << "Benchmark completed!" << std::endl;
    
    return 0;
}