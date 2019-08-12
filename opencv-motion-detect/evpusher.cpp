#pragma GCC diagnostic ignored "-Wunused-private-field"
#pragma GCC diagnostic ignored "-Wunused-variable"


#include <stdlib.h>
#include <string>
#include <thread>
#include <iostream>
#include <chrono>
#include <future>

#ifdef OS_LINUX
#include <filesystem>
namespace fs = std::filesystem;
#endif

#include  "vendor/include/zmq.h"
#include "inc/json.hpp"
#include "inc/blockingconcurrentqueue.hpp"
#include "inc/tinythread.hpp"
#include "inc/common.hpp"

using namespace std;
using json = nlohmann::json;
using namespace moodycamel;

class PacketPusher: public TinyThread {
private:
// 2M
#define MAX_ZMQ_MSG_SIZE 1204 * 1024 * 2
    void *pSubContext = NULL; // for packets relay
    void *pSubscriber = NULL;
    int setupMq()
    {
        teardownMq();
        int ret = 0;
        pSubContext = zmq_ctx_new();
        pSubscriber = zmq_socket(pSubContext, ZMQ_SUB);
        ret = zmq_setsockopt(pSubscriber, ZMQ_SUBSCRIBE, "", 0);
        if(ret != 0) {
            logThrow(NULL, AV_LOG_FATAL, "failed connect to pub");
        }
        ret = zmq_connect(pSubscriber, "tcp://localhost:5556");
        if(ret != 0) {
            logThrow(NULL, AV_LOG_FATAL, "failed create sub");
        }
        
        return 0;
    }

    int teardownMq()
    {
        if(pSubscriber != NULL) {
            zmq_close(pSubscriber);
        }
        if(pSubscriber != NULL) {
            zmq_ctx_destroy(pSubscriber);
        }
        return 0;
    }
protected:
    void run()
    {
        int ret = 0;
        bool bStopSig = false;
        zmq_msg_t msg;
        av_log_set_level(AV_LOG_DEBUG);
        while (true) {
            if(checkStop() == true) {
                bStopSig = true;
                break;
            }
            int ret =zmq_msg_init(&msg);
            if(ret != 0) {
                av_log(NULL, AV_LOG_ERROR, "failed to init zmq msg");
                continue;
            }
            ret = zmq_recvmsg(pSubscriber, &msg, 0);
            if(ret < 0) {
                av_log(NULL, AV_LOG_ERROR, "failed to recv zmq msg");
                continue;
            }

            av_log(NULL, AV_LOG_DEBUG, "msg size: %d, %d", ret, zmq_msg_size(&msg));
            zmq_msg_close(&msg);
        }
        if(!bStopSig && ret < 0) {
            //TOOD: reconnect
            av_log(NULL, AV_LOG_ERROR, "TODO: failed, reconnecting");
        }else {
            av_log(NULL, AV_LOG_INFO, "exit on command");
        }
    }

public:
    PacketPusher()
    {
        setupMq();
    }
    ~PacketPusher()
    {
        teardownMq();
    }
};

int main(int argc, char *argv[]){
    
    PacketPusher pusher;
    pusher.join();
}