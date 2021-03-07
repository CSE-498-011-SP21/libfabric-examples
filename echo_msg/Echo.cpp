#include <rdma/fabric.h>
#include <rdma/fi_domain.h>
#include <rdma/fi_endpoint.h>
#include <rdma/fi_rma.h>
#include <rdma/fi_errno.h>
#include <rdma/fi_cm.h>
#include <rdma/fi_tagged.h>

#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <spdlog/spdlog.h>

#include <cstring>
#include <chrono>
#include <thread>
#include <iostream>

fid_fabric *fabric;
fid_domain *domain;
fi_info *hints, *fi;
fid_mr *mr;
fid_ep *ep;
fid_pep *pep;
fid_eq *eq;
fi_eq_attr eq_attr = {};
fid_cq *rq, *tq;
struct fi_cq_attr cq_attr = {};
uint32_t event;
struct fi_eq_cm_entry entry;

const char *port = "8080";
const size_t max_msg_size = 4096;
const char* dest_addr;
char *remote_buf = new char[max_msg_size];
char *local_buf = new char[max_msg_size];


// Very nice way of error checking
#define safe_call(ans) callCheck((ans), __FILE__, __LINE__)
inline int callCheck(int err, const char *file, int line, bool abort=true) {
    if (err < 0) {
        std::cout << "Error: " << err << " " << fi_strerror(-err) << " " << file << ":" << line << std::endl;
        exit(0);
    }
    return err;
}


// Helper method to wait for data to be read from a cq (tx or rx queues in this case)
int wait_for_completion(struct fid_cq *cq) {
    fi_cq_entry entry;
    int ret;
    while (1) {
        ret = fi_cq_read(cq, &entry, 1);
        if (ret > 0) return 0;
        if (ret != -FI_EAGAIN) {
            // New error on queue
            struct fi_cq_err_entry err_entry;
            fi_cq_readerr(cq, &err_entry, 0);
            std::cerr << "ERROR " << std::endl;
            return ret;
        }
    }
}

int run_server() {
	// Sets event queue attributes
    eq_attr.size = 2; // Prob not necessary
    eq_attr.wait_obj = FI_WAIT_UNSPEC;

    // Open Event queue
    std::cout << "Opening event queue" << std::endl;
    safe_call(fi_eq_open(fabric, &eq_attr, &eq, nullptr));

    // Create a passive endpoint
    std::cout << "Creating passive endpoint" << std::endl;
    safe_call(fi_passive_ep(fabric, fi, &pep, nullptr));

    // Bind the event queue to the passive endpoint
    std::cout << "Binding eq to pep" << std::endl;
    safe_call(fi_pep_bind(pep, &eq->fid, 0));

    std::cout << "Transitioning pep to listening state" << std::endl;
    safe_call(fi_listen(pep));


    std::cout << "Waiting for connection request" << std::endl;
    // Blocking read of the event queue
    int rd = fi_eq_sread(eq, &event, &entry, sizeof(entry), -1, 0);
    if (rd != sizeof(entry)) {
        std::cerr << "Probably and error" << std::endl;
        exit(1);
    }

    if (event != FI_CONNREQ) {
        std::cerr << "Incorrect event type" << std::endl;
        exit(1);
    }

    std::cout << "Connection request received" << std::endl;

    // Use the entry info to create the domain
    safe_call(fi_domain(fabric, entry.info, &domain, nullptr));
    
    // Now we can allocate an active endpoint. 
    std::cout << "Creating active endpoint" << std::endl;
    safe_call(fi_endpoint(domain, entry.info, &ep, nullptr));

    // Define transmit and recieve queue attributes and open them
    std::cout << "Opening transmit and recieve queues" << std::endl;
    // RQ
    cq_attr.size = fi->rx_attr->size;
    cq_attr.wait_obj = FI_WAIT_UNSPEC;
    safe_call(fi_cq_open(domain, &cq_attr, &rq, nullptr));
    // TQ
    cq_attr.size = fi->tx_attr->size;
    safe_call(fi_cq_open(domain, &cq_attr, &tq, nullptr));

    // Bind the tx and rx queues
    std::cout << "Binding the tx and rx queues" << std::endl;
    safe_call(fi_ep_bind(ep, &rq->fid, FI_RECV));
    safe_call(fi_ep_bind(ep, &tq->fid, FI_TRANSMIT));

    // Bind event queue to endpoint
    std::cout << "Binding eq to ep" << std::endl;
    safe_call(fi_ep_bind(ep, &eq->fid, 0));

    // Enable the endpoint
    safe_call(fi_enable(ep));

    // Memory Region
    safe_call(fi_mr_reg(domain, remote_buf, max_msg_size,
                         FI_WRITE | FI_REMOTE_WRITE | FI_READ | FI_REMOTE_READ, 0,
                         0, 0, &mr, NULL));

    // Accept connection from client
    safe_call(fi_accept(ep, nullptr, 0));

    // Blocking read of event queue
    int addr_len = safe_call(fi_eq_sread(eq, &event, &entry, sizeof(entry), -1, 0));
    if (event != FI_CONNECTED) {
        std::cerr << "Not a connected event" << std::endl;
        exit(1);
    }

    std::cout << "Connected!" << std::endl;

    // Send data to client
    std::string data = "Hello, World!";
    memcpy(local_buf, data.c_str(), data.length());
    safe_call(fi_send(ep, local_buf, data.length(), nullptr, 0, nullptr));
    safe_call(wait_for_completion(tq));
}

int run_client() {
	// Create Domain
    safe_call(fi_domain(fabric, fi, &domain, nullptr));

    // Sets event queue attributes
    eq_attr.size = 2; // Prob not necessary
    eq_attr.wait_obj = FI_WAIT_UNSPEC;

    // Open Event queue
    std::cout << "Opening event queue" << std::endl;
    safe_call(fi_eq_open(fabric, &eq_attr, &eq, nullptr));

    // Create endpoint
    std::cout << "Creating endpoint" << std::endl;
    safe_call(fi_endpoint(domain, fi, &ep, nullptr));

    // Define transmit and recieve queue attributes and open them
    std::cout << "Opening transmit and recieve queues" << std::endl;
    // RQ
    cq_attr.size = fi->rx_attr->size;
    cq_attr.wait_obj = FI_WAIT_UNSPEC;
    safe_call(fi_cq_open(domain, &cq_attr, &rq, nullptr));
    // TQ
    cq_attr.size = fi->tx_attr->size;
    safe_call(fi_cq_open(domain, &cq_attr, &tq, nullptr));

    // Bind the tx and rx queues
    std::cout << "Binding the tx and rx queues" << std::endl;
    safe_call(fi_ep_bind(ep, &rq->fid, FI_RECV));
    safe_call(fi_ep_bind(ep, &tq->fid, FI_TRANSMIT));

    // Bind event queue to the endpoint
    std::cout << "Binding eq to ep" << std::endl;
    safe_call(fi_ep_bind(ep, &eq->fid, 0));

    // Enable endpoint
    safe_call(fi_enable(ep));

    // Connect to the server
    std::cout << "Sending connection request" << std::endl;
    safe_call(fi_connect(ep, fi->dest_addr, nullptr, 0));

    // Memeory region
    safe_call(fi_mr_reg(domain, remote_buf, max_msg_size,
                         FI_WRITE | FI_REMOTE_WRITE | FI_READ | FI_REMOTE_READ, 0,
                         0, 0, &mr, NULL));

    std::cout << "Waiting for connection accept" << std::endl;
    int out_len = safe_call(fi_eq_sread(eq, &event, &entry, sizeof(entry), -1, 0));
    if (event != FI_CONNECTED) {
        std::cerr << "Wrong event" << std::endl;
        exit(1);
    }
    std::cout << "Connected" << std::endl;

    // Recieve a message from the server
    safe_call(fi_recv(ep, remote_buf, max_msg_size, nullptr, 0, nullptr));
    safe_call(wait_for_completion(rq));

    std::cout << "Received: " << remote_buf << std::endl;
}

int main(int argc, char **argv) {

    hints = fi_allocinfo();
    hints->ep_attr->type = FI_EP_MSG;
    hints->caps = FI_MSG;

    // Get command line args
    if (argc < 2) { // Server
        std::cout << "Running as SERVER" << std::endl;
        safe_call(fi_getinfo(FI_VERSION(1, 6), nullptr, port, FI_SOURCE, hints, &fi));
    } else if (argc == 2) { // Client
        dest_addr = argv[1];
        std::cout <<  "Running as CLIENT - server addr=" << dest_addr << std::endl;
        safe_call(fi_getinfo(FI_VERSION(1, 6), argv[1], port, 0, hints, &fi));
    } else {
        std::cout << "Too many arguments!" << std::endl;
        return -1;
    }

    // Fabric object. 
    std::cout << "Creating fabric object" << std::endl;
    safe_call(fi_fabric(fi->fabric_attr, &fabric, nullptr));

    if (dest_addr) {
        run_client();
    } else {
    	run_server();
    }
}