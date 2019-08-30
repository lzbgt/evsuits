/*
module: evdaemon
description: to monitor and configure all other components. runs only one instance per host.
author: Bruce.Lu <lzbgt@icloud.com>
update: 2019/08/30
*/

#include "inc/tinythread.hpp"
#include "inc/httplib.h"
#include "inc/zmqhelper.hpp"
#include "inc/database.h"


class EvDaemon: TinyThread{
    private:
    protected:
    void run(){

    }
    public:
    EvDaemon();
    ~EvDaemon();
};

int main(){

}