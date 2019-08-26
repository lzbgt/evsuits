#include "inc/json.hpp"
#include <iostream>

using namespace nlohmann;
using namespace std;

int main() {
    json origin = R"({"name": "john", "genda":"male"})"_json;
    json newJ = R"({"name":"Jim", "age":10})"_json;
    json diff = json::diff(origin, newJ);
    cout << diff.dump();
}