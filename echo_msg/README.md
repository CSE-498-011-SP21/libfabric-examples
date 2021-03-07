# MSG ECHO

Simple application that establishes connection between server and client and send a message between them. This is done using libfabrics connection based message passing (https://ofiwg.github.io/libfabric/v1.1.1/man/fi_msg.3.html).

### Build instructions

In the echo_msg directory:

```
mkdir build
cd build
cmake ..
make -j
```

### Execute instructions

Run server:

`./echo`

Run client:

`./echo <server-ip>`