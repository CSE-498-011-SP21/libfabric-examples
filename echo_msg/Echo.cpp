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

static char *local_buf;
static char *remote_buf;
static struct fi_info *fi_pep, *fi, *hints;
static struct fid_fabric *fabric;
static struct fi_eq_attr eq_attr;
static struct fid_eq *eq;
static struct fid_pep *pep;
static struct fid_domain *domain;
static struct fid_ep *ep;
static struct fi_cq_attr cq_attr;
static struct fid_cq *tx_cq, *rx_cq;
static struct fid_mr *mr;
static struct fi_av_attr av_attr;
static struct fid_av *av;
static size_t max_msg_size = 4096;

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

int check_malloc(char *first, char *second) {
	int ret = 0;
	if (!first || !second) {
        ret = -1;
    }
    return ret;
}

int start_server() {
	/*
		Functions in example:
		- ft_init
		- ft_init_oob // Dont think I need
		- ft_getInfo // Since dont have ops (I think) just fi_getinfo()
		- fi_fabric
		- fi_eq_open
		- fi_passive_ep
		- fi_pep_bind
		- fi_listen
    */

	// Init
    std::cout << "Starting server" << std::endl;
    hints = fi_allocinfo();
    hints->caps = FI_MSG;
    hints->ep_attr->type = FI_EP_MSG;

    // getInfo for server. Assumes server is starting first
    //safe_call(fi_getinfo(FI_VERSION(1, 6), NULL, "4092", FI_SOURCE, hints, &fi_pep));


    if (dst_addr) { // Prob dont need this
        /* client */
        safe_call(fi_getinfo(FI_VERSION(1, 6), dst_addr, "4092", 0, hints, &fi_pep));
    } else {
        /* server */
        safe_call(fi_getinfo(FI_VERSION(1, 6), NULL, "4092", FI_SOURCE, hints, &fi_pep));
    }

    // fi is a linked list of providers. For now just use first one
    //std::cout << "Using provider: " << fi_pep->fabric_attr->prov_name << std::endl;

     // Create a fabric object. This is the parent of everything else.
    std::cout << "Creating fabric object" << std::endl;
    safe_call(fi_fabric(fi_pep->fabric_attr, &fabric, NULL));

    // create and open event queue. Dont think I need to allocate
    std::cout << "Creating event queue" << std::endl;
    //memset(&eq_attr, 0, sizeof(eq_attr));
    //eq_attr.wait_obj = FI_WAIT_UNSPEC;
    //eq_attr.size = eq_attr.size;
    safe_call(fi_eq_open(fabric, &eq_attr, &eq, NULL));

    safe_call(fi_passive_ep(fabric, fi_pep, &pep, NULL));

    safe_call(fi_pep_bind(pep, &eq->fid, 0));

    safe_call(fi_listen(pep));

    std::cout << "Done starting server" << std::endl;

    return 0;
}

// I think this okay
int retrieve_conn_req(struct fid_eq *eq, struct fi_info **fi) {
	std::cout << "Retrieve Conn request" << std::endl;
	struct fi_eq_cm_entry entry;
	uint32_t event;
	ssize_t rd;
	int ret;

	// A synchronous (blocking) read of an event queue
	rd = fi_eq_sread(eq, &event, &entry, sizeof(entry), -1, 0);
	//rd = fi_eq_read(eq, &event, &entry, sizeof(entry), 0);
	if (rd != sizeof entry) {
		return (int) rd;
	}
	std::cout << entry.info << std::endl;

	*fi = entry.info;
	if (event != FI_CONNREQ) {
		ret = -FI_EOTHER;
		return ret;
	}

	std::cout << "Done Retrieving Conn request" << std::endl;

	return 0;
}

// I think alloc_active_res is for the queues
int alloc_ep_res(struct fi_info *fi) {
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
	return 0;
}

int alloc_active_res(struct fi_info *fi) {
	safe_call(alloc_ep_res(fi));
	safe_call(fi_endpoint(domain, fi, &ep, NULL));
	return 0;

}

int server_connect(void) {
	/*
		Functions in example:
		- retrieve connection request
		- fi_domain
		- fi_domain_bind // maybe
		- allocate resources
		- enable_ep_recv // try just fi_enable(ep)
		- accept connection
    */
	std::cout << "Server Connect" << std::endl;

	// Retrieve connection request
	safe_call(retrieve_conn_req(eq, &fi));

	// Domain
    std::cout << "Creating domain" << std::endl;
    safe_call(fi_domain(fabric, fi, &domain, NULL));
    // maybe need to do fi_domain_bind(domain, &eq->fid, 0)

    // Allocate resources
    safe_call(alloc_active_res(fi)); // Think this is my tx and rx queue. Also clean up

    safe_call(fi_enable(ep));

    // Accept connection
	return 0;
}

// Think okay
int open_fabric_res(void) {
	std::cout << "Opening fabric res" << std::endl;
	safe_call(fi_fabric(fi_pep->fabric_attr, &fabric, NULL));
	safe_call(fi_eq_open(fabric, &eq_attr, &eq, NULL));
	safe_call(fi_domain(fabric, fi_pep, &domain, NULL));
	// Dont think I need to bind domain
	std::cout << "Done opening fabric res" << std::endl;
	return 0;
}

int connect_ep(struct fid_ep *ep, struct fid_eq *eq, fi_addr_t *remote_addr) {
	safe_call(fi_connect(ep, remote_addr, NULL, 0));
	return 0;
}

int client_connect(void) {
	/*
		Functions in example:
		- ft_init
		- ft_init_oob // Dont think I need
		- ft_getInfo // Since dont have ops (I think) just fi_getinfo()
		- ft_open_fabric_res
		- ft_alloc_active_res
		- ft_enable_ep_recv
		- ft_connect_ep
    */

	// Init
    std::cout << "Starting client" << std::endl;
    hints = fi_allocinfo();
    hints->caps = FI_MSG;
    hints->ep_attr->type = FI_EP_MSG;

    // getinfo 127.0. 0.1
    safe_call(fi_getinfo(FI_VERSION(1, 6), dst_addr, "4092", 0, hints, &fi_pep))

    safe_call(open_fabric_res());

    // Allocate resources
    safe_call(alloc_active_res(fi_pep));

    safe_call(fi_enable(ep));

    std::cout << fi_pep->dest_addr << std::endl;

    safe_call(connect_ep(ep, eq, fi_pep->dest_addr));
    // int ret = fi_connect(ep, fi_pep->dest_addr, NULL, 0);
    // if (ret) {
    // 	std::cout << "Fucking idiot" << std::endl;
    // }

    std::cout << "Done starting client" << std::endl;

    return 0;
}

void usage() {
    std::cout << "Usage: ./Echo [optional server address]" << std::endl;
    std::cout << "            server address - remote server to connect to as a client." << std::endl;
    std::cout << "                             If not specified, will run as a server." << std::endl;
}

int main(int argc, char **argv) {
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
	//safe_call(init_fabric());
	if (!dst_addr) {
		safe_call(start_server());
	}
	// 2) Init endpoint
	//safe_call(init_endpoint());
	// 3) Bind fabric to endpoint
	//safe_call(bind_endpoint());

	ret = dst_addr ? client_connect() : server_connect();
	if (ret) {
		return ret;
	}

	if (dst_addr) {
		// Client stuff

	} else {
		// Server stuff

	}

	return ret;
}
