# liboncrpc manual pages

One page per public function, in the layout of a Unix manual page: name,
synopsis, description, return value, an example, and related pages.
Start with [liboncrpc(3)](liboncrpc.3.md) for the concepts, or with the
[tutorial](../TUTORIAL.md) if you are new to RPC.

## Overview

| Page | Description |
|---|---|
| [liboncrpc(3)](liboncrpc.3.md) | asynchronous ONC RPC and XDR on libuv |
| [oncrpcgen(1)](oncrpcgen.1.md) | generate C code for liboncrpc from an XDR/RPC specification |

## Library and memory

| Page | Description |
|---|---|
| [oncRPCStrError(3)](oncRPCStrError.3.md) | describe a status code in words |
| [oncRPCVersion(3)](oncRPCVersion.3.md) | version of the library in use |
| [oncRPCAlloc(3)](oncRPCAlloc.3.md) | allocate zeroed memory through an allocator |
| [oncRPCAllocArray(3)](oncRPCAllocArray.3.md) | allocate a zeroed array through an allocator |
| [oncRPCRealloc(3)](oncRPCRealloc.3.md) | resize a block through an allocator |
| [oncRPCFree(3)](oncRPCFree.3.md) | release a block through an allocator |
| [oncRPCStrdup(3)](oncRPCStrdup.3.md) | copy a string through an allocator |

## XDR streams

| Page | Description |
|---|---|
| [oncRPCXdrInitEncode(3)](oncRPCXdrInitEncode.3.md) | start encoding into a fixed buffer |
| [oncRPCXdrInitEncodeDynamic(3)](oncRPCXdrInitEncodeDynamic.3.md) | start encoding into a growing buffer |
| [oncRPCXdrInitDecode(3)](oncRPCXdrInitDecode.3.md) | start decoding from a buffer |
| [oncRPCXdrInitFree(3)](oncRPCXdrInitFree.3.md) | make a stream that frees decoded data |
| [oncRPCXdrDestroy(3)](oncRPCXdrDestroy.3.md) | release a stream's buffer |
| [oncRPCXdrReset(3)](oncRPCXdrReset.3.md) | rewind a stream and clear its error |
| [oncRPCXdrGetPos(3)](oncRPCXdrGetPos.3.md) | current offset in a stream |
| [oncRPCXdrSetPos(3)](oncRPCXdrSetPos.3.md) | move to an offset in a stream |
| [oncRPCXdrData(3)](oncRPCXdrData.3.md) | the buffer of a stream |
| [oncRPCXdrLength(3)](oncRPCXdrLength.3.md) | bytes in a stream |
| [oncRPCXdrRemaining(3)](oncRPCXdrRemaining.3.md) | bytes left to decode, or room left to encode |
| [oncRPCXdrDetach(3)](oncRPCXdrDetach.3.md) | take the buffer of a dynamic encoding stream |
| [oncRPCXdrFree(3)](oncRPCXdrFree.3.md) | release everything a decode allocated |
| [oncRPCXdrMemFree(3)](oncRPCXdrMemFree.3.md) | free a block through a stream's allocator |

## XDR types

| Page | Description |
|---|---|
| [oncRPCXdrVoid(3)](oncRPCXdrVoid.3.md) | XDR routine for no data |
| [oncRPCXdrInt32(3)](oncRPCXdrInt32.3.md) | XDR routine for a signed 32-bit integer |
| [oncRPCXdrUint32(3)](oncRPCXdrUint32.3.md) | XDR routine for an unsigned 32-bit integer |
| [oncRPCXdrInt64(3)](oncRPCXdrInt64.3.md) | XDR routine for a signed 64-bit integer (hyper) |
| [oncRPCXdrUint64(3)](oncRPCXdrUint64.3.md) | XDR routine for an unsigned 64-bit integer |
| [oncRPCXdrFloat(3)](oncRPCXdrFloat.3.md) | XDR routine for a single-precision float |
| [oncRPCXdrDouble(3)](oncRPCXdrDouble.3.md) | XDR routine for a double-precision float |
| [oncRPCXdrBool(3)](oncRPCXdrBool.3.md) | XDR routine for a boolean |
| [oncRPCXdrEnum(3)](oncRPCXdrEnum.3.md) | XDR routine for an enumeration value |
| [oncRPCXdrInt8(3)](oncRPCXdrInt8.3.md) | XDR routine for a signed char |
| [oncRPCXdrUint8(3)](oncRPCXdrUint8.3.md) | XDR routine for an unsigned char |
| [oncRPCXdrInt16(3)](oncRPCXdrInt16.3.md) | XDR routine for a short |
| [oncRPCXdrUint16(3)](oncRPCXdrUint16.3.md) | XDR routine for an unsigned short |
| [oncRPCXdrOpaque(3)](oncRPCXdrOpaque.3.md) | XDR routine for fixed-length opaque data |
| [oncRPCXdrBytes(3)](oncRPCXdrBytes.3.md) | XDR routine for variable-length opaque data |
| [oncRPCXdrString(3)](oncRPCXdrString.3.md) | XDR routine for a string |
| [oncRPCXdrArray(3)](oncRPCXdrArray.3.md) | XDR routine for a variable-length array |
| [oncRPCXdrVector(3)](oncRPCXdrVector.3.md) | XDR routine for a fixed-length array |
| [oncRPCXdrPointer(3)](oncRPCXdrPointer.3.md) | XDR routine for optional data |
| [oncRPCXdrArrayLen(3)](oncRPCXdrArrayLen.3.md) | XDR routine for an array's element count |
| [oncRPCXdrArrayAlloc(3)](oncRPCXdrArrayAlloc.3.md) | allocate element storage while decoding |
| [oncRPCXdrAuthSys(3)](oncRPCXdrAuthSys.3.md) | XDR routine for an AUTH_SYS credential |

## Clients

| Page | Description |
|---|---|
| [oncRPCClientCreate(3)](oncRPCClientCreate.3.md) | create a client for a remote program |
| [oncRPCClientClose(3)](oncRPCClientClose.3.md) | close a client |
| [oncRPCClientCall(3)](oncRPCClientCall.3.md) | start a remote procedure call |
| [oncRPCClientLastCall(3)](oncRPCClientLastCall.3.md) | id of the call just started |
| [oncRPCClientCancel(3)](oncRPCClientCancel.3.md) | cancel one call |
| [oncRPCClientSetTimeout(3)](oncRPCClientSetTimeout.3.md) | time allowed for each call |
| [oncRPCClientSetRetry(3)](oncRPCClientSetRetry.3.md) | UDP retransmission interval |
| [oncRPCClientSetMaxRecord(3)](oncRPCClientSetMaxRecord.3.md) | largest reply a TCP client accepts |
| [oncRPCClientSetAuthNone(3)](oncRPCClientSetAuthNone.3.md) | send calls without credentials |
| [oncRPCClientSetAuthSys(3)](oncRPCClientSetAuthSys.3.md) | send calls with AUTH_SYS credentials |
| [oncRPCClientSetAuthSysDefault(3)](oncRPCClientSetAuthSysDefault.3.md) | send calls as the current process |
| [oncRPCClientSetAuth(3)](oncRPCClientSetAuth.3.md) | use an application-defined authentication flavor |
| [oncRPCClientSetTls(3)](oncRPCClientSetTls.3.md) | make a client use TLS |
| [oncRPCClientLastError(3)](oncRPCClientLastError.3.md) | details of the last completed call |
| [oncRPCClientData(3)](oncRPCClientData.3.md) | application pointer of a client |
| [oncRPCClientAllocator(3)](oncRPCClientAllocator.3.md) | allocator of a client |
| [oncRPCClientPending(3)](oncRPCClientPending.3.md) | number of calls not yet completed |

## Port mapper and rpcbind

| Page | Description |
|---|---|
| [oncRPCPmapGetPort(3)](oncRPCPmapGetPort.3.md) | ask a port mapper for a program's port |
| [oncRPCPmapSet(3)](oncRPCPmapSet.3.md) | register a program with the local port mapper |
| [oncRPCPmapUnset(3)](oncRPCPmapUnset.3.md) | remove a program from the local port mapper |
| [oncRPCRpcbGetAddr(3)](oncRPCRpcbGetAddr.3.md) | ask rpcbind for a service's address |
| [oncRPCRpcbSet(3)](oncRPCRpcbSet.3.md) | register a service with the local rpcbind |
| [oncRPCRpcbUnset(3)](oncRPCRpcbUnset.3.md) | remove a service from the local rpcbind |
| [oncRPCUaddrFromSockaddr(3)](oncRPCUaddrFromSockaddr.3.md) | universal address of a socket address |
| [oncRPCUaddrToSockaddr(3)](oncRPCUaddrToSockaddr.3.md) | socket address of a universal address |
| [oncRPCNetid(3)](oncRPCNetid.3.md) | rpcbind network id of a transport |

## Servers

| Page | Description |
|---|---|
| [oncRPCServerCreate(3)](oncRPCServerCreate.3.md) | create an RPC server |
| [oncRPCServerClose(3)](oncRPCServerClose.3.md) | close a server |
| [oncRPCServerListen(3)](oncRPCServerListen.3.md) | accept calls on a transport and address |
| [oncRPCServerPort(3)](oncRPCServerPort.3.md) | port a server listens on |
| [oncRPCServerRegister(3)](oncRPCServerRegister.3.md) | serve a program version |
| [oncRPCServerUnregister(3)](oncRPCServerUnregister.3.md) | stop serving a program version |
| [oncRPCServerPmapRegister(3)](oncRPCServerPmapRegister.3.md) | register a server's listeners with rpcbind |
| [oncRPCServerPmapUnregister(3)](oncRPCServerPmapUnregister.3.md) | remove a program version from rpcbind |
| [oncRPCServerGetLimits(3)](oncRPCServerGetLimits.3.md) | read a server's limits |
| [oncRPCServerSetLimits(3)](oncRPCServerSetLimits.3.md) | change a server's limits |
| [oncRPCServerGetStats(3)](oncRPCServerGetStats.3.md) | what a server has done |
| [oncRPCServerSetMaxRecord(3)](oncRPCServerSetMaxRecord.3.md) | largest request a server accepts |
| [oncRPCServerPending(3)](oncRPCServerPending.3.md) | requests not yet finished |
| [oncRPCServerAllocator(3)](oncRPCServerAllocator.3.md) | allocator of a server |
| [oncRPCServerSetAuth(3)](oncRPCServerSetAuth.3.md) | check callers of an authentication flavor |
| [oncRPCServerSetTls(3)](oncRPCServerSetTls.3.md) | offer or require TLS on a server |

## Server requests

| Page | Description |
|---|---|
| [oncRPCSvcDecodeArgs(3)](oncRPCSvcDecodeArgs.3.md) | decode a request's arguments |
| [oncRPCSvcReply(3)](oncRPCSvcReply.3.md) | send a successful reply |
| [oncRPCSvcDrop(3)](oncRPCSvcDrop.3.md) | finish a request without replying |
| [oncRPCSvcErrNoProc(3)](oncRPCSvcErrNoProc.3.md) | reply that the procedure does not exist |
| [oncRPCSvcErrDecode(3)](oncRPCSvcErrDecode.3.md) | reply that the arguments could not be decoded |
| [oncRPCSvcErrSystem(3)](oncRPCSvcErrSystem.3.md) | reply that the server failed |
| [oncRPCSvcErrNoProg(3)](oncRPCSvcErrNoProg.3.md) | reply that the program is not served |
| [oncRPCSvcErrProgVers(3)](oncRPCSvcErrProgVers.3.md) | reply that the program version is not served |
| [oncRPCSvcErrAuth(3)](oncRPCSvcErrAuth.3.md) | reject a request's credentials |
| [oncRPCSvcAbandoned(3)](oncRPCSvcAbandoned.3.md) | whether a request's reply can still be delivered |
| [oncRPCSvcSetAbandonCb(3)](oncRPCSvcSetAbandonCb.3.md) | be told when a request's caller goes away |
| [oncRPCSvcPeer(3)](oncRPCSvcPeer.3.md) | describe who sent a request |
| [oncRPCSvcLocalCred(3)](oncRPCSvcLocalCred.3.md) | user and group of a local caller |
| [oncRPCSvcCred(3)](oncRPCSvcCred.3.md) | credential a request arrived with |
| [oncRPCSvcVerf(3)](oncRPCSvcVerf.3.md) | verifier a request arrived with |
| [oncRPCSvcSetReplyVerf(3)](oncRPCSvcSetReplyVerf.3.md) | verifier to send with a reply |
| [oncRPCSvcTls(3)](oncRPCSvcTls.3.md) | whether a request arrived over TLS |
| [oncRPCSvcTlsPeer(3)](oncRPCSvcTlsPeer.3.md) | certificate subject of a TLS client |

## TLS

| Page | Description |
|---|---|
| [oncRPCTlsConfigCreate(3)](oncRPCTlsConfigCreate.3.md) | create a TLS configuration |
| [oncRPCTlsConfigFree(3)](oncRPCTlsConfigFree.3.md) | release the application's hold on a TLS configuration |
| [oncRPCTlsConfigSetCertificate(3)](oncRPCTlsConfigSetCertificate.3.md) | this side's certificate and key |
| [oncRPCTlsConfigAddTrusted(3)](oncRPCTlsConfigAddTrusted.3.md) | trust certificates as issuers |
| [oncRPCTlsConfigUseSystemTrust(3)](oncRPCTlsConfigUseSystemTrust.3.md) | trust the system's certificate store |
| [oncRPCTlsConfigSetVerify(3)](oncRPCTlsConfigSetVerify.3.md) | how strictly to check the other side |

## The .x language parser

| Page | Description |
|---|---|
| [oncRPCXSpecParse(3)](oncRPCXSpecParse.3.md) | parse an XDR/RPC language specification |
| [oncRPCXSpecFree(3)](oncRPCXSpecFree.3.md) | release a parsed specification |
| [oncRPCXSpecFind(3)](oncRPCXSpecFind.3.md) | find a definition by name |
| [oncRPCXSpecEval(3)](oncRPCXSpecEval.3.md) | value of a constant, enum item or number |
| [oncRPCXSpecGenerate(3)](oncRPCXSpecGenerate.3.md) | generate C code from a specification |
