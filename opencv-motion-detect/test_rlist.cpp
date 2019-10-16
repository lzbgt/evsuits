#include <set>
#include <algorithm>
#include <spdlog/spdlog.h>
#include <json.hpp>
#include <iostream>

using namespace std;

void insertTsList(set<long> &_list, long elem, int maxSize) {
    // _list.insert(lower_bound(_list.begin(), _list.end(), elem), elem);
    if(_list.size() == 0) {
        _list.insert(_list.begin(),elem);
        return;
    }

    auto itr = _list.rbegin();

    for(; itr != _list.rend(); itr++) {
        if(*itr < elem){
            break;
        }
    }

    if(itr == _list.rbegin() ) {
        _list.insert(_list.end(), elem);
    }else{
        _list.insert(itr.base(), elem);
    }

    if(_list.size() > maxSize) {
        _list.erase(_list.begin());
    }
}

void printTsList(set<long>&_list) {
    for(auto &i:_list) {
        cout <<i <<endl;
    }
}

int main() {
    set<long> tsList;
    insertTsList(tsList, 10, 5);
    insertTsList(tsList,6, 5);
    insertTsList(tsList, 11, 5);
    insertTsList(tsList, 2, 5);
    insertTsList(tsList, 3, 5);
    insertTsList(tsList, 9, 5);

    printTsList(tsList);
    return 0;
}