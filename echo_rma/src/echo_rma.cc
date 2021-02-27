#include <rdma/fabric.h>
#include <rdma/fi_domain.h>
#include <rdma/fi_endpoint.h>
#include <rdma/fi_rma.h>
#include <rdma/fi_errno.h>
#include <rdma/fi_cm.h>
#include <cstring>
#include <cassert>

#include <iostream>

// using namespace std;

/* Wait for a new completion on the completion queue (from libfarbic_helloworld) */
int wait_for_completion(struct fid_cq *cq) {
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

// This is pretty neat!
// From here: https://stackoverflow.com/a/14038590
#define safe_call(ans) { callCheck((ans), __FILE__, __LINE__); }
inline void callCheck(int err, const char *file, int line, bool abort=true) {
	if (err != 0) {
		std::cout << "Error: " << err << " " << fi_strerror(-err) << " " << file << ":" << line << std::endl;
		exit(0);
	}
}

struct Header {
	uint16_t data_len;
};

const char *port = "8080";
char *local_buf;
char *remote_buf;
int max_msg_size = 4096;

int main(int argc, char **argv) {
	// I don't release anything which isn't great, but eh whatever. 
	fi_info *hints, *info;

	hints = fi_allocinfo();
	hints->ep_attr->type = FI_EP_RDM;
	hints->caps = FI_MSG;

	bool is_client = argc == 3;
	std::string dest_addr;

	if (is_client) {
		// Client
		std::cout << "Initializing client" << std::endl;
		dest_addr = argv[1];
		std::cout << "Address is " << dest_addr << std::endl;
		safe_call(fi_getinfo(FI_VERSION(1, 6), argv[1], port, 0, hints, &info));
//		FI_ADDR_STR

	} else {
		// Server
		std::cout << "Initializing server" << std::endl;
		safe_call(fi_getinfo(FI_VERSION(1, 6), nullptr, port, FI_SOURCE, hints, &info));
	}


	// Fabric object. 
	fid_fabric *fab;
	safe_call(fi_fabric(info->fabric_attr, &fab, nullptr));
	// Domain in the fabric
	fid_domain *domain;
	safe_call(fi_domain(fab, info, &domain, nullptr));

	fid_cntr *ctr;
	fi_cntr_attr cntr_attr = {};
	cntr_attr.events = FI_CNTR_EVENTS_COMP;
	cntr_attr.wait_obj = FI_WAIT_UNSPEC;
	safe_call(fi_cntr_open(domain, &cntr_attr, &ctr, nullptr));

	fid_av *av;
	fi_av_attr av_attr = {};
	av_attr.type = info->domain_attr->av_type;
	av_attr.count = 1;
	safe_call(fi_av_open(domain, &av_attr, &av, nullptr));

	fid_ep *ep;
	safe_call(fi_endpoint(domain, info, &ep, nullptr));

	// Bind all this stuff
	safe_call(fi_ep_bind(ep, &av->fid, 0));
	safe_call(fi_ep_bind(ep, &ctr->fid, FI_REMOTE_WRITE | FI_WRITE));

	// Enable the endpoint
	safe_call(fi_enable(ep));

	fid_mr *mr;
	local_buf = new char[max_msg_size];
	remote_buf = new char[max_msg_size];
	memset(remote_buf, 0, max_msg_size);

	safe_call(fi_mr_reg(domain, remote_buf, max_msg_size,
					FI_WRITE | FI_REMOTE_WRITE | FI_READ | FI_REMOTE_READ, 0,
					0, 0, &mr, NULL));

	if(is_client) {
		fi_addr_t remote_addr;
		if (1 != fi_av_insert(av, info->dest_addr, 1, &remote_addr, 0, nullptr)) {
			std::cerr << "Error on av_insert" << std::endl;
			exit(1);
		}

		// Lets send some stuff. 
		size_t addrlen = 0;
    	fi_getname(&ep->fid, nullptr, &addrlen); // This returns an acceptable nonzero return code (hence no safe_call)
		char *addr = new char[addrlen];
		safe_call(fi_getname(&ep->fid, addr, &addrlen));

		// Might be able to create a "packet" struct that points to the location of the local_buf, then
		// we would have to deal with this painful memcpy stuff. Although that struct would also be sort
		// of sketch. 
		memcpy(local_buf, &addrlen, sizeof(uint64_t));
		memcpy(local_buf + sizeof(uint64_t), addr, addrlen);

		std::string data = argv[2];
		Header header = {};
		header.data_len = data.length();

		memcpy(local_buf + sizeof(uint64_t) + addrlen, (char *) &header, sizeof(Header));
		memcpy(local_buf + sizeof(uint64_t) + addrlen + sizeof(Header), data.c_str(), data.length());

		std::cout << "Sending " << data << " to server" << std::endl;
		safe_call(fi_write(ep, local_buf, sizeof(uint64_t) + addrlen + data.length() + sizeof(Header), nullptr, remote_addr, 0, 0, nullptr));
		safe_call(fi_cntr_wait(ctr, 2, -1)); // Wait until the server responds. 
		std::cout << "The server responded" << std::endl;
		Header h = *(Header *) (remote_buf);
		std::string rec_data(remote_buf + sizeof(Header), h.data_len);
		std::cout << "Server responded with " << h.data_len << " bytes of data: " << rec_data << std::endl;
	} else {
		safe_call(fi_cntr_wait(ctr, 1, -1));
		std::cout << "Received something from the client" << std::endl;

		uint64_t sizeOfAddress = *(uint64_t *) remote_buf;

		fi_addr_t remote_addr;

		if (1 != fi_av_insert(av, remote_buf + sizeof(uint64_t), 1, &remote_addr, 0, NULL)) {
			std::cerr << "ERROR - fi_av_insert did not return 1" << std::endl;
			exit(1);
		}
		Header h = *(Header *) (remote_buf + sizeof(uint64_t) + sizeOfAddress);

		std::string rec_data(remote_buf + sizeof(uint64_t) + sizeOfAddress + sizeof(Header), h.data_len);
		std::cout << "Client sent " << h.data_len << " bytes of data: " << rec_data << std::endl;

		Header header = {};
		header.data_len = rec_data.length();

		memcpy(local_buf, (char *) &header, sizeof(Header));
		memcpy(local_buf + sizeof(Header), rec_data.c_str(), rec_data.length());
		std::cout << "Responding to the client" << std::endl;
		// std::cout << fi_cntr_read(ctr) << std::endl;
		safe_call(fi_write(ep, local_buf, rec_data.length() + sizeof(Header), nullptr, remote_addr, 0, 0, nullptr));
		// std::cout << fi_cntr_read(ctr) << std::endl;
		safe_call(fi_cntr_wait(ctr, 2, -1)); // Wait until the message is sent (the counter isn't incremented for the fi_write for some reason so this never completes. )
		// std::cout << fi_cntr_readerr(ctr) << std::endl;
		std::cout << "Response sent" << std::endl;
	}
}