/*
 * gimli.c
 *    Mines for system information.
 */

#include "gimli.h"


/* Global stats data, updated by the mine() threads. */
gimli_t           gimli;


/**
 * get_cpu_util - get total CPU util from kernel
 *
 * Samples the first line of /proc/stat twice and saves
 * columns 2-5 (skipping the first column 'cpu') two times
 * and saves calculated percentage results in gimli.cpu_util.
 *
 * The values for columns 2-5 in /proc/stat are as follows:
 *
 *     user, nice, system, idle, iowait
 *
 * More info about these values can be found in proc(5).
 *
 */
static status_t
get_cpu_util(gimli_t *gimli)
{
    FILE          *f;
    char           buf[256];
    long double    tot = 0;
    gimli_cpu_t    old = {0}, new = {0}, diff = {0};

    // First poll.
    if ((f = fopen(PROC_STAT, "r")) == NULL) return (G_FAIL);
    if (fgets(buf, sizeof (buf), f) == NULL) return (G_FAIL);
    if (sscanf(buf, CPU_FMT, &old.u, &old.n, &old.s, &old.i, &old.w) < 4)
        return (G_FAIL);
    rewind(f);
    fflush(f);

    usleep(3000000); // wait 3 seconds

    // Second poll.
    if (fgets(buf, sizeof (buf), f) == NULL) return (G_FAIL);
    if (sscanf(buf, CPU_FMT, &new.u, &new.n, &new.s, &new.i, &new.w) < 4)
        return (G_FAIL);
    if (fclose(f) != 0) return (G_FAIL);

    // Calculate diffs.
    diff.u = new.u > old.u ? new.u - old.u : old.u - new.u;
    diff.n = new.n > old.n ? new.n - old.n : old.n - new.n;
    diff.s = new.s > old.s ? new.s - old.s : old.s - new.s;
    diff.i = new.i > old.i ? new.i - old.i : old.i - new.i;
    diff.w = new.w > old.w ? new.w - old.w : old.w - new.w;
    tot = diff.u + diff.n + diff.s + diff.i + diff.w;

    // Calculate final percentages
    gimli->cpu[CPU_USER] = (diff.u / tot) * 100;
    gimli->cpu[CPU_NICE] = (diff.n / tot) * 100;
    gimli->cpu[CPU_SYSTEM] = (diff.s / tot) * 100;
    gimli->cpu[CPU_IDLE] = (diff.i / tot) * 100;
    gimli->cpu[CPU_IOWAIT] = (diff.w / tot) * 100;

    return (G_OK);
}

/**
 * get_loadavg - sample /proc/loadavg for loadavg
 *
 * Save the first three values from /proc/loadavg:
 *
 *    1) load avg of last 1 minute
 *    2) load avg of last 5 minutes
 *    3) load avg of last 15 minutes
 *
 * More info about these values can be found in proc(5).
 *
 */
static status_t
get_loadavg(gimli_t *gimli)
{
    FILE          *f;
    char           buf[256];

    // Read first line of /proc/loadavg and get first 3 values.
    if ((f = fopen(PROC_LOADAVG, "r")) == NULL) return (G_FAIL);
    if (fgets(buf, sizeof (buf), f) == NULL) return (G_FAIL);
    if (sscanf(buf, LOAD_FMT, &gimli->load[0], &gimli->load[1],
                &gimli->load[2]) < 2) {
        return (G_FAIL);
    }
    if (fclose(f) != 0) return (G_FAIL);
    return (G_OK);
}

/**
 * get_meminfo - get system memory info
 *
 * Gathers various memory data from sysinfo() library
 * function and sets the gimli_t struct fields.
 *
 * For more info on memory fields, see sysinfo(2).
 */
static status_t
get_meminfo(gimli_t *gimli)
{
   struct sysinfo meminfo;

   if (sysinfo(&meminfo) < 0) {
       return (G_FAIL);
   }

   gimli->meminfo[TOTAL_RAM]  = (meminfo.totalram * meminfo.mem_unit) / 1024;
   gimli->meminfo[FREE_RAM]   = (meminfo.freeram * meminfo.mem_unit) / 1024;
   gimli->meminfo[SHARED_RAM] = (meminfo.sharedram * meminfo.mem_unit) / 1024;
   gimli->meminfo[BUFFER_RAM] = (meminfo.bufferram * meminfo.mem_unit) / 1024;
   gimli->meminfo[TOTAL_SWAP] = (meminfo.totalswap * meminfo.mem_unit) / 1024;
   gimli->meminfo[FREE_SWAP]  = (meminfo.freeswap * meminfo.mem_unit) / 1024;
   gimli->meminfo[TOTAL_HIGH] = (meminfo.totalhigh * meminfo.mem_unit) / 1024;
   gimli->meminfo[FREE_HIGH]  = (meminfo.freehigh * meminfo.mem_unit) / 1024;
   gimli->meminfo[MEM_UNIT]   = meminfo.mem_unit;
   gimli->procs = meminfo.procs;
   gimli->uptime = meminfo.uptime;

   return (G_OK);
}

/**
 * get_netif - get network interface information
 *
 * Detects all network interfaces and their addresses and Rx/Tx data
 */
static status_t
get_netif(gimli_t *gimli)
{
    struct ifaddrs *ifaddr, *ifa;
    int family, s;
    char ipv4[NI_MAXHOST];

    if (getifaddrs(&ifaddr) == -1) {
        printf("getifaddrs failed\n");
        return (G_FAIL);
    }

    gimli->netifs = 0;
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_INET) {
            family = ifa->ifa_addr->sa_family;
            sprintf(gimli->net[gimli->netifs].ifname, "%s", ifa->ifa_name);
            s = getnameinfo(ifa->ifa_addr,
                    (family == AF_INET) ? sizeof(struct sockaddr_in) :
                    sizeof(struct sockaddr_in6),
                    ipv4, NI_MAXHOST,
                    NULL, 0, NI_NUMERICHOST);
            if (s != 0) {
                printf("getnameinfo failed: %s\n", gai_strerror(s));
                return (G_FAIL);
            }
            sprintf(gimli->net[gimli->netifs].ipv4, "%s", ipv4);
            gimli->netifs++;
        }
        /*
        if (family == AF_PACKET && ifa->ifa_data != NULL) {
            struct rtnl_link_stats *stats = ifa->ifa_data;

            gimli->net[n].tx_packets = stats->tx_packets;
            gimli->net[n].rx_packets = stats->rx_packets;
            gimli->net[n].tx_bytes = stats->tx_bytes;
            gimli->net[n].rx_bytes = stats->rx_bytes;
        }
        */
    }

    freeifaddrs(ifaddr);
    return (G_OK);
}

static void *
thread_create_detached(void *(*func) (void *), void *arg)
{
    pthread_t tid;
    pthread_attr_t attr;

    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&tid, &attr, func, arg);
    return (void *) {0};
}

static void
handle_request(const char *buf, char *output, size_t size)
{
    if (strncmp(buf, "GET /cpu", sizeof ("GET /cpu") - 2) == 0) {
        snprintf(output, size,
                "{" \
                    "\"cpu\":{" \
                        "\"us\":%.1Lf," \
                        "\"sy\":%.1Lf," \
                        "\"id\":%.1Lf," \
                        "\"wa\":%.1Lf," \
                        "\"ni\":%.1Lf" \
                    "}" \
                "}\r\n",
                gimli.cpu[CPU_USER], gimli.cpu[CPU_SYSTEM],
                gimli.cpu[CPU_IDLE], gimli.cpu[CPU_IOWAIT],
                gimli.cpu[CPU_NICE]);
    } else if (strncmp(buf, "GET /load", sizeof ("GET /load") - 2) == 0) {
        snprintf(output, size,
                "{" \
                    "\"load\":[%.2f, %.2f, %.2f]" \
                "}\r\n",
                gimli.load[LOAD_ONE], gimli.load[LOAD_FIVE],
                gimli.load[LOAD_FIFTEEN]);
    } else if (strncmp(buf, "GET /uptime", sizeof ("GET /uptime") - 2) == 0) {
        snprintf(output, size,
                "{" \
                    "\"uptime\":[%lu, %01lu, %02lu]" \
                "}\r\n",
                gimli.uptime/86400, gimli.uptime/3600%24, gimli.uptime/60%60);
    } else if (strncmp(buf, "GET /procs", sizeof ("GET /procs") - 2) == 0) {
        snprintf(output, size,
                "{" \
                    "\"procs\":%hu" \
                "}\r\n",
                gimli.procs);
    } else if (strncmp(buf, "GET /cores", sizeof ("GET /cores") - 2) == 0) {
        snprintf(output, size,
                "{" \
                    "\"cores\":%d" \
                "}\r\n",
                gimli.cores);
    } else if (strncmp(buf, "GET /net", sizeof ("GET /net") - 2) == 0) {
        snprintf(output, size, "{\"netifs\":[");
        for (int i=0; i<gimli.netifs; i++) {
            sprintf(output+strlen(output), IFNAME_JSON, gimli.net[i].ifname,
                    gimli.net[i].ipv4);
            if (i+1 < gimli.netifs) {
                sprintf(output+strlen(output), ",");
            }
        }
        sprintf(output+strlen(output), "]}\r\n");
    } else if (strncmp(buf, "GET / HTTP", sizeof ("GET / HTTP") - 2) == 0) {
        snprintf(output, size,
                "{\n" \
                "    \"cpu\": {\n" \
                "        \"us\": %.1Lf,\n" \
                "        \"sy\": %.1Lf,\n" \
                "        \"id\": %.1Lf,\n" \
                "        \"wa\": %.1Lf,\n" \
                "        \"ni\": %.1Lf\n" \
                "    },\n" \
                "    \"load\": [%.2f, %.2f, %.2f],\n" \
                "    \"uptime\": [%lu, %01lu, %02lu],\n" \
                "    \"procs\": %hu,\n" \
                "    \"cores\": %d,\n",
                gimli.cpu[CPU_USER], gimli.cpu[CPU_SYSTEM],
                gimli.cpu[CPU_IDLE], gimli.cpu[CPU_IOWAIT],
                gimli.cpu[CPU_NICE], gimli.load[LOAD_ONE],
                gimli.load[LOAD_FIVE], gimli.load[LOAD_FIFTEEN],
                gimli.uptime/86400, gimli.uptime/3600%24, gimli.uptime/60%60,
                gimli.procs, gimli.cores);
        snprintf(output+strlen(output), size, "    \"netifs\": [");
        for (int i=0; i<gimli.netifs; i++) {
            if (i==0) {
                sprintf(output+strlen(output), IFNAME_PRETTY_FIRST_JSON,
                        gimli.net[i].ifname, gimli.net[i].ipv4);
            } else {
                sprintf(output+strlen(output), IFNAME_PRETTY_JSON,
                        gimli.net[i].ifname, gimli.net[i].ipv4);
            }
            if (i+1 < gimli.netifs) {
                sprintf(output+strlen(output), ", ");
            }
        }
        sprintf(output+strlen(output), "]\n}\r\n");
    } else {
        snprintf(output, size, "{\"err\": 1}\r\n");
    }
}

static void *
handle_connection(void *arg)
{
    int fd, len;
    char buf[1024];
    char output[2048] = {0};
    size_t size = sizeof (output);

    fd = *((int *) arg);
    len = recv(fd, buf, sizeof (buf), 0);
    if (len <= 0) {
        // Connection lost, gracefully exit.
        return (void *) {0};
    }

    // Trim trailing newline.
    if (buf[len-1] == '\n') {
        buf[len-1] = '\0';
    } else {
        buf[len] = '\0';
    }

    printf("%s\n", buf);
    snprintf(output, size,
            "HTTP/1.1 200 OK\r\n" \
            "Content-Type: application/json; charset=utf-8\r\n" \
            "\r\n");
    if (send(fd, output, strlen(output), MSG_NOSIGNAL) <= 0) {
        return (void *) {0};
    }
    handle_request(buf, output, size);
    if (send(fd, output, strlen(output), MSG_NOSIGNAL) <= 0) {
        return (void *) {0};
    }
    if (fd > 0) {
        shutdown(fd, SHUT_RDWR);
        close(fd);
    }

    return (void *) {0};
}

static void *
handle_connections()
{
    int fd, newfd;
    struct sockaddr_in svr_addr, peer_addr;
    socklen_t peer_addr_size;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) {
        printf("Couldn't create socket: %m\n");
        exit(1);
    }
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &(int){ 1 },
                sizeof (int)) < 0) {
        printf("setsockopt(SO_REUSEADDR) failed\n");
        exit(1);
    }

    memset(&svr_addr, 0, sizeof(struct sockaddr_in));
    svr_addr.sin_addr.s_addr = INADDR_ANY;
    svr_addr.sin_family = AF_INET;
    svr_addr.sin_port = htons(SERVER_PORT);

    if (bind(fd, (struct sockaddr *) &svr_addr,
                sizeof(struct sockaddr_in)) == -1) {
        printf("Couldn't bind to socket: %m\n");
        close(fd);
        exit(1);
    }

    /* Listen on specified port. */
    if (listen(fd, 5) == -1) {
        printf("Couldn't listen to port: %m\n");
        close(fd);
        exit(1);
    }
    printf("Listening at: 127.0.0.1:%d (%d)\n", SERVER_PORT, (int) getpid());

    /* Accept connections. */
    peer_addr_size = sizeof(struct sockaddr_in);
    while ((newfd = accept(fd, (struct sockaddr *) &peer_addr,
                    &peer_addr_size))) {
        if (newfd != -1) {
            printf("Incoming connection from %s:%d, fd=%d\n",
                    inet_ntoa(peer_addr.sin_addr), ntohs(peer_addr.sin_port),
                    newfd);
            int localfd = newfd;
            thread_create_detached(&handle_connection, (void *) &localfd);
        }
    }

    /* Never reached. */
    close(fd);
    return (void *) {0};
}

void *
gimli_mine_cpu()
{
    gimli.cores = sysconf(_SC_NPROCESSORS_CONF);
    while (1) {
        if (get_cpu_util(&gimli) != G_OK) {
            printf("get_cpu_util failed\n");
        }
    }
}

void *
gimli_mine_load()
{
    while (1) {
        if (get_loadavg(&gimli) != G_OK) {
            printf("get_loadavg failed\n");
        }
        sleep(1);
    }
}

void *
gimli_mine_meminfo()
{
    while (1) {
        if (get_meminfo(&gimli) != G_OK) {
            printf("get_meminfo failed\n");
        }
        sleep(1);
    }
}

void *
gimli_mine_netif()
{
    while (1) {
        if (get_netif(&gimli) != G_OK) {
            printf("get_netif failed\n");
        }
        sleep(1);
    }
}

static void
daemonize(void)
{
    pid_t pid;

    // Fork off the parent process.
    pid = fork();

    // An error occurred.
    if (pid < 0)
        exit(EXIT_FAILURE);

    // Success: Let the parent terminate.
    if (pid > 0)
        exit(EXIT_SUCCESS);

    // On success: The child process becomes session leader.
    if (setsid() < 0)
        exit(EXIT_FAILURE);

    // Catch, ignore and handle signals.
    //TODO: Implement a working signal handler */
    signal(SIGCHLD, SIG_IGN);
    signal(SIGHUP, SIG_IGN);

    // Fork off for the second time.
    pid = fork();

    // An error occurred.
    if (pid < 0)
        exit(EXIT_FAILURE);

    // Success: Let the parent terminate.
    if (pid > 0)
        exit(EXIT_SUCCESS);

    // Set new file permissions.
    umask(0);

    // Change the working directory to the root directory
    // or another appropriated directory.
    chdir("/");

    // Close all open file descriptors
    for (int x = sysconf(_SC_OPEN_MAX); x>=0; x--) {
        close (x);
    }

    // Open the log file. -> /var/log/syslog -> [gimli]
    // openlog ("gimli", LOG_PID, LOG_DAEMON);
}

int
main(int argc, char *argv[])
{
    if (argc == 2) {
        if (strcmp(argv[1], "--daemon") == 0) {
            /* Become a daemon. */
            daemonize();
        } else {
            printf("usage: gimli [--daemon]\n");
            exit(1);
        }
    }

    /* Start the mine threads to gather system information. */
    thread_create_detached(&gimli_mine_cpu, NULL);
    thread_create_detached(&gimli_mine_load, NULL);
    thread_create_detached(&gimli_mine_meminfo, NULL);
    thread_create_detached(&gimli_mine_netif, NULL);

    /* Start main program loop. */
    handle_connections();

    /* Never reached. */
    if (argc == 2) {
        closelog();
    }
    return (0);
}
