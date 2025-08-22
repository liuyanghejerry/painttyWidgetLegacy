#ifndef BRUSHFEATURE_H
#define BRUSHFEATURE_H

#include <bitset>

class BrushFeature
{
public:
    enum FEATURE {
        WIDTH = 0,
        COLOR,
        HARDNESS,
        THICKNESS,
        WATER,
        EXTEND,
        MIXIN,
        MASK,
        // 新增压感笔刷特性
        SMOOTHNESS,
        PRESSURE_CURVE,
        TILT_SENSITIVITY
    };

    enum LIMIT: int {
        WIDTH_MAX = 100,
        WIDTH_MIN = 1,
        THICKNESS_MAX = 100,
        THICKNESS_MIN = 0,
        HARDNESS_MAX = 100,
        HARDNESS_MIN = 0,
        WATER_MAX = 100,
        WATER_MIN = 0,
        EXTEND_MAX = 100,
        EXTEND_MIN = 0,
        MIXIN_MAX = 100,
        MIXIN_MIN = 0,
        // 新增压感笔刷限制
        SMOOTHNESS_MAX = 100,
        SMOOTHNESS_MIN = 0,
        PRESSURE_CURVE_MAX = 200,
        PRESSURE_CURVE_MIN = 10,
        TILT_SENSITIVITY_MAX = 100,
        TILT_SENSITIVITY_MIN = 0
    };

    static constexpr int FEATURE_COUNT = TILT_SENSITIVITY+1;

    typedef std::bitset<FEATURE_COUNT> FeatureBits;

    BrushFeature(const FeatureBits& features = FeatureBits())
    {
        features_ = features;
    }

    bool support(FEATURE f)
    {
        return features_.test(f);
    }

protected:
    FeatureBits features_;
};

#endif // BRUSHFEATURE_H
