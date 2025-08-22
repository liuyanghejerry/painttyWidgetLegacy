#ifdef PAINTTY_USE_SIMD

#include "basic-stamp-simd.h"
#include <QPainter>
#include <QRandomGenerator>
#include <QtMath>
#include <QDebug>
#include <experimental/simd>
#include <algorithm>
#include <random>

BasicStampSIMD::BasicStampSIMD()
    : BasicStamp()
{
    qDebug() << "BasicStampSIMD initialized with SIMD width:" << simd_float::size();
}

void BasicStampSIMD::applyTexture(QPainter* painter, const QColor& color, qreal size, const QPointF& pos)
{
    if (!painter) return;
    
    if (textureType() == "solid") {
        BasicStamp::applyTexture(painter, color, size, pos);
    } else if (textureType() == "noise") {
        applyNoiseTextureSIMD(painter, color, size, pos);
    } else if (textureType() == "crayon") {
        applyCrayonTextureSIMD(painter, color, size, pos);
    }
}

void BasicStampSIMD::applyNoiseTextureSIMD(QPainter* painter, const QColor& color, qreal size, const QPointF& pos)
{
    // 计算噪点数量，使用SIMD友好的数量
    int baseNoisePoints = qMax(1, static_cast<int>(size * 0.15));
    const size_t simd_width = simd_float::size();
    int noisePoints = ((baseNoisePoints + simd_width - 1) / simd_width) * simd_width;
    noisePoints = qMin(noisePoints, MAX_NOISE_POINTS);
    
    // 生成随机点位置和大小
    std::vector<QPointF> points;
    std::vector<qreal> pointSizes;
    generateRandomPointsSIMD(size, pos, points, pointSizes, noisePoints);
    
    // 生成颜色变化
    std::vector<QColor> colors;
    bool isDarker = color.lightness() > 128;
    processColorVariationsSIMD(color, colors, noisePoints, isDarker);
    
    // 保存当前的合成模式
    QPainter::CompositionMode originalMode = painter->compositionMode();
    painter->setCompositionMode(QPainter::CompositionMode_SourceOver);
    
    // 绘制噪点（只绘制实际需要的数量）
    int actualPoints = qMin(baseNoisePoints, noisePoints);
    for (int i = 0; i < actualPoints; ++i) {
        painter->setBrush(colors[i]);
        painter->drawEllipse(points[i], pointSizes[i], pointSizes[i]);
    }
    
    painter->setCompositionMode(originalMode);
}

void BasicStampSIMD::applyCrayonTextureSIMD(QPainter* painter, const QColor& color, qreal size, const QPointF& pos)
{
    // 蜡笔纹理的留白效果，使用SIMD优化
    int baseGaps = qMax(2, static_cast<int>(size * 0.15));
    int gaps = ((baseGaps + simd_float::size() - 1) / simd_float::size()) * simd_float::size();
    gaps = qMin(gaps, MAX_NOISE_POINTS / 2);
    
    // 生成留白位置和大小
    std::vector<QPointF> gapPoints;
    std::vector<qreal> gapSizes;
    generateRandomPointsSIMD(size, pos, gapPoints, gapSizes, gaps);
    
    // 调整留白大小（比噪点更大）
    for (size_t i = 0; i < gapSizes.size(); ++i) {
        gapSizes[i] = qMax(gapSizes[i] * 2.5, size * 0.08);
        gapSizes[i] = qMin(gapSizes[i], size * 0.3);
    }
    
    // 绘制留白（只绘制实际需要的数量）
    int actualGaps = qMin(baseGaps, gaps);
    painter->setBrush(Qt::transparent);
    for (int i = 0; i < actualGaps; ++i) {
        painter->drawEllipse(gapPoints[i], gapSizes[i], gapSizes[i]);
    }
}

void BasicStampSIMD::generateRandomPointsSIMD(qreal size, const QPointF& center,
                                              std::vector<QPointF>& points,
                                              std::vector<qreal>& pointSizes,
                                              int numPoints)
{
    points.resize(numPoints);
    pointSizes.resize(numPoints);
    
    // 使用更高质量的随机数生成器
    thread_local std::random_device rd;
    thread_local std::mt19937 gen(rd());
    std::uniform_real_distribution<float> dis(0.0f, 1.0f);
    
    const size_t simd_width = simd_float::size();
    const size_t numVectors = (numPoints + simd_width - 1) / simd_width;
    
    // 准备SIMD向量数据
    std::vector<float> randX(numVectors * simd_width);
    std::vector<float> randY(numVectors * simd_width);
    std::vector<float> randSize(numVectors * simd_width);
    std::vector<float> relativeX(numVectors * simd_width);
    std::vector<float> relativeY(numVectors * simd_width);
    std::vector<float> finalSizes(numVectors * simd_width);
    
    // 生成随机数
    for (size_t i = 0; i < randX.size(); ++i) {
        randX[i] = dis(gen);
        randY[i] = dis(gen);
        randSize[i] = dis(gen);
    }
    
    const float sizeF = static_cast<float>(size);
    const float centerX = static_cast<float>(center.x());
    const float centerY = static_cast<float>(center.y());
    
    // SIMD处理
    for (size_t i = 0; i < numVectors; ++i) {
        const size_t offset = i * simd_width;
        
        // 加载随机数
        simd_float rand_x_vec;
        simd_float rand_y_vec;
        simd_float rand_size_vec;
        
        rand_x_vec.copy_from(randX.data() + offset, std_simd::element_aligned);
        rand_y_vec.copy_from(randY.data() + offset, std_simd::element_aligned);
        rand_size_vec.copy_from(randSize.data() + offset, std_simd::element_aligned);
        
        // 计算相对位置 (-0.5 到 0.5) * size * 0.6
        auto rel_x = (rand_x_vec - 0.5f) * sizeF * 0.6f;
        auto rel_y = (rand_y_vec - 0.5f) * sizeF * 0.6f;
        
        // 计算最终位置
        auto final_x = simd_float(centerX) + rel_x;
        auto final_y = simd_float(centerY) + rel_y;
        
        // 计算点大小 (最小3%，最大15%)
        auto point_size = rand_size_vec * sizeF * 0.12f + sizeF * 0.03f;
        
        // 存储结果
        final_x.copy_to(relativeX.data() + offset, std_simd::element_aligned);
        final_y.copy_to(relativeY.data() + offset, std_simd::element_aligned);
        point_size.copy_to(finalSizes.data() + offset, std_simd::element_aligned);
    }
    
    // 转换为QPointF和qreal
    for (int i = 0; i < numPoints; ++i) {
        points[i] = QPointF(relativeX[i], relativeY[i]);
        pointSizes[i] = static_cast<qreal>(finalSizes[i]);
    }
}

void BasicStampSIMD::processColorVariationsSIMD(const QColor& baseColor,
                                                std::vector<QColor>& colors,
                                                int numColors, bool isDarker)
{
    colors.resize(numColors);
    
    const int baseR = baseColor.red();
    const int baseG = baseColor.green();
    const int baseB = baseColor.blue();
    
    const size_t simd_width = simd_float::size();
    const size_t numVectors = (numColors + simd_width - 1) / simd_width;
    
    // 准备SIMD数据
    std::vector<float> resultR(numVectors * simd_width);
    std::vector<float> resultG(numVectors * simd_width);
    std::vector<float> resultB(numVectors * simd_width);
    
    // 使用固定的颜色变化模式以确保一致性
    const float multiplier = isDarker ? 0.8f : 1.2f;
    
    // SIMD处理颜色变化
    for (size_t i = 0; i < numVectors; ++i) {
        // 创建颜色乘数向量
        simd_float color_mult(multiplier);
        simd_float base_r(static_cast<float>(baseR));
        simd_float base_g(static_cast<float>(baseG));
        simd_float base_b(static_cast<float>(baseB));
        
        // 计算新颜色值
        auto new_r = base_r * color_mult;
        auto new_g = base_g * color_mult;
        auto new_b = base_b * color_mult;
        
        // 限制在0-255范围内
        new_r = min(max(new_r, simd_float(0.0f)), 
                   simd_float(255.0f));
        new_g = min(max(new_g, simd_float(0.0f)), 
                   simd_float(255.0f));
        new_b = min(max(new_b, simd_float(0.0f)), 
                   simd_float(255.0f));
        
        // 存储结果
        const size_t offset = i * simd_width;
        new_r.copy_to(resultR.data() + offset, std_simd::element_aligned);
        new_g.copy_to(resultG.data() + offset, std_simd::element_aligned);
        new_b.copy_to(resultB.data() + offset, std_simd::element_aligned);
    }
    
    // 转换为QColor
    for (int i = 0; i < numColors; ++i) {
        int r = static_cast<int>(resultR[i]);
        int g = static_cast<int>(resultG[i]);
        int b = static_cast<int>(resultB[i]);
        colors[i] = QColor(r, g, b, 120); // 半透明
    }
}

#endif // PAINTTY_USE_SIMD