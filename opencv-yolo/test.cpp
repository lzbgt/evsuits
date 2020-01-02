#include <fstream>
#include <sstream>
#include <iostream>

#ifdef _MY_HEADERS_
#include <opencv2/core/types_c.h>
#include <opencv2/dnn.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/highgui.hpp>
#else
#include <opencv2/core/types_c.h>
#include <opencv2/opencv.hpp>
#endif

using namespace std;
using namespace cv;

int main(){
  VideoCapture cap;
  if(!cap.open("a.mp4"))
  cout << "failed to open";
}