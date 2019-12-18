#include <sys/statvfs.h>
#include <iostream>
#include <string>
using namespace std;

double getDiskAvailPercent(string path) {
    struct statvfs fiData;
    if((statvfs(path.c_str(),&fiData)) < 0 ) {
    } else {
            double fper = fiData.f_bavail/double(fiData.f_blocks);
            cout << fper << endl;
            return fper;
    }

    return -1;
}

int main( int argc, char *argv[] )
{
    struct statvfs fiData;

    if( argc < 2 ) {
            cout <<"Usage, ./size dir1 dir2 ... dirN\n";
            return(1);
    }

    //Lets loopyloop through the argvs
    for( int  i= 1 ; i<argc; i++ ) {
        getDiskAvailPercent(argv[i]);
    }
}