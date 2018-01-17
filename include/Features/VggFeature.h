/*
Author: iXez
Website: https://ixez.github.io
Email: sachika.misawa@outlook.com
*/
#pragma once
#include "Features/IFeature.h"
#include <caffe/caffe.hpp>
#include <boost/shared_ptr.hpp>
#include <opencv2/opencv.hpp>

namespace zkcf {
    using namespace cv;
    using namespace caffe;
    class VggFeature : public IFeature {
    public:
        VggFeature(const string& modelPath, const string& weightsPath, const string& meanPath);
        Mat Extract(const Mat& patch, FeatureSize& sz) const override;
    private:
        shared_ptr<Net<float> > Model;
        FeatureSize ModelInputSz;
        Mat ModelMean;

        Mat ModelMeanInit(const string &path, int chns);
    };
}