#include <ctime>
#include <iostream>
#include <cstdlib>


using namespace std;

int main(int argc, char *argv[]) {
    if(strcmp(argv[1], "1") == 0) {
        time_t ts = atol(argv[2]);
        struct tm* ptm = localtime(&ts);
        char buff[20];
        std::strftime(buff, 20, "%Y%m%d_%H%M%S", ptm);
        cout << buff;
    }else{
        std::tm t;
        strptime(argv[2], "%Y%m%d_%H%M%S", &t);
        cout << mktime(&t);
    }
}
