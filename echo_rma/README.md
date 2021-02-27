# RMA ECHO

The server receives a message from the client, then responds back with the same message. This is done using libfabric's RMA. The client writes the value to the server, then the server reads it and writes it back to the client. 

Run server:

`./echo`

Run client:

`./echo <server-ip> <string-to-echo>`