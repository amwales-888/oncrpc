# oncRPCXdrReset(3)

## NAME

oncRPCXdrReset - rewind a stream and clear its error

## SYNOPSIS

```c
#include <xdr.h>

void oncRPCXdrReset(oncRPCXdr *xdrs);
```

## DESCRIPTION

`oncRPCXdrReset()` moves the stream back to its start and clears its
error flag. On an encoding stream it also discards the bytes produced so
far, so the buffer can be reused for the next message; a dynamic stream
keeps its (possibly enlarged) buffer. On a decoding stream the same bytes
can then be decoded again.

## EXAMPLE

Encode several messages, one after another, in the same buffer:

```c
#include <xdr.h>

void send_counters(const uint32_t *values, int n,
                   void (*send)(const uint8_t *buf, size_t len))
{
    uint8_t buf[4];
    oncRPCXdr xdrs;
    int i;

    oncRPCXdrInitEncode(&xdrs, buf, sizeof(buf));
    for (i = 0; i < n; i++) {
        uint32_t v = values[i];
        oncRPCXdrReset(&xdrs);
        if (oncRPCXdrUint32(&xdrs, &v))
            send(oncRPCXdrData(&xdrs), oncRPCXdrLength(&xdrs));
    }
}
```

## SEE ALSO

[oncRPCXdrSetPos(3)](oncRPCXdrSetPos.3.md),
[oncRPCXdrInitEncode(3)](oncRPCXdrInitEncode.3.md)
