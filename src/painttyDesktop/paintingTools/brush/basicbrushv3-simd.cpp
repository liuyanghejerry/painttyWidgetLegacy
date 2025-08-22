#ifdef PAINTTY_USE_SIMD
#include "basicbrushv3-simd.h"
#include "basic-stamp-simd.h"
#include <QPainter>
#include <QDebug>
#include <experimental/simd>
#include <algorithm>
#include <cmath>

BasicBrushV3SIMD::BasicBrushV3SIMD()
    : BasicBrushV3()
{
    displayName_ = "基础笔刷 V3 (SIMD优化)";
    qDebug() << "SIMD Width:" << simd_float::size();
}

BasicBrushV3SIMD::~BasicBrushV3SIMD()
{
    // 基类会自动清理
}

Stamp* BasicBrushV3SIMD::createStamp()
{
    return new BasicStampSIMD();
}

void BasicBrushV3SIMD::drawPathToPainter(const QList<PressurePoint>& points, QPainter* painter)
{
    if (!painter || points.isEmpty()) return;

    // 使用轨迹系统处理点序列
    QList<PressurePoint> processedPoints = points;
    if (trail_) {
        processedPoints = trail_->processTrail(points);
    }

    // 使用SIMD优化的绘制方法
    drawStampsAlongPathSIMD(processedPoints, painter);
}

void BasicBrushV3SIMD::drawStampsAlongPathSIMD(const QList<PressurePoint>& points, QPainter* painter)
{
    qDebug() << "Drawing stamps along path with SIMD optimization" << points.size();
    if (!painter || !stamp_ || !colorSystem_ || points.isEmpty()) return;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    // 批量预计算印记大小和颜色
    std::vector<qreal> stampSizes;
    std::vector<QColor> colors;
    
    stampSizes.reserve(points.size());
    colors.reserve(points.size());
    
    // SIMD批处理计算
    calculateStampSizesBatch(points, stampSizes);
    processColorsBatch(points, colors);

    // 检查是否包含透明色
    bool hasTransparentColors = std::any_of(colors.begin(), colors.end(),
        [](const QColor& color) { return color.alphaF() < 1.0; });

    if (hasTransparentColors) {
        painter->setCompositionMode(QPainter::CompositionMode_Plus);
    }

    // 绘制所有印记
    for (int i = 0; i < points.size(); ++i) {
        stamp_->drawStamp(painter, points[i].pos, colors[i], stampSizes[i], points[i].pressure);
    }

    painter->restore();
}

void BasicBrushV3SIMD::calculateStampSizesBatch(const QList<PressurePoint>& points, 
                                                std::vector<qreal>& sizes)
{
    sizes.resize(points.size());
    
    const size_t numPoints = points.size();
    const size_t simd_width = simd_float::size();
    const size_t numVectors = (numPoints + simd_width - 1) / simd_width;
    
    // 准备SIMD向量数据
    std::vector<float> pressures(numVectors * simd_width, 0.5f);
    std::vector<float> tiltX(numVectors * simd_width, 0.0f);
    std::vector<float> tiltY(numVectors * simd_width, 0.0f);
    std::vector<float> results(numVectors * simd_width);
    
    // 填充输入数据
    for (size_t i = 0; i < numPoints; ++i) {
        pressures[i] = static_cast<float>(points[i].pressure);
        tiltX[i] = static_cast<float>(points[i].tiltX);
        tiltY[i] = static_cast<float>(points[i].tiltY);
    }
    
    const float baseSize = static_cast<float>(width_);
    
    // SIMD处理
    for (size_t i = 0; i < numVectors; ++i) {
        const size_t offset = i * simd_width;
        
        // 加载数据到SIMD向量
        simd_float pressure_vec;
        simd_float tiltX_vec;
        simd_float tiltY_vec;
        
        pressure_vec.copy_from(pressures.data() + offset, std_simd::element_aligned);
        tiltX_vec.copy_from(tiltX.data() + offset, std_simd::element_aligned);
        tiltY_vec.copy_from(tiltY.data() + offset, std_simd::element_aligned);
        
        // 压感影响大小计算: 0.5 + pressure * 0.5
        auto pressure_multiplier = 0.5f + pressure_vec * 0.5f;
        
        // 倾斜影响计算
        auto tilt_intensity = sqrt(tiltX_vec * tiltX_vec + tiltY_vec * tiltY_vec);
        auto tilt_multiplier = 1.0f - tilt_intensity * 0.3f;
        
        // 确保倾斜乘数不小于0.7
        tilt_multiplier = max(tilt_multiplier, simd_float(0.7f));
        
        // 最终大小计算
        auto final_size = simd_float(baseSize) * pressure_multiplier * tilt_multiplier;
        
        // 存储结果
        final_size.copy_to(results.data() + offset, std_simd::element_aligned);
    }
    
    // 复制回结果向量
    for (size_t i = 0; i < numPoints; ++i) {
        sizes[i] = static_cast<qreal>(results[i]);
    }
}

void BasicBrushV3SIMD::processColorsBatch(const QList<PressurePoint>& points, 
                                          std::vector<QColor>& colors)
{
    colors.resize(points.size());
    
    // 颜色处理目前还是单线程，因为QColor操作比较复杂
    // 但可以通过批处理减少函数调用开销
    const size_t batchSize = std::min(BATCH_SIZE, static_cast<size_t>(points.size()));
    
    for (size_t start = 0; start < static_cast<size_t>(points.size()); start += batchSize) {
        const size_t end = std::min(start + batchSize, static_cast<size_t>(points.size()));
        
        for (size_t i = start; i < end; ++i) {
            colors[i] = colorSystem_->getProcessedColor(
                points[i].pressure, 
                static_cast<int>(i), 
                points.size()
            );
        }
    }
}

void BasicBrushV3SIMD::vectorMathOperations(const std::vector<qreal>& input1,
                                            const std::vector<qreal>& input2,
                                            std::vector<qreal>& output,
                                            qreal multiplier1, qreal multiplier2)
{
    const size_t size = input1.size();
    output.resize(size);
    
    if (size != input2.size()) {
        qWarning() << "Vector size mismatch in SIMD operations";
        return;
    }
    
    const size_t simd_width = simd_float::size();
    const size_t numVectors = (size + simd_width - 1) / simd_width;
    
    // 准备对齐的数据
    std::vector<float> aligned_input1(numVectors * simd_width, 0.0f);
    std::vector<float> aligned_input2(numVectors * simd_width, 0.0f);
    std::vector<float> aligned_output(numVectors * simd_width);
    
    // 转换输入数据
    for (size_t i = 0; i < size; ++i) {
        aligned_input1[i] = static_cast<float>(input1[i]);
        aligned_input2[i] = static_cast<float>(input2[i]);
    }
    
    const float mult1 = static_cast<float>(multiplier1);
    const float mult2 = static_cast<float>(multiplier2);
    
    // SIMD向量化操作
    for (size_t i = 0; i < numVectors; ++i) {
        const size_t offset = i * simd_width;
        
        simd_float vec1;
        simd_float vec2;
        
        vec1.copy_from(aligned_input1.data() + offset, std_simd::element_aligned);
        vec2.copy_from(aligned_input2.data() + offset, std_simd::element_aligned);
        
        // 执行向量化数学运算: output = input1 * mult1 + input2 * mult2
        auto result = vec1 * std_simd::native_simd<float>(mult1) + 
                     vec2 * std_simd::native_simd<float>(mult2);
        
        result.copy_to(aligned_output.data() + offset, std_simd::element_aligned);
    }
    
    // 转换回输出数据
    for (size_t i = 0; i < size; ++i) {
        output[i] = static_cast<qreal>(aligned_output[i]);
    }
}

#endif // PAINTTY_USE_SIMD