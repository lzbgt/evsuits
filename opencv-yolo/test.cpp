
#include <opencv2/core/types_c.h>
#include <opencv2/opencv.hpp>
#include <iostream>

using namespace std;
using namespace cv;

int main(){
  VideoCapture cap;
  if(!cap.open("a.mp4"))
  cout << "failed to open\n";
}