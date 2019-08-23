/*
module: tinythread
description: 
author: Bruce.Lu <lzbgt@icloud.com>
update: 2019/08/23
*/

#ifndef __TINY_THREAD__
#define __TINY_THREAD__

#include <thread>
#include <iostream>
#include <chrono>
#include <future>

using namespace std;


class TinyThread {
    std::promise<void> exitSignal;
    std::future<void> futureObj;
    int state = 0;
    thread th;
protected:
    // Task need to provide defination  for this function
    // It will be called by thread function
    virtual void run() = 0;
public:
    TinyThread() :
        futureObj(exitSignal.get_future())
    {

    }
    TinyThread(TinyThread && obj) : exitSignal(std::move(obj.exitSignal)), futureObj(std::move(obj.futureObj))
    {
        std::cout << "Move Constructor is called" << std::endl;
    }
    TinyThread & operator=(TinyThread && obj)
    {
        std::cout << "Move Assignment is called" << std::endl;
        exitSignal = std::move(obj.exitSignal);
        futureObj = std::move(obj.futureObj);
        return *this;
    }

    // Thread function to be executed by thread
private:
    void _run()
    {
        if(state == 0) {
            th =thread([&]() {
                this->run();
            });
            state = 1;
        }
    }

public:
    //Checks if thread is requested to stop
    bool checkStop()
    {
        // checks if value in future object is available
        if (futureObj.wait_for(std::chrono::milliseconds(0)) == std::future_status::timeout)
            return false;
        return true;
    }
    // Request the thread to stop by setting value in promise object
    void stop()
    {
        exitSignal.set_value();
    }

    void join()
    {
        _run();
        if(th.joinable()) {
            th.join();
        }
    }

    void detach()
    {
        _run();
        if(th.joinable()) {
            th.detach();
        }
    }
};

#endif