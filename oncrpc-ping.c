/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <errno.h>
#include <fcntl.h>
#include <float.h>
#include <getopt.h>
#include <math.h>
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <rpc/rpc.h>

#define VERSION "1.0.0"

/* define usage function */
static void usage(void) {
    printf(
        "ONC RPC Ping - Version %s\n"
        "usage: oncrpc-ping -n|--hostname <target hostname / IP>\n"
        "                   -P|--port <port number>\n"
        "                   -p|--program-number <RPC program number>\n"
        "                   -v|--program-version <RPC program version>\n"
        "                   -T|--transport tcp|udp\n"
        "                   [-i|--interval <second(s)>] default: 1 second\n"
        "                   [-t|--rpc-timeout <second(s)>] default: 10 seconds\n"
        "                   [-c|--connect-timeout <second(s)>] default: 5 seconds\n"
        "                   [-C|--count <count number>]\n"
        "                   [-h|--help]\n", VERSION
    );
}

/* define SIGINT signal handler */
static volatile sig_atomic_t break_flag = 0;
static void sigint_handler(int signo) {
    (void)signo;

    break_flag = 1;
}

/* rtt calculation function */
static double calculate_rtt(struct timespec *start, struct timespec *end) {
    double start_ms = (double)start->tv_sec * 1000.0 + (double)start->tv_nsec / 1000000.0;
    double end_ms = (double)end->tv_sec * 1000.0 + (double)end->tv_nsec / 1000000.0;
    return (end_ms - start_ms);
}

/* RPC TCP client */
static CLIENT *rpc_tcp_client(int input_socket, struct netbuf *input_netbuf, rpcprog_t input_program_number, rpcvers_t input_program_version) {
    return clnt_vc_create(input_socket, input_netbuf, input_program_number, input_program_version, 0, 0);
}

/* RPC UDP client */
static CLIENT *rpc_udp_client(int input_socket, struct netbuf *input_netbuf, rpcprog_t input_program_number, rpcvers_t input_program_version) {
    return clnt_dg_create(input_socket, input_netbuf, input_program_number, input_program_version, 0, 0);
}

/* get RPC session XID */
static void get_rpc_xid(CLIENT *input_client, char *input_rpc_xid_string, size_t buffer_length) {
    unsigned int rpc_xid;

    /* clear RPC XID string */
    memset(input_rpc_xid_string, 0, buffer_length);

    if (clnt_control(input_client, CLGET_XID, (char *)&rpc_xid)) {
        snprintf(input_rpc_xid_string, buffer_length, "0x%08X", rpc_xid);
    } else {
        snprintf(input_rpc_xid_string, buffer_length, "UNKNOWN");
    }
}

int main(int argc, char *argv[]) {
    /* define command-line options */
    char *short_opts = "n:P:p:v:T:i:t:c:C:h";
    struct option long_opts[] = {
        {"hostname", required_argument, NULL, 'n'},
        {"port", required_argument, NULL, 'P'},
        {"program-number", required_argument, NULL, 'p'},
        {"program-version", required_argument, NULL, 'v'},
        {"transport", required_argument, NULL, 'T'},
        {"interval", required_argument, NULL, 'i'},
        {"rpc-timeout", required_argument, NULL, 't'},
        {"connect-timeout", required_argument, NULL, 'c'},
        {"count", required_argument, NULL, 'C'},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0}
    };

    /* intermediate variable for conversion */
    long int parsed_val;

    /* option variables */
    char *hostname = NULL;
    char *port = NULL;
    rpcprog_t program_number = 0;
    rpcvers_t program_version = 0;
    char *transport = NULL;
    long int count = -1;

    /* default values for options */
    unsigned int interval = 1;
    long int rpc_timeout = 10;
    long int connect_timeout = 5;

    /* suppress default getopt error messages */
    opterr = 0;

    int c;

    for (;;) {
        c = getopt_long(argc, argv, short_opts, long_opts, NULL);

        if (c == -1) {
            break;
        }

        switch (c) {
            case 'n':
                hostname = optarg;

                break;
            case 'P':
                port = optarg;

                break;
            case 'p':
                errno = 0;
                parsed_val = strtol(optarg, NULL, 10);

                if (errno != 0) {
                    fprintf(stderr, "ERROR: failed to convert program number value\n\n");
                    exit(EXIT_FAILURE);
                }

                if (parsed_val <= 0) {
                    fprintf(stderr, "ERROR: RPC program number must be an integer and greater than 0\n\n");
                    usage();
                    exit(EXIT_FAILURE);
                }

                program_number = (rpcprog_t)parsed_val;

                break;
            case 'v':
                errno = 0;
                parsed_val = strtol(optarg, NULL, 10);

                if (errno != 0) {
                    fprintf(stderr, "ERROR: failed to convert program version value\n\n");
                    exit(EXIT_FAILURE);
                }

                if (parsed_val <= 0) {
                    fprintf(stderr, "ERROR: RPC program version must be an integer and greater than 0\n\n");
                    usage();
                    exit(EXIT_FAILURE);
                }

                program_version = (rpcvers_t)parsed_val;

                break;
            case 'T':
                transport = optarg;

                if (strcmp(transport, "tcp") != 0 && strcmp(transport, "udp") != 0) {
                    fprintf(stderr, "ERROR: transport type must be either tcp or udp\n\n");
                    usage();
                    exit(EXIT_FAILURE);
                }

                break;
            case 'i':
                errno = 0;
                parsed_val = strtol(optarg, NULL, 10);

                if (errno != 0) {
                    fprintf(stderr, "ERROR: failed to convert interval value\n\n");
                    exit(EXIT_FAILURE);
                }

                if (parsed_val <= 0) {
                    fprintf(stderr, "ERROR: interval must be an integer and greater than 0\n\n");
                    usage();
                    exit(EXIT_FAILURE);
                }

                interval = (unsigned int)parsed_val;

                break;
            case 't':
                errno = 0;
                rpc_timeout = strtol(optarg, NULL, 10);

                if (errno != 0) {
                    fprintf(stderr, "ERROR: failed to convert RPC timeout value\n\n");
                    exit(EXIT_FAILURE);
                }

                if (rpc_timeout <= 0) {
                    fprintf(stderr, "ERROR: RPC timeout must be an integer and greater than 0\n\n");
                    usage();
                    exit(EXIT_FAILURE);
                }

                break;
            case 'c':
                errno = 0;
                connect_timeout = strtol(optarg, NULL, 10);

                if (errno != 0) {
                    fprintf(stderr, "ERROR: failed to convert TCP connect timeout value\n\n");
                    exit(EXIT_FAILURE);
                }

                if (connect_timeout <= 0) {
                    fprintf(stderr, "ERROR: TCP connect timeout must be an integer and greater than 0\n\n");
                    usage();
                    exit(EXIT_FAILURE);
                }

                break;
            case 'C':
                errno = 0;
                count = strtol(optarg, NULL, 10);

                if (errno != 0) {
                    fprintf(stderr, "ERROR: failed to convert count value\n\n");
                    exit(EXIT_FAILURE);
                }

                if (count <= 0) {
                    fprintf(stderr, "ERROR: count must be an integer and greater than 0\n\n");
                    usage();
                    exit(EXIT_FAILURE);
                }

                break;
            case 'h':
                usage();
                exit(EXIT_SUCCESS);
            case '?':
                fprintf(stderr, "ERROR: Unknown option\n\n");
                usage();
                exit(EXIT_FAILURE);
            default:
                fprintf(stderr, "ERROR: Unimplemented option\n\n");
                usage();
                exit(EXIT_FAILURE);
        }
    }

    if (!hostname || !port || !program_number || !program_version || !transport) {
        usage();
        exit(EXIT_FAILURE);
    }

    /* initialize signal-related variables */
    struct sigaction sa;
    sigset_t signal_empty_set;
    sigset_t signal_block_set;

    if (sigemptyset(&signal_empty_set) < 0) {
        fprintf(stderr, "ERROR: failed to clear signal set signal_empty_set\n");
        exit(EXIT_FAILURE);
    }

    if (sigemptyset(&signal_block_set) < 0) {
        fprintf(stderr, "ERROR: failed to clear signal set signal_block_set\n");
        exit(EXIT_FAILURE);
    }

    /* add SIGINT signal in signal_block_set */
    if (sigaddset(&signal_block_set, SIGINT) < 0) {
        fprintf(stderr, "ERROR: failed to add SIGINT signal in signal_block_set\n");
        exit(EXIT_FAILURE);
    }

    /* block SIGINT signal */
    if (sigprocmask(SIG_BLOCK, &signal_block_set, NULL) < 0) {
        fprintf(stderr, "ERROR: failed to block SIGINT signal\n");
        exit(EXIT_FAILURE);
    }

    /* install signal handler */
    sa.sa_handler = sigint_handler;
    sa.sa_flags = 0;

    if (sigemptyset(&sa.sa_mask) < 0) {
        fprintf(stderr, "ERROR: failed to clear signal set sa.sa_mask\n");
        exit(EXIT_FAILURE);
    }

    if (sigaction(SIGINT, &sa, NULL) < 0) {
        fprintf(stderr, "ERROR: failed to install signal handler\n");
        exit(EXIT_FAILURE);
    }

    /* socket data structure */
    struct addrinfo hints;
    struct addrinfo *result = NULL;
    struct addrinfo *r = NULL;
    int getaddrinfo_ret;

    int sockfd = -1;
    int fd_flags;
    int connect_ret;
    int socket_error = 0;
    socklen_t socket_error_len;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_flags = 0;
    hints.ai_protocol = 0;

    if (strcmp(transport, "tcp") == 0) {
        hints.ai_socktype = SOCK_STREAM;
    } else {
        hints.ai_socktype = SOCK_DGRAM;
    }

    /* RPC client data structure */
    CLIENT *client = NULL;
    enum clnt_stat rpc_stat;
    struct rpc_err rpc_error;
    char rpc_xid_string[16];

    /* RPC client timeout */
    struct timeval rpc_timeout_val;
    rpc_timeout_val.tv_sec = rpc_timeout;
    rpc_timeout_val.tv_usec = 0;

    /* RPC client netbuf data structure */
    struct netbuf nbuf;

    /* IO-multiplexing variables */
    fd_set readfds;
    fd_set writefds;
    int pselect_ret;

    /* connect timeout */
    struct timespec connect_timeout_val;
    connect_timeout_val.tv_sec = connect_timeout;
    connect_timeout_val.tv_nsec = 0;

    /* timer for RPC rtt calculation */
    struct timespec start_time;
    struct timespec end_time;
    double rtt;

    /* rtt stats */
    double min_rtt = DBL_MAX;
    double max_rtt = 0.0;
    double sum_rtt = 0.0;
    double sum_rtt_sq = 0.0;
    double avg_rtt = 0.0;
    double avg_rtt_sq = 0.0;
    double stddev_rtt = 0.0;
    double variance = 0.0;
    long int success_count = 0;

    /* RPC request index */
    long int client_index = 0;

    /* populate IP structure */
    getaddrinfo_ret = getaddrinfo(hostname, port, &hints, &result);
    if (getaddrinfo_ret != 0) {
        fprintf(stderr, "ERROR: failed to call getaddrinfo: %s\n", gai_strerror(getaddrinfo_ret));
        exit(EXIT_FAILURE);
    }

    /* iterate IP structure item(s) */
    for (r = result; r != NULL; r = r->ai_next) {
        /* create socket */
        sockfd = socket(r->ai_family, r->ai_socktype, r->ai_protocol);

        if (sockfd < 0) {
            fprintf(stderr, "ERROR: failed to create socket: %s\n", strerror(errno));
            continue;
        }

        fprintf(stdout, "INFO: socket file descriptor number: %d\n", sockfd);

        /* set socket to non-blocking mode */
        fd_flags = fcntl(sockfd, F_GETFL, 0);
        if (fd_flags < 0) {
            fprintf(stderr, "ERROR: failed get socket %d status flags: %s\n", sockfd, strerror(errno));
            close(sockfd);
            sockfd = -1;
            continue;
        }
 
        if ((fcntl(sockfd, F_SETFL, fd_flags | O_NONBLOCK)) < 0) {
            fprintf(stderr, "ERROR: failed set socket %d to non-blocking mode: %s\n", sockfd, strerror(errno));
            close(sockfd);
            sockfd = -1;
            continue;
        }

        /* connect to remote RPC server. same to TCP / UDP */
        connect_ret = connect(sockfd, r->ai_addr, r->ai_addrlen);

        if (connect_ret < 0) {
            if (errno != EINPROGRESS) {
                fprintf(stderr, "ERROR: failed to connect remote RPC server %s: %s\n", hostname, strerror(errno));
                close(sockfd);
                sockfd = -1;
                continue;
            }
        }

        /* exit loop if connection is done immediately */
        if (connect_ret == 0) {
            break;
        }

        /* configure IO-multiplexing */
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);
        writefds = readfds;

        /* wait for socket fd */
        pselect_ret = pselect(sockfd + 1, &readfds, &writefds, NULL, &connect_timeout_val, &signal_empty_set);

        /* connect timeout triggered */
        if (pselect_ret == 0) {
            fprintf(stderr, "ERROR: connect() timed out\n");
            close(sockfd);
            sockfd = -1;
            continue;
        }

        /* handle SIGINT signal */
        if (pselect_ret < 0 && errno == EINTR) {
            if (break_flag > 0) {
                fprintf(stderr, "WARNING: SIGINT triggered\n");
                goto error_handler;
            }
        }

        /* check socket fd to determine if error occurs */
        if (FD_ISSET(sockfd, &readfds) || FD_ISSET(sockfd, &writefds)) {
            socket_error_len = sizeof(socket_error);

            if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &socket_error, &socket_error_len) < 0) {
                fprintf(stderr, "ERROR: failed to get socket option: %s\n", strerror(errno));
                close(sockfd);
                sockfd = -1;
                continue;
            }

            /* check socket connection status */
            if (socket_error == 0) {
                break;
            } else {
                errno = socket_error;
                fprintf(stderr, "ERROR: failed to connect remote RPC server %s: %s\n", hostname, strerror(errno));
                close(sockfd);
                sockfd = -1;
                continue;
            }
        } else {
            fprintf(stderr, "ERROR: IO-multiplexing error\n");
            close(sockfd);
            sockfd = -1;
            continue;
        }
    }

    /* fail-safe check */
    if (sockfd < 0 || r == NULL) {
        fprintf(stderr, "ERROR: failed to establish connection\n");
        goto error_handler;
    }

    /* restore blocking mode */
    if (fcntl(sockfd, F_SETFL, fd_flags) < 0) {
        fprintf(stderr, "ERROR: failed set socket %d to blocking mode: %s\n", sockfd, strerror(errno));
        goto error_handler;
    }

    /* fill out netbuf data structure */
    nbuf.len = r->ai_addrlen;
    nbuf.buf = r->ai_addr;
    nbuf.maxlen = r->ai_addrlen;

    /* construct RPC client */
    if (strcmp(transport, "tcp") == 0) {
        client = rpc_tcp_client(sockfd, &nbuf, program_number, program_version);
    } else {
        client = rpc_udp_client(sockfd, &nbuf, program_number, program_version);

        /*
         * we need to disable UDP retransmissions. this is retransmission count formula:
         * retransmission count = total timeout / retry timeout
         * total timeout = rpc_timeout_val
         * retry timeout = rpc_timeout_val
         */
        if (client != (CLIENT *) NULL) {
            if (!clnt_control(client, CLSET_RETRY_TIMEOUT, (char *)&rpc_timeout_val)) {
                fprintf(stderr, "ERROR: failed to disable UDP retransmissions\n");
                goto error_handler;
            }
        }
    }

    if (client == (CLIENT *) NULL) {
        clnt_pcreateerror("ERROR: couldn't create RPC client");
        goto error_handler;
    }

    /* unblock SIGINT before entering ping loop */
    if (sigprocmask(SIG_UNBLOCK, &signal_block_set, NULL) < 0) {
        fprintf(stderr, "ERROR: failed to unblock SIGINT signal\n");
        goto error_handler;
    }

    /* ping loop */
    for (;;) {
        /* check signal flag */
        if (break_flag > 0) {
            break;
        }

        /* increment index */
        ++client_index;

        /* start timer */
        clock_gettime(CLOCK_MONOTONIC, &start_time);

        /* silence legacy macro function cast warning */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"

        /* trigger RPC call with NULL procedure to remote RPC server */
        rpc_stat = CLNT_CALL(client, NULLPROC, (xdrproc_t) xdr_void,
            (char *) NULL, (xdrproc_t) xdr_void,
            (char *) NULL, rpc_timeout_val);

#pragma GCC diagnostic pop

        /* stop timer */
        clock_gettime(CLOCK_MONOTONIC, &end_time);

        /* check RPC client status. we should only print rtt if client is successful */
        switch (rpc_stat) {
            case RPC_SUCCESS:
                ++success_count;

                rtt = calculate_rtt(&start_time, &end_time);

                /* retrieve XID session ID */
                get_rpc_xid(client, rpc_xid_string, sizeof(rpc_xid_string));

                /* print ping result */
                fprintf(stdout, "index %ld from %s: RPC program=%u version=%u time=%.3f ms | XID=%s\n", client_index, hostname, program_number, program_version, rtt, rpc_xid_string);

                /* record rtt stats data. this is used for final output */
                if (rtt > max_rtt) {
                    max_rtt = rtt;
                }

                if (rtt < min_rtt) {
                    min_rtt = rtt;
                }

                sum_rtt += rtt;

                sum_rtt_sq += rtt * rtt;

                break;
            case RPC_CANTRECV:
                fprintf(stderr, "index %ld from %s: ERROR: failure in receiving result\n", client_index, hostname);

                break;
            case RPC_TIMEDOUT:
                fprintf(stderr, "index %ld from %s: ERROR: RPC call timed out\n", client_index, hostname);

                break;
            case RPC_VERSMISMATCH:
                fprintf(stderr, "ERROR: RPC versions not compatible\n");

                goto error_handler;
            case RPC_PROGUNAVAIL:
                fprintf(stderr, "ERROR: RPC program not available\n");

                goto error_handler;
            case RPC_PROGVERSMISMATCH:
                clnt_geterr(client, &rpc_error);
                fprintf(stderr, "ERROR: RPC min program version: %u; RPC max program version: %u\n", rpc_error.re_vers.low, rpc_error.re_vers.high);

                goto error_handler;
            case RPC_PROCUNAVAIL:
                fprintf(stderr, "ERROR: RPC procedure not available\n");

                goto error_handler;
            case RPC_CANTDECODEARGS:
                fprintf(stderr, "ERROR: decode arguments error\n");

                goto error_handler;
            case RPC_SYSTEMERROR:
                fprintf(stderr, "ERROR: RPC generic other error\n");

                goto error_handler;
            default:
                fprintf(stderr, "ERROR: unknown client error: %d\n", rpc_stat);

                goto error_handler;
        }

        /* exit loop if count becomes 0 */
        if (count > 0) {
            --count;

            if (count == 0) {
                break;
            }
        }

        /* sleep */
        sleep(interval);

    }

    /* print final stats output */
    if (success_count > 0) {
        avg_rtt = sum_rtt / (double)success_count;
        avg_rtt_sq = sum_rtt_sq / (double)success_count;
        variance = avg_rtt_sq - (avg_rtt * avg_rtt);

        /* fail-safe to cover below 0 error */
        if (variance < 0.0) {
            variance = 0.0;
        }

        stddev_rtt = sqrt(variance);

        fprintf(stdout, "\n--- %s ONC RPC ping statistics ---\n", hostname);
        fprintf(stdout, "rtt min/avg/max/stddev = %.3f/%.3f/%.3f/%.3f ms\n", min_rtt, avg_rtt, max_rtt, stddev_rtt);
    } else {
        fprintf(stderr, "ERROR: no successful RPC calls\n");
        goto error_handler;
    }

    /* clean up */
    freeaddrinfo(result);
    (void) CLNT_DESTROY (client);

    exit(EXIT_SUCCESS);

error_handler:
    if (result != NULL) {
        freeaddrinfo(result);
    }

    if (client != NULL) {
        (void) CLNT_DESTROY (client);
    } else {
        if (sockfd >= 0) {
            close(sockfd);
        }
    }

    exit(EXIT_FAILURE);
}
