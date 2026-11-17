# oncrpc
This code is written by Claude Opus 5.5 prompted from the chat web interface, deliverable we obtained as .tgz archives, compiled and tested on Linux and Windows 11.

ONC RPC library targeting Windows, Linux, FreeBSD and macOS

To provide Async IO libuv uses epoll on Linux, kqueue on BSD/macOS and on Windows I/O Completion Ports (IOCP).

The intent is to use this as a calling interface to torrentinfo.

See **[Usage Instructions](USAGE.md)**

See **[Build Instructions](BUILDING.md)**
