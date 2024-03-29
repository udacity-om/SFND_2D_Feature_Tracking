#include <numeric>
#include "matching2D.hpp"

using namespace std;

// Find best matches for keypoints in two camera images based on several matching methods
void matchDescriptors(std::vector<cv::KeyPoint> &kPtsSource, std::vector<cv::KeyPoint> &kPtsRef, cv::Mat &descSource, cv::Mat &descRef,
                      std::vector<cv::DMatch> &matches, std::string descriptorType, std::string matcherType, std::string selectorType)
{
    // configure matcher
    bool crossCheck = false;
    cv::Ptr<cv::DescriptorMatcher> matcher;

    if (matcherType.compare("MAT_BF") == 0)
    {
        int normType = descriptorType.compare("DES_BINARY") == 0 ? cv::NORM_HAMMING : cv::NORM_L2;
        matcher = cv::BFMatcher::create(normType, crossCheck);
    }
    else if (matcherType.compare("MAT_FLANN") == 0)
    {
		// OpenCV bug workaround : convert binary descriptors to floating point due to a bug in current OpenCV implementation
		if (descSource.type() != CV_32F || descRef.type() != CV_32F) {
			descSource.convertTo(descSource, CV_32F);
			descRef.convertTo(descRef, CV_32F);
		}

		matcher = cv::FlannBasedMatcher::create();
		//matcher = cv::DescriptorMatcher::create(cv::DescriptorMatcher::FLANNBASED);
    }

    // perform matching task
    if (selectorType.compare("SEL_NN") == 0)
    { // nearest neighbor (best match)

        matcher->match(descSource, descRef, matches); // Finds the best match for each descriptor in desc1
    }
    else if (selectorType.compare("SEL_KNN") == 0)
    { // k nearest neighbors (k=2)
		vector<vector<cv::DMatch>> knn_matches;
		matcher->knnMatch(descSource, descRef, knn_matches, 2);

		// filter matches using descriptor distance ratio test
		double min_desc_dist_ratio = 0.8;
		for (auto it = knn_matches.begin(); it != knn_matches.end(); ++it) {
			if ((*it)[0].distance < min_desc_dist_ratio*((*it)[1].distance)) {
				matches.push_back((*it)[0]);
			}
		}
    }
}

// Use one of several types of state-of-art descriptors to uniquely identify keypoints
void descKeypoints(vector<cv::KeyPoint> &keypoints, cv::Mat &img, cv::Mat &descriptors, string descriptorType)
{
    // select appropriate descriptor
    cv::Ptr<cv::DescriptorExtractor> extractor;
    if (descriptorType.compare("BRISK") == 0)
    {

        int threshold = 30;        // FAST/AGAST detection threshold score.
        int octaves = 3;           // detection octaves (use 0 to do single scale)
        float patternScale = 1.0f; // apply this scale to the pattern used for sampling the neighbourhood of a keypoint.

        extractor = cv::BRISK::create(threshold, octaves, patternScale);
    }
    else if(descriptorType.compare("ORB") == 0)
    {
		extractor = cv::ORB::create();
    }
	else if (descriptorType.compare("AKAZE") == 0)
	{
		extractor = cv::AKAZE::create();
	}
	else if (descriptorType.compare("FREAK") == 0)
	{
		//extractor = cv::xfeatures2d::FREAK::create();
	}
	else if (descriptorType.compare("SIFT") == 0)
	{
		//extractor = cv::xfeatures2d::SIFT::create();
	}
	else {
		// Do Nothing
	}

    // perform feature description
    double t = (double)cv::getTickCount();
    extractor->compute(img, keypoints, descriptors);
    t = ((double)cv::getTickCount() - t) / cv::getTickFrequency();
    cout << descriptorType << " descriptor extraction in " << 1000 * t / 1.0 << " ms" << endl;
}

// Detect keypoints in image using the traditional Shi-Thomasi detector
void detKeypointsShiTomasi(vector<cv::KeyPoint> &keypoints, cv::Mat &img, bool bVis = false)
{
    // compute detector parameters based on image size
    int blockSize = 4;       //  size of an average block for computing a derivative covariation matrix over each pixel neighborhood
    double maxOverlap = 0.0; // max. permissible overlap between two features in %
    double minDistance = (1.0 - maxOverlap) * blockSize;
    int maxCorners = img.rows * img.cols / max(1.0, minDistance); // max. num. of keypoints

    double qualityLevel = 0.01; // minimal accepted quality of image corners
    double k = 0.04;

    // Apply corner detection
    double t = (double)cv::getTickCount();
    vector<cv::Point2f> corners;
    cv::goodFeaturesToTrack(img, corners, maxCorners, qualityLevel, minDistance, cv::Mat(), blockSize, false, k);

    // add corners to result vector
    for (auto it = corners.begin(); it != corners.end(); ++it)
    {

        cv::KeyPoint newKeyPoint;
        newKeyPoint.pt = cv::Point2f((*it).x, (*it).y);
        newKeyPoint.size = blockSize;
        keypoints.push_back(newKeyPoint);
    }
    t = ((double)cv::getTickCount() - t) / cv::getTickFrequency();
    cout << "Shi-Tomasi detection with n=" << keypoints.size() << " keypoints in " << 1000 * t / 1.0 << " ms" << endl;

    // visualize results
    if (bVis)
    {
        cv::Mat visImage = img.clone();
        cv::drawKeypoints(img, keypoints, visImage, cv::Scalar::all(-1), cv::DrawMatchesFlags::DRAW_RICH_KEYPOINTS);
        string windowName = "Shi-Tomasi Corner Detector Results";
        cv::namedWindow(windowName, 6);
        imshow(windowName, visImage);
    }
}

void detKeypointsHarris(std::vector<cv::KeyPoint> &keypoints, cv::Mat &img, bool bVis = false)
{
	int blockSize = 2; // size of neighbourhood considered for corner detection
	int apertureSize = 3;// Aperture parameter for the Sobel() operator
	double k = 0.04; // Harris detector free parameter
	int borderType = cv::BORDER_DEFAULT; // Pixel extrapolation methods
	int minResponse = 120; // minimum value for a corner in the 8bit scaled response matrix

	double t = (double)cv::getTickCount();
	cv::Mat dst, dst_norm, dst_norm_scaled;
	dst = cv::Mat::zeros(img.size(), CV_32FC1);
	cv::cornerHarris(img, dst, blockSize, apertureSize, k, borderType);
	cv::normalize(dst, dst_norm, 0, 255, cv::NORM_MINMAX, CV_32FC1, cv::Mat());
	cv::convertScaleAbs(dst_norm, dst_norm_scaled);
	
	// Locate local maxima in the Harris response matrix 
	// and perform a non-maximum suppression (NMS) in a local neighborhood around 
	// each maximum. The resulting coordinates shall be stored in a list of keypoints 
	// of the type `vector<cv::KeyPoint>`.

	double maxOverlap = 0.0; // max permissible overlap between two features in %, used during non-maxima suppression

	for (std::size_t i = 0; i < dst_norm.rows; ++i) {
		for (std::size_t j = 0; j < dst_norm.cols; ++j) {
			int response = (int)dst_norm.at<float>(i, j);
			if (response > minResponse) {
				// only store points above a threshold
				cv::KeyPoint newKeyPoint;
				newKeyPoint.pt = cv::Point2f(j, i); //OpenCV Point2f needs y index first and then x index
				newKeyPoint.size = 2 * apertureSize;
				newKeyPoint.response = response;

				// Perform non-max suppression(NMS) in local neighbourhood around the new key point
				bool bOverlap = false;
				for (auto it = keypoints.begin(); it != keypoints.end(); ++it) {
					double kptOverlap = cv::KeyPoint::overlap(newKeyPoint, *it);
					if (kptOverlap > maxOverlap) {
						bOverlap = true;
						if (newKeyPoint.response > (*it).response) {
							// If overlap is greater than threshold and reponse is higher for new keypoint
							*it = newKeyPoint; // replace old key point with new one
							break;
						}
					}
				}

				if (!bOverlap) {
					// only add new key point if no overlap has been found in previous NMS
					keypoints.push_back(newKeyPoint); // store new key point in dynamic list
				}
			}
		}
	}
	t = ((double)cv::getTickCount() - t) / cv::getTickFrequency();
	cout << "Harris detection with n=" << keypoints.size() << " keypoints in " << 1000 * t / 1.0 << " ms" << endl;

	// visualize keypoints
	if (bVis)
	{
		string windowName = "Harris Corner Detector Results";
		cv::namedWindow(windowName, 6);
		cv::Mat visImage = dst_norm_scaled.clone();
		cv::drawKeypoints(dst_norm_scaled, keypoints, visImage, cv::Scalar::all(-1), cv::DrawMatchesFlags::DRAW_RICH_KEYPOINTS);
		cv::imshow(windowName, visImage);
	}
}


void detKeypointsModern(std::vector<cv::KeyPoint> &keypoints, cv::Mat &img, std::string detectorType, bool bVis = false)
{
	cv::Ptr<cv::FeatureDetector> detector;

	if ("FAST" == detectorType){
		int thresholdFAST = 80; // difference between intensity of the central pixel and pixels of a circle around this pixel
		bool nonMaxSuppression = true; // perform non-maxima suppression on keypoints
		cv::FastFeatureDetector::DetectorType type = cv::FastFeatureDetector::TYPE_9_16; // TYPE_9_16, TYPE_7_12, TYPE_5_8
		detector = cv::FastFeatureDetector::create(thresholdFAST, nonMaxSuppression, type);
	}
	else if ("BRISK" == detectorType) {
		detector = cv::BRISK::create();
	}
	else if ("ORB" == detectorType) {
		detector = cv::ORB::create();
	}
	else if ("AKAZE" == detectorType) {
		detector = cv::AKAZE::create();
	}
	else if ("FREAK" == detectorType) {
		//detector = cv::xfeatures2d::FREAK::create();
	}
	else if ("SIFT" == detectorType) {
		//detector = cv::xfeatures2d::SIFT::create();
	}
	else {
		// Do nothing
	}

	double t = (double)cv::getTickCount();
	detector->detect(img, keypoints);
	t = ((double)cv::getTickCount() - t) / cv::getTickFrequency();
	cout << detectorType + " with n = " << keypoints.size() << " keypoints in " << 1000 * t / 1.0 << " ms" << endl;

	if (bVis) {
		// visualize results
		cv::Mat visImage = img.clone();
		cv::drawKeypoints(img, keypoints, visImage, cv::Scalar::all(-1), cv::DrawMatchesFlags::DRAW_RICH_KEYPOINTS);
		string windowName = detectorType + " Results";
		cv::namedWindow(windowName, 6);
		imshow(windowName, visImage);
	}
}