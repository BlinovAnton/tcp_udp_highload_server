# tcp_udp_highload_server
it had to be server, which send accept_fd in queue to pthreads, which multiplexed for udp and tcp.
but it is too simple, so...

description: server listen tcp connection and other 3 pids wait in recvfrom state for udp communications. And another 1 pid create accept_fd's and send it by message queue to pthreads, which communicate by tcp.

sauce:
  sigint handler to close() and free() all shit
