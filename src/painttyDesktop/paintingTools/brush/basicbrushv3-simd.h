#ifndef BASICBRUSHV3_SIMD_H
#define BASICBRUSHV3_SIMD_H

#ifdef PAINTTY_USE_SIMD

#include "basicbrushv3.h"
#include <experimental/simd>

namespace std_simd = std::experimental;

// SIMD向量化配置
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

class BasicBrushV3SIMD : public BasicBrushV3
{
public:
    explicit BasicBrushV3SIMD();
    ~BasicBrushV3SIMD() override;

protected:
    void drawPathToPainter(const QList<PressurePoint>& points, QPainter* painter) override;
    
    // 重写子系统创建方法以使用SIMD版本
    Stamp* createStamp() override;

private:
    // SIMD优化的渲染辅助方法
    void drawStampsAlongPathSIMD(const QList<PressurePoint>& points, QPainter* painter);
    
    // SIMD批量计算方法
    void calculateStampSizesBatch(const QList<PressurePoint>& points, 
                                  std::vector<qreal>& sizes);
    void processColorsBatch(const QList<PressurePoint>& points, 
                           std::vector<QColor>& colors);
    
    // SIMD数学函数
    void vectorMathOperations(const std::vector<qreal>& input1,
                             const std::vector<qreal>& input2,
                             std::vector<qreal>& output,
                             qreal multiplier1, qreal multiplier2);
    
    // 常量定义
    static constexpr size_t SIMD_WIDTH = std_simd::native_simd<float>::size();
    static constexpr size_t BATCH_SIZE = 64; // 批处理大小
};

#endif // PAINTTY_USE_SIMD

#endif // BASICBRUSHV3_SIMD_H