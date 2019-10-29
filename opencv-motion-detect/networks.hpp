#ifndef __EV_NETWORKS_H__
#define __EV_NETWORKS_H__

#include <sys/types.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <string>
#include <arpa/inet.h>
#include <map>
#include <spdlog/spdlog.h>


namespace netutils {
using namespace std;
map<string, string> getIps()
{
    map<string, string> ret;
    struct ifaddrs * ifAddrStruct=nullptr;
    struct ifaddrs * ifa=nullptr;
    void * tmpAddrPtr=nullptr;
    char addressBuffer[INET_ADDRSTRLEN];

    getifaddrs(&ifAddrStruct);

    for (ifa = ifAddrStruct; ifa != nullptr; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr) {
            continue;
        }
        if (ifa->ifa_addr->sa_family == AF_INET) { // check it is IP4
            // is a valid IP4 Address
            tmpAddrPtr=&((struct sockaddr_in *)ifa->ifa_addr)->sin_addr;
            int ip = htonl(*(int*)tmpAddrPtr);
            // printf("%d %d %d %d", ( ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip>> 8) & 0xFF, (ip >> 0) & 0xFF);
            if(((ip >> 24) & 0xFF) == 127) {
                continue;
            }

            if(inet_ntop(AF_INET, tmpAddrPtr, addressBuffer, INET_ADDRSTRLEN) != nullptr) {
                string addrStr(addressBuffer);
                string ifStr(ifa->ifa_name);
                ret[addrStr] = ifStr;
            }
        }
        // else if (ifa->ifa_addr->sa_family == AF_INET6) { // check it is IP6
        //     // is a valid IP6 Address
        //     tmpAddrPtr=&((struct sockaddr_in6 *)ifa->ifa_addr)->sin6_addr;
        //     char addressBuffer[INET6_ADDRSTRLEN];
        //     inet_ntop(AF_INET6, tmpAddrPtr, addressBuffer, INET6_ADDRSTRLEN);
        // }  
    }
    if (ifAddrStruct!=nullptr) freeifaddrs(ifAddrStruct);
    return ret;
}
}


#endif