/**
 * dhclient lease parser by Travis Hein
 * licensed under Apache License, Version 2.0
 */
#include <string>
#include <map>
#include <vector>
#include <list>
#include <fstream>
#include <iostream>
#include <sstream>
#include <cstdlib>
#include <string.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/ioctl.h>

/* Include sockio.h if needed */
#ifndef SIOCGIFCONF
#include <sys/sockio.h>
#endif

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <net/if.h>
#include <netdb.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>

#include <readline/readline.h>

#define MIN(x,y) (y)^(((x) ^ (y)) &  - ((x) < (y)))
#define MAX(x,y) (y)^(((x) ^ (y)) &  - ((x) > (y)))

#define SERVERPORT "8888"
#define BUFSIZE 500
#define MAX_SUBSTREAMS 5

using namespace std;

string DEFAULT_LEASE_DIR="/var/lib/dhcp3/";
string DEFAULT_LEASE_FILE="/var/lib/dhcp3/dhclient.leases";

extern "C"{
    void printLeases_C(struct dhcp_lease_c*);
    dhcp_lease_c* leaseParser_C(const char*);
    struct dhcp_lease_c;
}

struct dhcp_lease_c{
    const char*         interface;
    const char*           address;
    const char*           gateway;
    const char*           netmask;
    time_t                rebind ;
    time_t                expire ;
    time_t                renew  ;
    struct dhcp_lease_c*  next   ;
};

struct dhcp_lease {
    string interface;
    string address;
    string gateway;
    map<string, list<string> > options;
    time_t renew ;
    time_t rebind;
    time_t expire;
    dhcp_lease() {}
};

/*
 * this is straight from beej's network tutorial. It is a nice wrapper
 * for inet_ntop and helpes to make the program IPv6 ready
 */
char*
get_ip_str(const struct sockaddr *sa, char *s, size_t maxlen)
{
  switch(sa->sa_family) {
  case AF_INET:
    inet_ntop(AF_INET, &(((struct sockaddr_in *)sa)->sin_addr),
              s, maxlen);
    break;

  case AF_INET6:
    inet_ntop(AF_INET6, &(((struct sockaddr_in6 *)sa)->sin6_addr),
              s, maxlen);
    break;

  default:
    strncpy(s, "Unknown AF", maxlen);
    return NULL;
  }

  return s;
}

int getdir (string dir, vector<string> &files)
{
    DIR *dp;
    struct dirent *dirp;
    if((dp = opendir(dir.c_str())) == NULL)
    {
        cout << "Error(" << errno << ") opening " << dir << endl;
        return errno;
    }

    while ((dirp = readdir(dp)) != NULL)
    {
        if( (strcmp(dirp->d_name,".") == 0) || (strcmp(dirp->d_name, "..") == 0) ){
            continue;
        }

        cout << dirp->d_name << endl;
        files.push_back(string(dirp->d_name));
    }

    closedir(dp);
    return 0;
}

time_t to_seconds(const char *date)
{
    struct tm storage={0,0,0,0,0,0,0,0,0};
    char *p=NULL;
    time_t retval=0;

    p=(char *)strptime(date,"%Y/%m/%d %H:%M:%S",&storage);
    if(p==NULL)
    {
        retval=0;
    }
    else
    {
        retval=mktime(&storage);
    }
}

void
leaseParser(const char* leasesFileName, vector<dhcp_lease> resultLeases)
{

    ifstream leasesFile;

    leasesFile.open(leasesFileName);

    if (!leasesFile) {
        cout << "Cannot open dhcp leases file: " << leasesFileName << "\n";
        exit(-1);
    }

    string line = "";
    char c;
    bool inLease = false;
    int leaseCount = 0;
    dhcp_lease currentLease;
    bool isInFilter;
    vector<dhcp_lease>::iterator it;

    while (leasesFile) {
        leasesFile.get(c);
        switch (c) {
        case '{':
            inLease = true;
            break;

        case '}':
            inLease = false;

            for( it = resultLeases.begin(); it != resultLeases.end(); it++)
            {
                if( currentLease.address.compare(it->address) == 0  &&
                    currentLease.renew > it->renew)
                {
                    it->gateway  = currentLease.gateway;
                    it->renew    = currentLease.renew;
                    goto done;
                }
            }

        done:
            currentLease = dhcp_lease();
            break;

        case '\n':
            if (inLease) {
                if (line.length() > 0) {
                    int startOfs = line.find_first_not_of(" \t");
                    int endOfs = line.find_last_of(";");
                    // trim extra stuff off from line
                    line = line.substr(startOfs, endOfs-startOfs);

                    if (line.find("interface") != string::npos){
                        string valstr = line.substr(line.find("interface") + 6);
                        int startOfs = valstr.find_first_of("\"");
                        int endOfs   = valstr.find_last_of("\"");
                        valstr = valstr.substr(startOfs+1, endOfs - startOfs-1);
                        currentLease.interface = valstr;
                    }
                    else if (line.find("option") != string::npos ) {
                        // drop the option keyword
                        line = line.substr(7);
                        size_t ofs = line.find_first_of(" ");

                        // extract the option name
                        string key = line.substr(0,  ofs);

                        // extract the comma delimited values

                        list<string> values;
                        line = line.substr(ofs+1);
                        while (line.length()) {
                            ofs = line.find_first_of(',');
                            string valStr;
                            if (ofs == string::npos) {
                                ofs = 0;
                                valStr = line;
                                line = "";
                            }
                            else {
                                valStr = line.substr(0, ofs);
                                line = line.substr(ofs+1);
                            }
                            values.push_back(valStr);
                        } // while
                        pair<string, list<string> > p(key,values);
                        currentLease.options.insert(p);
                    }
                    else if (line.find("fixed-address") != string::npos) {

                        string valstr = line.substr(line.find("fixed-address") + 13);
                        int startOfs = valstr.find_first_not_of(" \t");
                        int endOfs = valstr.find_last_of(";");
                        valstr = valstr.substr(startOfs, endOfs-startOfs);
                        list<string> values;
                        values.push_back(valstr);
                        pair<string, list<string> > p("fixed-address", values);
                        currentLease.options.insert(p);
                        currentLease.address = valstr;
                    }
                    else if (line.find("renew") != string::npos) {
                        currentLease.renew =
                            to_seconds(line.substr(line.find("renew") + 8).c_str());
                    }
                    else if (line.find("rebind") != string::npos) {
                        currentLease.rebind =
                            to_seconds(line.substr(line.find("rebind") + 9).c_str());;
                    }
                    else if (line.find("expire") != string::npos) {
                        currentLease.expire =
                            to_seconds(line.substr(line.find("expire") + 9).c_str());
                    }

                    // reset the line string
                    line = "";
                }
            } // if in a lease
            break;

        default:
            if (inLease) {
                line += c;
            }
        }
    } // while lease file has values
}

void
printLeases(list<dhcp_lease> leases, list<string> options)
{
    // retreive the last lease in the file (tokenize entire contents by { and } delimiters
    // (which is now the first one in our list of parsed leases)
    if (leases.size() > 0) {

        list<dhcp_lease>::iterator itr;

        for( itr = leases.begin(); itr != leases.end(); itr++){
            cout << "\n*******************\n";
            dhcp_lease lease = *itr;

            cout << "Interface: " << itr->interface << endl;
            cout << "Address: "   << itr->address   << endl;
            cout << "Expire: "    << itr->expire    << endl;

            // get the requested option(s)
            list<string>::iterator optionsIterator = options.begin();
            while (optionsIterator != options.end() ) {
                string getOption = *optionsIterator;

                map<string, list<string> >::iterator valItr = lease.options.find(getOption);

                if (valItr != lease.options.end()) {
                    pair<string, list<string> > p = *valItr;
                    list<string> valList = p.second;
                    list<string>::iterator itmItr = valList.begin();


                    string envStr = getOption + "=" + *itmItr;
                    cout << envStr << ";" << endl;
                }
                else {
                    // this option could not be found in the lease
                }
                optionsIterator++;
            }
        }
    }
}

const char*
getOption(dhcp_lease lease, const char* opt)
{
    map<string, list<string> >::iterator valItr = lease.options.find(opt);
    if (valItr != lease.options.end()) {
        pair<string, list<string> > p = *valItr;
        list<string> valList = p.second;
        list<string>::iterator itmItr = valList.begin();
        return itmItr->c_str();
    }

    return NULL;
}

vector<dhcp_lease>
toList(dhcp_lease_c* leases_itr)
{
    list<dhcp_lease> list;
    dhcp_lease curLease;

    do{
        curLease = dhcp_lease();
        if( leases_itr->interface == NULL) break;
        curLease.interface = leases_itr->interface;
        curLease.address   = leases_itr->address;
        curLease.gateway   = leases_itr->gateway;
        curLease.renew     = leases_itr->renew;
        curLease.rebind    = leases_itr->rebind;
        curLease.expire    = leases_itr->expire;

        list.push_back(curLease);

    }while( (leases_itr = leases_itr->next) != NULL);
}

dhcp_lease_c* leaseParser_C(dhcp_lease_c* filter_C, const char* leasesFileName)
{
    vector<dhcp_lease> filter = toList(filter_C);
    leaseParser(leasesFileName, filter);
    dhcp_lease_c* leases_c   = (dhcp_lease_c*) malloc(sizeof (dhcp_lease_c));
    dhcp_lease_c* leases_itr = leases_c;

    vector<dhcp_lease>::iterator it;
    for( it = filter.begin(); it != filter.end(); it++ )
    {
        leases_itr->interface = it->interface.c_str();
        leases_itr->address   = it->address.c_str();
        leases_itr->renew     = it->renew;
        leases_itr->rebind    = it->rebind;
        leases_itr->expire    = it->expire;

        leases_itr->gateway   = getOption(*it, "routers");
        leases_itr->netmask   = getOption(*it, "subnet-mask");

        leases_itr->next      = (dhcp_lease_c*) malloc(sizeof (dhcp_lease_c));
        leases_itr            = leases_itr->next;
    }

    leases_itr->next = NULL; // Mark the tail of the list
    free(leases_itr);
    return leases_c;
}

void printLeases_C(dhcp_lease_c* lease)
{
    dhcp_lease_c* leases_itr = (dhcp_lease_c*) malloc(sizeof (dhcp_lease_c));
    leases_itr = lease;

    do{
        if( leases_itr->interface == NULL) break;

        printf("Interface %s\nAddress %s\nGateway %s\nNetmask %s\nRenew %jd\nRebind %jd\nExpire %jd\n****************\n",
               leases_itr->interface,
               leases_itr->address,
               leases_itr->gateway,
               leases_itr->netmask,
               leases_itr->renew,
               leases_itr->rebind,
               leases_itr->expire);
    }while( (leases_itr = leases_itr->next) != NULL);

    free(leases_itr);
}

int
getDeviceInfo(vector<dhcp_lease_c*>& info){
    int activeSockets = 0;
    int c;
    char* host = "127.0.0.1";
    int sockfd;
    int substreams = 1;

    struct sockaddr srvAddr;
    socklen_t srvAddrLen = sizeof srvAddr;
    char            ip[INET6_ADDRSTRLEN] = {0};
    struct addrinfo hints, *servInfo;
    struct addrinfo *result;
    int numbytes;
    int rv;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;

    if((rv = getaddrinfo(host, SERVERPORT, &hints, &servInfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    int             i            = 0;
    int             nInterfaces  = 0;
    char            buf[8192]    = {0};
    struct ifconf   ifc          = {0};
    struct ifreq    *ifr         = NULL;
    struct ifreq    *item;
    struct sockaddr *addr;
    char            hostname[NI_MAXHOST];

    // loop through all the results and bind to the first possible
loop:
    for(result = servInfo; result != NULL; result = result->ai_next) {
        printf("Initializing socket %d\n", i);
        if((sockfd = socket(result->ai_family,
                               result->ai_socktype,
                               result->ai_protocol)) == -1)
        {
            perror("failed to initialize sokcket %d\n");
            goto loop;
        }
        break;
    }

    // If we got here, it means that we couldn't initialize the socket.
    if(result  == NULL){
        perror("failed to create a socket");
        exit(1);
    }

    // Query available interfaces.
    ifc.ifc_len = sizeof(buf);
    ifc.ifc_buf = buf;
    if(ioctl(sockfd, SIOCGIFCONF, &ifc) < 0)
    {
        perror("ioctl(SIOCGIFCONF)");
        exit(1);
    }

    // Iterate through the list of interfaces.
    ifr = ifc.ifc_req;
    nInterfaces = ifc.ifc_len / sizeof(struct ifreq);
    printf("We have a total of %d interfaces\n", nInterfaces-1); // ignore lo
    for(i = 0; i < nInterfaces; i++) {
        memset(hostname, 0, NI_MAXHOST);

        item = &ifr[i];

        if(strcmp(item->ifr_name,"lo") == 0) continue;

        dhcp_lease_c* lease = (dhcp_lease_c*) malloc(sizeof(dhcp_lease_c));
        lease->interface = item->ifr_name;

        // Get the address
        // This may seem silly but it seems to be needed on some systems
        if(ioctl(sockfd, SIOCGIFADDR, item) < 0) {
            perror("ioctl(SIOCGIFADDR)");
            exit(1);
        }

        lease->address   = strdup(get_ip_str(&(item->ifr_addr), ip, INET6_ADDRSTRLEN));

        if(ioctl(sockfd, SIOCGIFNETMASK, item) < 0) {
            perror("ioctl(SIOCGIFADDR)");
            exit(1);
        }

        lease->netmask   = strdup(get_ip_str(&(item->ifr_netmask), ip, INET6_ADDRSTRLEN));

        info.push_back( lease );

    }
}

int
main(int argc, char** argv)
{
    vector<dhc

    dhcp_lease_c* leases = leaseParser_C(DEFAULT_LEASE_FILE.c_str());
    printLeases_C(leases);
}
