# ONC RPC Ping Utility

## Intro

The `oncrpc-ping` is a utility designed to measure actual ONC RPC(Sun RPC) service latency over both TCP and UDP transport protocols. By bypassing the RPC Bind service, the utility avoids unnecessary service inquiries, ensuring accurate, end-to-end latency measurements.

## Dependency

| Library | Purpose |
|---|---|
| [libtirpc](https://git.linux-nfs.org/?p=steved/libtirpc.git) | ONC RPC client API |

## Measurement Methodology

ONC RPC specifications define a standardized 'NULL' procedure (procedure number 0). This 'do-nothing' operation is supported by all ONC RPC servers and is ideal for service latency measurement.

## Measurement Workflow

To capture pure client-server RPC latency, the program utilizes low-level ONC RPC interfaces to control client behavior. The measurement lifecycle is as follows:

- Create a network socket configured for the specified transport (TCP/UDP).
- Initialize an ONC RPC client.
- Execute a `connect()` call.
- Issue an ONC RPC NULL request to the remote server and record the latency.
- Terminate the ONC RPC client connection.

The following simplified data flow graph shows where the program measures the latency (the box section):

```
      Client                                        Server
         |                                             |
         |  ------- Connect Request / Handshake ---->  |
         |  <--------- Handshake Response ------------ |
         |                                             |
       ---------------------------------------------------  
      |  |                                             |  |
      |  |  --- ONC RPC NULL PROC (Ping Request) ----> |  |
      |  |  <--------- ONC RPC NULL PROC Reply ------- |  |
      |  |                                             |  |
       ---------------------------------------------------
         |                                             |
         |  --------- Destroy ONC RPC Client --------> |
         |  <--------- Close Connection -------------- |
         |                                             |
```

## Compilation

```
$ make
gcc -g -Wall -Wextra -Wpedantic -Wconversion -Wdouble-promotion -Wunused -Wshadow -Wsign-conversion -fsanitize=undefined -I/usr/include/tirpc -c oncrpc-ping.c -o oncrpc-ping.o
gcc -g -Wall -Wextra -Wpedantic -Wconversion -Wdouble-promotion -Wunused -Wshadow -Wsign-conversion -fsanitize=undefined oncrpc-ping.o -o oncrpc-ping -lm -ltirpc
```

## Usage

```
$ ./oncrpc-ping -h
ONC RPC Ping - Version 1.0.0
usage: oncrpc-ping -n|--hostname <target hostname / IP>
                   -P|--port <port number>
                   -p|--program-number <RPC program number>
                   -v|--program-version <RPC program version>
                   -T|--transport tcp|udp
                   [-i|--interval <second(s)>] default: 1 second
                   [-t|--rpc-timeout <second(s)>] default: 10 seconds
                   [-c|--connect-timeout <second(s)>] default: 5 seconds
                   [-C|--count <count number>]
                   [-h|--help]
```

- `-n` or `--hostname <hostname / IP>`: target hostname or IP address of the RPC server

- `-P` or `--port <port>`: port number the RPC server is listening on

- `-p` or `--program-number <number>`: ONC RPC program number registered by the target service

- `-v` or `--program-version <version>`: ONC RPC program version number of the target service

- `-T` or `--transport <tcp|udp>`: transport protocol

- `-i` or `--interval <seconds>`: interval in whole seconds between successive RPC NULL calls

- `-t` or `--rpc-timeout <seconds>`: timeout in whole seconds for each individual RPC NULL call

- `-c` or `--connect-timeout <seconds>`: timeout in whole seconds for the initial `connect()` call

- `-C` or `--count <number>`: number of RPC NULL calls to send before printing statistics and exiting. if omitted, the program runs indefinitely until interrupted with `Ctrl+C` (SIGINT), at which point it prints final statistics and exits cleanly

- `-h` or `--help`: print usage information and exit

## ChangeLog

```
[07/24/2026] 1.0.0 - initial commit
```

## Demo

```
$ ./oncrpc-ping -n 10.x.x.x -P 2049 -p 100003 -v 3 -T tcp -C 5
INFO: socket file descriptor number: 3
index 1 from 10.x.x.x: RPC program=100003 version=3 time=0.084 ms | XID=0x948041C3
index 2 from 10.x.x.x: RPC program=100003 version=3 time=0.093 ms | XID=0x938041C3
index 3 from 10.x.x.x: RPC program=100003 version=3 time=0.070 ms | XID=0x928041C3
index 4 from 10.x.x.x: RPC program=100003 version=3 time=0.104 ms | XID=0x918041C3
index 5 from 10.x.x.x: RPC program=100003 version=3 time=0.149 ms | XID=0x908041C3

--- 10.x.x.x ONC RPC ping statistics ---
rtt min/avg/max/stddev = 0.070/0.100/0.149/0.027 ms
```

## Reference

[libtirpc](https://git.linux-nfs.org/?p=steved/libtirpc.git)

[ONC+ Developer's Guide](https://docs.oracle.com/cd/E18752_01/html/816-1435/)
