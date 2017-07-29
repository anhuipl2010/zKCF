#include <KCF.h>
#include <Kernel/GaussianKernel.h>
#include <Feature/HogFeature.h>
#include <Feature/HogLabFeature.h>
#include "KCF.h"
#include "ffttools.hpp"
#include "recttools.hpp"
#include "fhog.hpp"
#include "labdata.hpp"

namespace zkcf {
// Update position based on the new frame
    cv::Rect KCF::update(cv::Mat image) {
        if (Roi.x + Roi.width <= 0) Roi.x = -Roi.width + 1;
        if (Roi.y + Roi.height <= 0) Roi.y = -Roi.height + 1;
        if (Roi.x >= image.cols - 1) Roi.x = image.cols - 2;
        if (Roi.y >= image.rows - 1) Roi.y = image.rows - 2;

        float cx = Roi.x + Roi.width / 2.0f;
        float cy = Roi.y + Roi.height / 2.0f;


        float peak_value;
        cv::Point2f res = detect(_tmpl, getFeatures(image, 0, 1.0f), peak_value);

        if (ScaleStep != 1) {
            // Test at a smaller _scale
            float new_peak_value;
            cv::Point2f new_res = detect(_tmpl, getFeatures(image, 0, 1.0f / ScaleStep), new_peak_value);

            if (ScaleWeight * new_peak_value > peak_value) {
                res = new_res;
                peak_value = new_peak_value;
                _scale /= ScaleStep;
                Roi.width /= ScaleStep;
                Roi.height /= ScaleStep;
            }

            // Test at a bigger _scale
            new_res = detect(_tmpl, getFeatures(image, 0, ScaleStep), new_peak_value);

            if (ScaleWeight * new_peak_value > peak_value) {
                res = new_res;
                peak_value = new_peak_value;
                _scale *= ScaleStep;
                Roi.width *= ScaleStep;
                Roi.height *= ScaleStep;
            }
        }

        // Adjust by cell size and _scale
        Roi.x = cx - Roi.width / 2.0f + ((float) res.x * CellSize * _scale);
        Roi.y = cy - Roi.height / 2.0f + ((float) res.y * CellSize * _scale);

        if (Roi.x >= image.cols - 1) Roi.x = image.cols - 1;
        if (Roi.y >= image.rows - 1) Roi.y = image.rows - 1;
        if (Roi.x + Roi.width <= 0) Roi.x = -Roi.width + 2;
        if (Roi.y + Roi.height <= 0) Roi.y = -Roi.height + 2;

        assert(Roi.width >= 0 && Roi.height >= 0);
        cv::Mat x = getFeatures(image, 0);
        train(x, LearningRate);

        return Roi;
    }


// Detect object in the current frame.
    cv::Point2f KCF::detect(cv::Mat z, cv::Mat x, float &peak_value) {
        using namespace FFTTools;

        cv::Mat k = gaussianCorrelation(x, z);
        cv::Mat res = (real(fftd(complexMultiplication(_alphaf, fftd(k)), true)));

        //minMaxLoc only accepts doubles for the peak, and integer points for the coordinates
        cv::Point2i pi;
        double pv;
        cv::minMaxLoc(res, NULL, &pv, NULL, &pi);
        peak_value = (float) pv;

        //subpixel peak estimation, coordinates will be non-integer
        cv::Point2f p((float) pi.x, (float) pi.y);

        if (pi.x > 0 && pi.x < res.cols - 1) {
            p.x += subPixelPeak(res.at<float>(pi.y, pi.x - 1), peak_value, res.at<float>(pi.y, pi.x + 1));
        }

        if (pi.y > 0 && pi.y < res.rows - 1) {
            p.y += subPixelPeak(res.at<float>(pi.y - 1, pi.x), peak_value, res.at<float>(pi.y + 1, pi.x));
        }

        p.x -= (res.cols) / 2;
        p.y -= (res.rows) / 2;

        return p;
    }

// train tracker with a single image
    void KCF::train(cv::Mat x, float train_interp_factor) {
        using namespace FFTTools;

        cv::Mat k = gaussianCorrelation(x, x);
        cv::Mat alphaf = complexDivision(_prob, (fftd(k) + Lambda));

        _tmpl = (1 - train_interp_factor) * _tmpl + (train_interp_factor) * x;
        _alphaf = (1 - train_interp_factor) * _alphaf + (train_interp_factor) * alphaf;


        /*cv::Mat kf = fftd(gaussianCorrelation(x, x));
        cv::Mat num = complexMultiplication(kf, _prob);
        cv::Mat den = complexMultiplication(kf, kf + lambda);

        _tmpl = (1 - train_interp_factor) * _tmpl + (train_interp_factor) * x;
        _num = (1 - train_interp_factor) * _num + (train_interp_factor) * num;
        _den = (1 - train_interp_factor) * _den + (train_interp_factor) * den;

        _alphaf = complexDivision(_num, _den);*/

    }

// Evaluates a Gaussian kernel with bandwidth SIGMA for all relative shifts between input images X and Y, which must both be MxN. They must    also be periodic (ie., pre-processed with a cosine window).
    cv::Mat KCF::gaussianCorrelation(cv::Mat x1, cv::Mat x2) {
        using namespace FFTTools;
        cv::Mat c = cv::Mat(cv::Size(size_patch[1], size_patch[0]), CV_32F, cv::Scalar(0));
        // HOG features
        if (_hogfeatures) {
            cv::Mat caux;
            cv::Mat x1aux;
            cv::Mat x2aux;
            for (int i = 0; i < size_patch[2]; i++) {
                x1aux = x1.row(i);   // Procedure do deal with cv::Mat multichannel bug
                x1aux = x1aux.reshape(1, size_patch[0]);
                x2aux = x2.row(i).reshape(1, size_patch[0]);
                cv::mulSpectrums(fftd(x1aux), fftd(x2aux), caux, 0, true);
                caux = fftd(caux, true);
                rearrange(caux);
                caux.convertTo(caux, CV_32F);
                c = c + real(caux);
            }
        }
            // Gray features
        else {
            cv::mulSpectrums(fftd(x1), fftd(x2), c, 0, true);
            c = fftd(c, true);
            rearrange(c);
            c = real(c);
        }
        cv::Mat d;
        cv::max(((cv::sum(x1.mul(x1))[0] + cv::sum(x2.mul(x2))[0]) - 2. * c) /
                (size_patch[0] * size_patch[1] * size_patch[2]), 0, d);

        cv::Mat k;
        cv::exp((-d / (Sigma * Sigma)), k);
        return k;
    }

// Create Gaussian Peak. Function called only in the first frame.
    cv::Mat KCF::createGaussianPeak(int sizey, int sizex) {
        cv::Mat_<float> res(sizey, sizex);

        int syh = (sizey) / 2;
        int sxh = (sizex) / 2;

        float output_sigma = std::sqrt((float) sizex * sizey) / Padding * OutputSigmaFactor;
        float mult = -0.5 / (output_sigma * output_sigma);

        for (int i = 0; i < sizey; i++)
            for (int j = 0; j < sizex; j++) {
                int ih = i - syh;
                int jh = j - sxh;
                res(i, j) = std::exp(mult * (float) (ih * ih + jh * jh));
            }
        return FFTTools::fftd(res);
    }

// Obtain sub-window from image, with replication-padding and extract features
    cv::Mat KCF::getFeatures(const cv::Mat &image, bool inithann, float scale_adjust) {
        cv::Rect extracted_roi;

        float cx = Roi.x + Roi.width / 2;
        float cy = Roi.y + Roi.height / 2;

        if (inithann) {
            int padded_w = Roi.width * Padding;
            int padded_h = Roi.height * Padding;

            if (TemplateSize > 1) {  // Fit largest dimension to the given template size
                if (padded_w >= padded_h)  //fit to width
                    _scale = padded_w / (float) TemplateSize;
                else
                    _scale = padded_h / (float) TemplateSize;

                _tmpl_sz.width = padded_w / _scale;
                _tmpl_sz.height = padded_h / _scale;
            } else {  //No template size given, use ROI size
                _tmpl_sz.width = padded_w;
                _tmpl_sz.height = padded_h;
                _scale = 1;
                // original code from paper:
                /*if (sqrt(padded_w * padded_h) >= 100) {   //Normal size
                    _tmpl_sz.width = padded_w;
                    _tmpl_sz.height = padded_h;
                    _scale = 1;
                }
                else {   //ROI is too big, track at half size
                    _tmpl_sz.width = padded_w / 2;
                    _tmpl_sz.height = padded_h / 2;
                    _scale = 2;
                }*/
            }

            if (_hogfeatures) {
                // Round to cell size and also make it even
                _tmpl_sz.width = (((int) (_tmpl_sz.width / (2 * CellSize))) * 2 * CellSize) + CellSize * 2;
                _tmpl_sz.height = (((int) (_tmpl_sz.height / (2 * CellSize))) * 2 * CellSize) + CellSize * 2;
            } else {  //Make number of pixels even (helps with some logic involving half-dimensions)
                _tmpl_sz.width = (_tmpl_sz.width / 2) * 2;
                _tmpl_sz.height = (_tmpl_sz.height / 2) * 2;
            }
        }

        extracted_roi.width = scale_adjust * _scale * _tmpl_sz.width;
        extracted_roi.height = scale_adjust * _scale * _tmpl_sz.height;

        // center roi with new size
        extracted_roi.x = cx - extracted_roi.width / 2;
        extracted_roi.y = cy - extracted_roi.height / 2;

        cv::Mat FeaturesMap;
        cv::Mat z = RectTools::subwindow(image, extracted_roi, cv::BORDER_REPLICATE);

        if (z.cols != _tmpl_sz.width || z.rows != _tmpl_sz.height) {
            cv::resize(z, z, _tmpl_sz);
        }

        // HOG features
        if (_hogfeatures) {
            IplImage z_ipl = z;
            CvLSVMFeatureMapCaskade *map;
            getFeatureMaps(&z_ipl, CellSize, &map);
            normalizeAndTruncate(map, 0.2f);
            PCAFeatureMaps(map);
            size_patch[0] = map->sizeY;
            size_patch[1] = map->sizeX;
            size_patch[2] = map->numFeatures;

            FeaturesMap = cv::Mat(cv::Size(map->numFeatures, map->sizeX * map->sizeY), CV_32F,
                                  map->map);  // Procedure do deal with cv::Mat multichannel bug
            FeaturesMap = FeaturesMap.t();
            freeFeatureMapObject(&map);

            // Lab features
            if (_labfeatures) {
                cv::Mat imgLab;
                cvtColor(z, imgLab, CV_BGR2Lab);
                unsigned char *input = (unsigned char *) (imgLab.data);

                // Sparse output vector
                cv::Mat outputLab = cv::Mat(LabCentroids.rows, size_patch[0] * size_patch[1], CV_32F, float(0));

                int cntCell = 0;
                // Iterate through each cell
                for (int cY = CellSize; cY < z.rows - CellSize; cY += CellSize) {
                    for (int cX = CellSize; cX < z.cols - CellSize; cX += CellSize) {
                        // Iterate through each pixel of cell (cX,cY)
                        for (int y = cY; y < cY + CellSize; ++y) {
                            for (int x = cX; x < cX + CellSize; ++x) {
                                // Lab components for each pixel
                                float l = (float) input[(z.cols * y + x) * 3];
                                float a = (float) input[(z.cols * y + x) * 3 + 1];
                                float b = (float) input[(z.cols * y + x) * 3 + 2];

                                // Iterate trough each centroid
                                float minDist = FLT_MAX;
                                int minIdx = 0;
                                float *inputCentroid = (float *) (LabCentroids.data);
                                for (int k = 0; k < LabCentroids.rows; ++k) {
                                    float dist = ((l - inputCentroid[3 * k]) * (l - inputCentroid[3 * k]))
                                                 + ((a - inputCentroid[3 * k + 1]) * (a - inputCentroid[3 * k + 1]))
                                                 + ((b - inputCentroid[3 * k + 2]) * (b - inputCentroid[3 * k + 2]));
                                    if (dist < minDist) {
                                        minDist = dist;
                                        minIdx = k;
                                    }
                                }
                                // Store result at output
                                outputLab.at<float>(minIdx, cntCell) += 1.0 / (CellSize * CellSize);
                                //((float*) outputLab.data)[minIdx * (size_patch[0]*size_patch[1]) + cntCell] += 1.0 / cell_sizeQ;
                            }
                        }
                        cntCell++;
                    }
                }
                // Update size_patch[2] and add features to FeaturesMap
                size_patch[2] += LabCentroids.rows;
                FeaturesMap.push_back(outputLab);
            }
        } else {
            FeaturesMap = RectTools::getGrayImage(z);
            FeaturesMap -= (float) 0.5; // In Paper;
            size_patch[0] = z.rows;
            size_patch[1] = z.cols;
            size_patch[2] = 1;
        }

        if (inithann) {
            createHanningMats();
        }
        FeaturesMap = hann.mul(FeaturesMap);
        return FeaturesMap;
    }

// Initialize Hanning window. Function called only in the first frame.
    void KCF::createHanningMats() {
        cv::Mat hann1t = cv::Mat(cv::Size(size_patch[1], 1), CV_32F, cv::Scalar(0));
        cv::Mat hann2t = cv::Mat(cv::Size(1, size_patch[0]), CV_32F, cv::Scalar(0));

        for (int i = 0; i < hann1t.cols; i++)
            hann1t.at<float>(0, i) = 0.5 * (1 - std::cos(2 * 3.14159265358979323846 * i / (hann1t.cols - 1)));
        for (int i = 0; i < hann2t.rows; i++)
            hann2t.at<float>(i, 0) = 0.5 * (1 - std::cos(2 * 3.14159265358979323846 * i / (hann2t.rows - 1)));

        cv::Mat hann2d = hann2t * hann1t;
        // HOG features
        if (_hogfeatures) {
            cv::Mat hann1d = hann2d.reshape(1, 1); // Procedure do deal with cv::Mat multichannel bug

            hann = cv::Mat(cv::Size(size_patch[0] * size_patch[1], size_patch[2]), CV_32F, cv::Scalar(0));
            for (int i = 0; i < size_patch[2]; i++) {
                for (int j = 0; j < size_patch[0] * size_patch[1]; j++) {
                    hann.at<float>(i, j) = hann1d.at<float>(0, j);
                }
            }
        }
            // Gray features
        else {
            hann = hann2d;
        }
    }

// Calculate sub-pixel peak for one dimension
    float KCF::subPixelPeak(float left, float center, float right) {
        float divisor = 2 * center - right - left;

        if (divisor == 0)
            return 0;

        return 0.5 * (right - left) / divisor;
    }

    KCF::KCF(IFeature::Type ft, IKernel::Type kt) {
        FeatType=ft;
        KernelType=kt;
    }

    void KCF::Init(const Mat &frm, Rect roi) {
        Lambda = 0.0001;
        Padding = 2.5;
        OutputSigmaFactor = 0.125;
        switch(FeatType) {
            case IFeature::HOG:
                LearningRate = 0.012;
                Sigma = 0.6;
                Feature=new HogFeature(KernelType);
                break;
            case IFeature::HOG_LAB:
                LearningRate = 0.005;
                Sigma = 0.4;
                OutputSigmaFactor = 0.1;
                Feature=new HogLabFeature(KernelType,CellSize);
                break;
            case IFeature::RAW:
                LearningRate = 0.075;
                Sigma = 0.2;
//                Feature=new RawFeature(KernelType,CellSize);
                break;
        }
        Kernel=Feature->Kernel;
        if(EnableScale) {
            TemplateSize=TEMPLATE_SIZE_SCALE;
            ScaleStep = 1.05;
            ScaleWeight = 0.95;
        }
        else {
            ScaleStep = 1;
            ScaleWeight = 1;
        }

        assert(roi.width >= 0 && roi.height >= 0);
        Roi = roi;
        _tmpl = getFeatures(frm, 1);
        _prob = createGaussianPeak(size_patch[0], size_patch[1]);
        _alphaf = cv::Mat(size_patch[0], size_patch[1], CV_32FC2, float(0));
        //_num = cv::Mat(size_patch[0], size_patch[1], CV_32FC2, float(0));
        //_den = cv::Mat(size_patch[0], size_patch[1], CV_32FC2, float(0));
        train(_tmpl, 1.0); // train with initial frame
    }
}