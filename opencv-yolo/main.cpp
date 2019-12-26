#include "yolo.hpp"
#include "clipp.h"
#include <sstream>

using namespace clipp;
using namespace std;

int main(int argc, char *argv[]){
    bool bHumanOnly = true;
    float fConfident = 0.1;
    bool bVerbose = false;
    bool help = false;
    bool bCont = false;
    string sInput, sOutput = "detect.jpg";
    string modelPath = ".";

    auto cli = (
        value("input path", sInput),
        option("-cl").set(fConfident).doc("confidence level of detection, default: 0.1"),
        option("-vv", "--debug").set(bVerbose).doc("verbose prints"),
        option("-human", "--human-only").set(bHumanOnly).doc("detect only human object"),
        option("-c", "--config-path").set(modelPath).doc("model and configuration path"),
        option("-h", "--help").set(help).doc("print this help info"),
        option("-o", "--output").set(sOutput).doc("output, eg: a.jpg; b.avi"),
        option("-r", "--continue").set(bCont).doc("continue detection, default: false")
    );

    if(!parse(argc, argv, cli) || help) {
        stringstream s;
        s << make_man_page(cli, argv[0]);
        spdlog::info(s.str());
        exit(0);
    }
    if(bVerbose) {
        spdlog::set_level(spdlog::level::debug);
    }

    YoloDectect detector(modelPath, bHumanOnly, fConfident, bCont);
    detector.process(sInput, sOutput);
}