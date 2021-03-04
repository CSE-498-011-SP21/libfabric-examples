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

// I think this okay
int retrieve_conn_req(struct fid_eq *eq, struct fi_info **fi) {
	std::cout << "Retrieving Conn request" << std::endl;
	struct fi_eq_cm_entry entry;
	uint32_t event;
	ssize_t rd;
	int ret;

	std::cout << "Waiting..." << std::endl;

	// A synchronous (blocking) read of an event queue
	rd = fi_eq_sread(eq, &event, &entry, sizeof(entry), -1, 0);
	//rd = fi_eq_read(eq, &event, &entry, sizeof(entry), 0);
	if (ret > 0) {
		std::cout << "Returned " << rd << std::endl;
		return 0;
	}
	if (ret != -FI_EAGAIN) {
		return ret;
	}

	std::cout << "Done Retrieving Conn request" << std::endl;

	return -1;
}

int main(int argc, char **argv) {
	hints = fi_allocinfo();
    hints->caps = FI_MSG;
    hints->ep_attr->type = FI_EP_MSG;

    // Get command line args
	if (argc < 2) { // Server
		std::cout << "Running as SERVER" << std::endl;
		safe_call(fi_getinfo(FI_VERSION(1, 6), NULL, port, FI_SOURCE, hints, &fi));
	} else if (argc == 2) { // Client
		dst_addr = argv[1];
		std::cout <<  "Running as CLIENT - server addr=" << dst_addr << std::endl;
		safe_call(fi_getinfo(FI_VERSION(1, 6), dst_addr, port, 0, hints, &fi));
	} else {
		std::cout << "Too many arguments!" << std::endl;
        return -1;
	}

	// Create a fabric object.
    std::cout << "Creating fabric" << std::endl;
    safe_call(fi_fabric(fi->fabric_attr, &fabric, NULL));

    // Fabric Domain
    std::cout << "Creating domain" << std::endl;
	safe_call(fi_domain(fabric, fi, &domain, NULL));

	// Create and open event queue
    std::cout << "Creating event queue" << std::endl;
    memset(&eq_attr, 0, sizeof(eq_attr));
    eq_attr.wait_obj = FI_WAIT_UNSPEC;
    //eq_attr.size = eq_attr.size; // Might need idk
    safe_call(fi_eq_open(fabric, &eq_attr, &eq, NULL));

    // Might actually need address vector

    // Think I can just bind eq to the domain
    //safe_call(fi_domain_bind(domain, &eq->fid, FI_REG_MR));

    // If not then have to make a passive ep then bind it to eq
    //safe_call(fi_passive_ep(fabric, fi_pep, &pep, nullptr));
    //safe_call(fi_pep_bind(pep, &eq->fid, 0)); // Might need flags, TBD

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

    // Malloc Buffers
    fid_mr *mr;
	local_buf = new char[max_msg_size];
	remote_buf = new char[max_msg_size];
	memset(remote_buf, 0, max_msg_size);

	safe_call(fi_mr_reg(domain, remote_buf, max_msg_size,
					FI_SEND | FI_RECV, 0,
					0, 0, &mr, NULL));


    // Enable the endpoint
 //    std::cout << "Enabling EP" << std::endl;
	 //safe_call(fi_enable(ep));

	// Specify client vs server behavior
	if (dst_addr) { // Client
		std::cout << "Connecting client" << std::endl;
		// Create endpoint
		//safe_call(fi_endpoint(domain, fi, &ep, NULL));
		// Bind to eq
		safe_call(fi_ep_bind(ep, &eq->fid, 0))
		safe_call(fi_enable(ep));
		// Connect
		safe_call(fi_connect(ep, fi->dest_addr, NULL, 0));
		//safe_call(retrieve_conn_req(eq, &fi));




		// Communication Stuff
		assert(sizeof(size_t) == sizeof(uint64_t));

		size_t addrlen = 0;
        fi_getname(&ep->fid, nullptr, &addrlen);
        char *addr = new char[addrlen];
        safe_call(fi_getname(&ep->fid, addr, &addrlen));

        //std::cout << "Client: Sending " << addrlen << " " << (void*) addr << " " << fi->dest_addr << std::endl;
        //safe_call(fi_connect(ep, fi->dest_addr, NULL, 0));

        memcpy(local_buf, &addrlen, sizeof(uint64_t));
        memcpy(local_buf + sizeof(uint64_t), addr, addrlen);

        std::string data = "Hello, World!";
		Header header = {};
		header.data_len = data.length();

		memcpy(local_buf + sizeof(uint64_t) + addrlen, (char *) &header, sizeof(Header));
		memcpy(local_buf + sizeof(uint64_t) + addrlen + sizeof(Header), data.c_str(), data.length());

		std::cout << "Sending " << data << " to server" << std::endl;
		safe_call(fi_send(ep, local_buf, sizeof(uint64_t) + addrlen + data.length() + sizeof(Header), NULL, 0, NULL));
		//safe_call(fi_write(ep, local_buf, sizeof(uint64_t) + addrlen + data.length() + sizeof(Header), nullptr, remote_addr, 0, 0, nullptr));
		safe_call(wait_for_completion(tx_cq));
		std::cout << "Client done" << std::endl;

	} else { // Server
		// Connection stuff
		std::cout << "Connecting server" << std::endl;

		// Create pep
		safe_call(fi_passive_ep(fabric, fi, &pep, nullptr));

		// bind pep to eq
		safe_call(fi_pep_bind(pep, &eq->fid, 0));

		// listen
		safe_call(fi_listen(pep));
		//safe_call(wait_for_completion(rx_cq));
		safe_call(retrieve_conn_req(eq, &fi));

		// After processing, allocate ep
		//safe_call(fi_endpoint(domain, fi, &ep, NULL));
		// bind ep to same eq
		std::cout << "Binding EQ to EP" << std::endl;
    	safe_call(fi_ep_bind(ep, &eq->fid, 0));
   

    	safe_call(fi_enable(ep));
		// accept on ep // make sure its OFI accept not socket accept
		safe_call(fi_accept(ep, NULL, 0));
		//safe_call(wait_for_completion(rx_cq));



		// Communication stuff
		std::cout << "Server: Receiving client addres" << std::endl;
		safe_call(fi_recv(ep, remote_buf, max_msg_size, nullptr, 0, nullptr));
		safe_call(wait_for_completion(rx_cq));
		std::cout << "Server done" << std::endl;
	}

	return 0;
}