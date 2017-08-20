#pragma once

#include "Def.h"
#include <opencv2/opencv.hpp>

namespace zkcf {
    using namespace cv;

    class IFeature {
    public:
        // Extract feature maps in r x c x d maps encoded in d x (r*c) Mat.
        virtual Mat Extract(const Mat& patch, FeatureSize& sz) const=0;
        int CellSize = 1;
    };
}