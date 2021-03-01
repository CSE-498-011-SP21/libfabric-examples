/*  Notes
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

static struct fi_info *fi, *hints;
static struct fid_fabric *fabric;
static struct fid_domain *domain;
static struct fi_cq_attr cq_attr;
static struct fid_cq *tx_cq, *rx_cq;
static struct fi_av_attr av_attr;
static struct fid_av *av;

static char* dst_addr; // client specifies server

// This is pretty neat!
// From here: https://stackoverflow.com/a/14038590
#define safe_call(ans) { callCheck((ans), __FILE__, __LINE__); }
inline void callCheck(int err, const char *file, int line, bool abort=true) {
	if (err != 0) {
		std::cout << "Error: " << err << " " << fi_strerror(-err) << " " << file << ":" << line << std::endl;
		exit(0);
	}
}


int init_fabric() {
    int ret = 0;

    // Get list of providers i.e. verbs, psm2, tcp
    std::cout << "Getting fi provider" << std::endl;
    hints = fi_allocinfo();
    hints->caps = FI_MSG;
    //hints->ep_attr->type = FI_EP_RDM;
    hints->ep_attr->type = FI_EP_MSG;
    if (dst_addr) {
        /* client */
        safe_call(fi_getinfo(FI_VERSION(1, 6), dst_addr, "4092", 0, hints, &fi));
    } else {
        /* server */
        safe_call(fi_getinfo(FI_VERSION(1, 6), NULL, "4092", FI_SOURCE, hints, &fi));
    }

    // fi is a linked list of providers. For now just use first one
    std::cout << "Using provider: " << fi->fabric_attr->prov_name << std::endl;

     // Create a fabric object. This is the parent of everything else.
    std::cout << "Creating fabric object" << std::endl;
    safe_call(fi_fabric(fi->fabric_attr, &fabric, NULL));

    // Create a domain. This represents logical connection into fabric.
    // For example, this may map to a physical NIC, and defines the boundary
    // four fabric resources. Most other objects belong to a domain.
    std::cout << "Creating domain" << std::endl;
    safe_call(fi_domain(fabric, fi, &domain, NULL));

    // Create a completion queue. This reports data transfer operation completions.
    // Unlike fabric event queues, these are associated with a single hardware NIC.
    // Will create one for tx and one for rx
    std::cout << "Creating tx completion queue" << std::endl;
    memset(&cq_attr, 0, sizeof(cq_attr));
    cq_attr.wait_obj = FI_WAIT_NONE;
    cq_attr.size = fi->tx_attr->size;
    safe_call(fi_cq_open(domain, &cq_attr, &tx_cq, NULL));

    std::cout << "Creating rx completion queue" << std::endl;
    cq_attr.size = fi->rx_attr->size;
    safe_call(fi_cq_open(domain, &cq_attr, &rx_cq, NULL));

    // Create an address vector. This allows connectionless endpoints to communicate
    // without having to resolve addresses, such as IPv4, during data transfers.
    std::cout << "Creating address vector" << std::endl;
    memset(&av_attr, 0, sizeof(av_attr));
    av_attr.type = fi->domain_attr->av_type ? fi->domain_attr->av_type : FI_AV_MAP;
    av_attr.count = 1;
    av_attr.name = NULL;
    safe_call(fi_av_open(domain, &av_attr, &av, NULL));

    return ret;
}

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

	// 1) Init fabric objects
	safe_call(init_fabric());
	// 2) Init endpoint


	exit: 
		if (err) {
	        std::cerr << "ERROR (" << err << "): " << fi_strerror(-err) << std::endl;
	        ret = err;
	    }

	    // int rel_err = release_all();
	    // if (!ret && rel_err) ret = rel_err;

		return ret;
}
