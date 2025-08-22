#ifndef BASIC_STAMP_SIMD_H
#define BASIC_STAMP_SIMD_H

#ifdef PAINTTY_USE_SIMD

#include "basic-stamp.h"
#include <experimental/simd>

namespace std_simd = std::experimental;

// 使用与BasicBrushV3SIMD相同的SIMD配置
#ifdef __AVX512F__
    using simd_abi = std_simd::simd_abi::native<float>;
#elif defined(__AVX2__)
    using simd_abi = std_simd::simd_abi::native<float>;
#elif defined(__SSE2__)
    using simd_abi = std_simd::simd_abi::native<float>;
#else
    using simd_abi = std_simd::simd_abi::scalar;
#endif

using simd_float = std_simd::simd<float, simd_abi>;
using simd_mask = std_simd::simd_mask<float, simd_abi>;

class BasicStampSIMD : public BasicStamp
{
public:
    explicit BasicStampSIMD();
    ~BasicStampSIMD() override = default;
    
    // 重写纹理应用方法以使用SIMD优化
    void applyTexture(QPainter* painter, const QColor& color, qreal size, const QPointF& pos) override;

private:
    // SIMD优化的纹理处理方法
    void applyNoiseTextureSIMD(QPainter* painter, const QColor& color, qreal size, const QPointF& pos);
    void applyCrayonTextureSIMD(QPainter* painter, const QColor& color, qreal size, const QPointF& pos);
    
    // SIMD随机数生成和处理
    void generateRandomPointsSIMD(qreal size, const QPointF& center,
                                 std::vector<QPointF>& points,
                                 std::vector<qreal>& pointSizes,
                                 int numPoints);
    
    // SIMD颜色处理
    void processColorVariationsSIMD(const QColor& baseColor,
                                   std::vector<QColor>& colors,
                                   int numColors, bool isDarker);
    
    // 常量定义
    static constexpr size_t SIMD_WIDTH = std_simd::native_simd<float>::size();
    static constexpr int MAX_NOISE_POINTS = 256; // 最大噪点数量
};

#endif // PAINTTY_USE_SIMD

#endif // BASIC_STAMP_SIMD_H