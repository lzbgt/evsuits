#include "yolo.hpp"
#include "clipp.h"
#include <sstream>

using namespace clipp;
using namespace std;

int main(int argc, char *argv[]){
    string bHumanOnly = "true";
    float fConfident = 0.1;
    bool bVerbose = false;
    bool help = false;
    bool bCont = false;
    int wrap = 10;
    string sInput, sOutput = "detect.jpg";
    string modelPath = ".";

    auto cli = (
        value("input path", sInput),
        option("-cl") & value("confidence level of detection, default: 0.1", fConfident),
        option("-w", "--wrap") & value("output file wrap. defualt: 10; 0 - no wrap", wrap),
        option("-vv", "--debug").set(bVerbose).doc("verbose prints"),
        option("-human", "--human-only") & value("detect only human object, default: true", bHumanOnly),
        option("-c", "--config-path") & value("model and configuration path", modelPath),
        option("-h", "--help").set(help).doc("print this help info"),
        option("-o", "--output") & value("output, eg: a.jpg; b.avi. default: detect.jpg", sOutput),
        option("-r", "--continue").set(bCont).doc("continue detection, default: false")
    );

    if(!parse(argc, argv, cli) || help) {
        stringstream s;
        s << make_man_page(cli, argv[0]);
        spdlog::info(s.str());
        exit(0);
    }
    spdlog::info("{} {} {}", bHumanOnly, fConfident, wrap);

    if(bVerbose) {
        spdlog::set_level(spdlog::level::debug);
    }
 
    YoloDectect detector(modelPath, bHumanOnly == "true"?true:false, fConfident, bCont, wrap);
    detector.process(sInput, sOutput);
}