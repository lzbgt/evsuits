#include "vendor/include/leveldb/db.h"
#include <iostream>
using namespace std;

int main(){
    leveldb::DB* pdb;
    leveldb::Options options;
    options.create_if_missing = true;
    leveldb::Status status = leveldb::DB::Open(options, "/tmp/test.db", &pdb);
    assert(status.ok());

    string value;
    status = pdb->Get(leveldb::ReadOptions(), "mykey1", &value);
    if(!status.ok()) status = pdb->Put(leveldb::WriteOptions(), "mykey1", "myvalue1");
    if(status.ok()) status = pdb->Get(leveldb::ReadOptions(), "mykey1", &value);
    cout << value;
    assert(value == "myvalue1");
    if(status.ok()) status = pdb->Delete(leveldb::WriteOptions(), "mykey1");

    delete pdb;

}