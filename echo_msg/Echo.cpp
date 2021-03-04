// standard includes
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <iostream>
#include <errno.h>
#include <cassert>

// libfabric includes
#include <rdma/fabric.h>
#include <rdma/fi_domain.h>
#include <rdma/fi_endpoint.h>
#include <rdma/fi_cm.h>
#include <rdma/fi_tagged.h>
#include <rdma/fi_rma.h>
#include <rdma/fi_errno.h>

static struct fid_fabric *fabric;
static struct fid_domain *domain;
static struct fi_info *fi, *fi_pep, *hints;
static struct fid_eq *eq; // Event queue
static struct fi_eq_attr eq_attr; // Event queue data
static struct fid_pep *pep; // Passive endpoint to listen to incoming connection requests
static struct fid_ep *ep; // Endpoint
static struct fi_cq_attr cq_attr;
static struct fid_cq *tx_cq, *rx_cq;
fi_addr_t remote_addr; // Remote address

//Malloc Buffers
char *local_buf;
char *remote_buf;

const char *port = "8080";
const std::string address = "127.0.0.1";
int max_msg_size = 4096;
const char* dst_addr;

/**
 * Header for RPC
 */
struct Header {
	uint16_t data_len;
};

// This is pretty neat!
// From here: https://stackoverflow.com/a/14038590
#define safe_call(ans) { callCheck((ans), __FILE__, __LINE__); }
inline void callCheck(int err, const char *file, int line, bool abort=true) {
	if (err != 0) {
		std::cout << "Error: " << err << " " << fi_strerror(-err) << " " << file << ":" << line << std::endl;
		exit(0);
	}
}

/**
 * Waits for something to happen in the cq.
 * @param cq The CQ we are waiting on
 * @return error
 */
int wait_for_completion(struct fid_cq *cq) {
	std::cout << "Waiting for completion..." << std::endl;
	struct fi_cq_entry entry = {};
	int ret;
	while(true) {
		ret = fi_cq_read(cq, &entry, 1);
		if (ret > 0) return 0;
		if (ret != -FI_EAGAIN) {
			// New error on queue
			struct fi_cq_err_entry err_entry = {};
			fi_cq_readerr(cq, &err_entry, 0);
			std::cout << fi_strerror(err_entry.err) << " " <<
						 fi_cq_strerror(cq, err_entry.prov_errno,
										err_entry.err_data, nullptr, 0) << std::endl;
			return ret;
		}
	}
}

int retrieve_conn_req(struct fid_eq *eq, struct fi_info **fi) {
    struct fi_eq_cm_entry entry;
	uint32_t event;
	ssize_t rd;
	int ret;

	rd = fi_eq_sread(eq, &event, &entry, sizeof(entry), -1, 0);
	if (rd != sizeof entry) {
		//FT_PROCESS_EQ_ERR(rd, eq, "fi_eq_sread", "listen");
		return (int) rd;
	}

	*fi = entry.info;
	if (event != FI_CONNREQ) {
		//fprintf(stderr, "Unexpected CM event %d\n", event);
		ret = -FI_EOTHER;
		return ret;
	}

	return 0;
}

int complete_connect(struct fid_ep *ep, struct fid_eq *eq)
{
	struct fi_eq_cm_entry entry;
	uint32_t event;
	ssize_t rd;
	int ret;

	rd = fi_eq_sread(eq, &event, &entry, sizeof(entry), -1, 0);
	if (rd != sizeof(entry)) {
		//FT_PROCESS_EQ_ERR(rd, eq, "fi_eq_sread", "accept");
		ret = (int) rd;
		return ret;
	}

	if (event != FI_CONNECTED || entry.fid != &ep->fid) {
		//fprintf(stderr, "Unexpected CM event %d fid %p (ep %p)\n", event, entry.fid, ep);
		ret = -FI_EOTHER;
		return ret;
	}

	return 0;
}

int start_server(void) {
	// Init
	safe_call(fi_getinfo(FI_VERSION(1, 6), address.c_str(), port, FI_MSG, hints, &fi_pep));

	// Create a fabric object.
    std::cout << "Creating fabric" << std::endl;
    safe_call(fi_fabric(fi_pep->fabric_attr, &fabric, NULL));

    // Fabric Domain - maybe dont need
    //std::cout << "Creating domain" << std::endl;
    //safe_call(fi_domain(fabric, fi, &domain, NULL));

    // Create and open event queue
    std::cout << "Creating event queue" << std::endl;
    memset(&eq_attr, 0, sizeof(eq_attr));
    eq_attr.wait_obj = FI_WAIT_UNSPEC;
    eq_attr.size = eq_attr.size; // Might need idk
    safe_call(fi_eq_open(fabric, &eq_attr, &eq, NULL));

    // Create pep
    std::cout << "Creating passive endpoint" << std::endl;
    safe_call(fi_passive_ep(fabric, fi_pep, &pep, nullptr));

    // bind pep to eq
    std::cout << "Binding pep to eq" << std::endl;
    safe_call(fi_pep_bind(pep, &eq->fid, 0));

    // listen
    std::cout << "Listen" << std::endl;
    safe_call(fi_listen(pep));

    return 0;
}

int client_connect() {
	// Init
	safe_call(fi_getinfo(FI_VERSION(1, 6), dst_addr, port, 0, hints, &fi));

	// Create a fabric object.
    std::cout << "Creating fabric" << std::endl;
    safe_call(fi_fabric(fi->fabric_attr, &fabric, NULL));

    // Create and open event queue
    std::cout << "Creating event queue" << std::endl;
    memset(&eq_attr, 0, sizeof(eq_attr));
    eq_attr.wait_obj = FI_WAIT_UNSPEC;
    eq_attr.size = eq_attr.size; // Might need idk
    safe_call(fi_eq_open(fabric, &eq_attr, &eq, NULL));

    // Fabric Domain
    std::cout << "Creating domain" << std::endl;
    safe_call(fi_domain(fabric, fi, &domain, NULL));

    // Create rx and tx completion queues
    std::cout << "Creating tx completion queue" << std::endl;
    memset(&cq_attr, 0, sizeof(cq_attr));
    cq_attr.wait_obj = FI_WAIT_NONE;
    //cq_attr.format = FI_CQ_FORMAT_CONTEXT;
    cq_attr.size = fi->tx_attr->size;
    safe_call(fi_cq_open(domain, &cq_attr, &tx_cq, NULL));
    std::cout << "Creating rx completion queue" << std::endl;
    cq_attr.size = fi->rx_attr->size;
    safe_call(fi_cq_open(domain, &cq_attr, &rx_cq, NULL));

    // Endpoint
    safe_call(fi_endpoint(domain, fi, &ep, NULL))

    // // Bind queues to endpoints
    std::cout << "Binding TX CQ to EP" << std::endl;
    safe_call(fi_ep_bind(ep, &tx_cq->fid, FI_TRANSMIT));
    std::cout << "Binding RX CQ to EP" << std::endl;
    safe_call(fi_ep_bind(ep, &rx_cq->fid, FI_RECV));
    std::cout << "Binding EQ to EP" << std::endl;
    safe_call(fi_ep_bind(ep, &eq->fid, 0))

    std::cout << "Enabling endpoint" << std::endl;
    safe_call(fi_enable(ep));

    safe_call(fi_connect(ep, fi->dest_addr, NULL, 0));

    safe_call(complete_connect(ep, eq));

	return 0;
}

int server_connect() {
	// Retrieve connection request
	safe_call(retrieve_conn_req(eq, &fi));

	safe_call(fi_domain(fabric, fi, &domain, NULL));

	return 0;
}

int main(int argc, char **argv) {
	int ret;

	hints = fi_allocinfo();
    hints->caps = FI_MSG;
    hints->ep_attr->type = FI_EP_MSG;
    //hints->domain_attr->mr_mode = opts.mr_mode;

	// Get command line args
    if (argc < 2) { // Server
        std::cout << "Running as SERVER" << std::endl;
        //safe_call(fi_getinfo(FI_VERSION(1, 6), address.c_str(), port, FI_MSG, hints, &fi));
    } else if (argc == 2) { // Client
        dst_addr = argv[1];
        std::cout <<  "Running as CLIENT - server addr=" << dst_addr << std::endl;
        //safe_call(fi_getinfo(FI_VERSION(1, 6), dst_addr, port, 0, hints, &fi));
    } else {
        std::cout << "Too many arguments!" << std::endl;
        return -1;
    }

    if (!dst_addr) {
    	safe_call(start_server());
    }

    ret = dst_addr ? client_connect() : server_connect();
    if (ret) {
    	return ret;
    }

    // send greeting

    //fi_shurdown(ep, 0)

	return ret;
}