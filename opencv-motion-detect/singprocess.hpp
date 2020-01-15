#include <netinet/in.h>

class SingletonProcess {
private:
    uint16_t name2port(string name)
    {
        uint16_t ret = 41070;
        if(name == "evdaemon") {
            //
        }
        else if(name == "evmgr") {
            ret +=100;
        }
        else if(name == "evpuller") {
            ret +=200;
        }
        else if(name == "evpusher") {
            ret +=300;
        }
        else if(name == "evslicer") {
            ret +=400;
        }
        else if(name == "evmlmotion") {
            ret +=500;
        }
        else if(name == "evwifi") {
            ret +=600;
        }
        else {
            ret += 900;
        }

        return ret;
    }
public:
    SingletonProcess(uint16_t port0)
        : socket_fd(-1)
        , rc(1)
        , port(port0)
    {
    }

    SingletonProcess(string moduName, uint16_t iid)
        : socket_fd(-1)
        , rc(1)
    {
        port = name2port(moduName) + iid;
    }

    ~SingletonProcess()
    {
        if (socket_fd != -1) {
            close(socket_fd);
        }
    }

    bool operator()()
    {
        if (socket_fd == -1 || rc) {
            socket_fd = -1;
            rc = 1;

            if ((socket_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
                throw std::runtime_error(std::string("Could not create socket: ") +  strerror(errno));
            }
            else {
                struct sockaddr_in name;
                name.sin_family = AF_INET;
                name.sin_port = htons (port);
                name.sin_addr.s_addr = htonl (INADDR_ANY);
                rc = ::bind(socket_fd, (struct sockaddr *) &name, sizeof (name));
            }
        }
        return (socket_fd != -1 && rc == 0);
    }

    std::string GetLockFileName()
    {
        return std::to_string(port);
    }

private:
    int socket_fd = -1;
    int rc;
    uint16_t port;
};