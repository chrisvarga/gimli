/*
 * gimli.h
 *   Headers and definitions for gimli.c
 */

#ifndef GIMLI_H
#define GIMLI_H

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdint.h>
#include <signal.h>
#include <syslog.h>
#include <pthread.h>
#include <sys/sysinfo.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <linux/if_link.h>
#include <linux/if.h>


#define PROC_STAT    "/proc/stat"
#define PROC_LOADAVG "/proc/loadavg"
#define PROC_UPTIME  "/proc/uptime"

#define MILLION      1000000L
#define BILLION      1000000000L

// 42? Not bad for an elvish princeling.
// I happen to be sitting comfortably at 43.
#define SERVER_PORT  8043

#define CPU_FMT      "cpu %Lu %Lu %Lu %Lu %Lu"
#define LOAD_FMT     "%f %f %f"

#define IFNAME_JSON "{\"ifname\":\"%s\",\"ipv4\":\"%s\"}"
#define IFNAME_PRETTY_JSON          "{\n"\
                                    "        \"ifname\": \"%s\",\n"\
                                    "        \"ipv4\": \"%s\"\n"\
                                    "    }"
#define IFNAME_PRETTY_FIRST_JSON    "{\n"\
                                    "        \"ifname\": \"%s\",\n"\
                                    "        \"ipv4\": \"%s\"\n"\
                                    "    }"

enum cpu_util {
    CPU_USER       = 0,
    CPU_NICE       = 1,
    CPU_SYSTEM     = 2,
    CPU_IDLE       = 3,
    CPU_IOWAIT     = 4,
    CPU_NRSTATS    = 5,
};

enum cpu_loadavg {
    LOAD_ONE       = 0,
    LOAD_FIVE      = 1,
    LOAD_FIFTEEN   = 2,
    LOAD_NRSTATS   = 3
};

enum meminfo {
    TOTAL_RAM      = 0,
    FREE_RAM       = 1,
    SHARED_RAM     = 2,
    BUFFER_RAM     = 3,
    TOTAL_SWAP     = 4,
    FREE_SWAP      = 5,
    TOTAL_HIGH     = 6,
    FREE_HIGH      = 7,
    MEM_UNIT       = 8,
    MEM_NRSTATS    = 9
};

typedef enum {
    G_OK           = 0,
    G_FAIL         = 1
} status_t;

typedef struct {
    unsigned long long u, n, s, i, w;
} gimli_cpu_t;

typedef struct {
    char ifname[IFNAMSIZ];
    char ipv4[NI_MAXHOST];
    // char family[NI_MAXSERV];
    // unsigned tx_packets;
    // unsigned rx_packets;
    // unsigned tx_bytes;
    // unsigned rx_bytes;
} gimli_net_t;

typedef struct {
    int            cores;                     // number of cpu's
    long double    cpu[CPU_NRSTATS];          // in percentages
    float          load[LOAD_NRSTATS];        // straight from /proc/loadavg
    unsigned long  meminfo[MEM_NRSTATS];      // system memory info in bytes
    double         memuse;                    // system memory usage as percent
    unsigned long  uptime;                    // system uptime in seconds
    unsigned short procs;                     // number of current processes
    gimli_net_t    net[255];                  // network interface information
    unsigned       netifs;                    // number of network interfaces
} gimli_t;

#endif /* GIMLI_H */
