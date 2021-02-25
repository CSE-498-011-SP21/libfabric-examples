/*  Notes
Applications will typically use the control interfaces to discover local capabilities and allocate necessary resources. 
They will then allocate and configure a communication endpoint to send and receive data, or perform other types of data transfers, 
with remote endpoints.
compile: gcc Echo.cpp -lstdc++ -lfabric
*/

// standard includes
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <iostream>
#include <errno.h>

// libfabric includes
#include <rdma/fabric.h>
#include <rdma/fi_domain.h>
#include <rdma/fi_endpoint.h>
#include <rdma/fi_cm.h>
#include <rdma/fi_tagged.h>
#include <rdma/fi_rma.h>
#include <rdma/fi_errno.h>

static char* dst_addr; // client specifies server

void usage() {
    std::cout << "Usage: ./Echo [optional server address]" << std::endl;
    std::cout << "            server address - remote server to connect to as a client." << std::endl;
    std::cout << "                             If not specified, will run as a server." << std::endl;
}

int main(int argc, char **argv) {
	int err = 0;
	int ret = 0;

	// Get command line args
	if (argc < 2) {
		std::cout << "Running as SERVER" << std::endl;
	} else if (argc == 2) {
		if (argv[1] == "--help") {
			usage();
			return 0;
		} else {
			dst_addr = argv[1];
			std::cout <<  "Running as CLIENT - server addr=" << dst_addr << std::endl;
		}
	} else {
		std::cout << "Too many arguments!" << std::endl;
        usage();
        return -1;
	}

	return ret;
}
