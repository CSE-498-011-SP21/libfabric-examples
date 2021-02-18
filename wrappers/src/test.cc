//
// Created by depaulsmiller on 2/17/21.
//

#include <Fabric.hh>
#include <iostream>
#include <rdma/fi_endpoint.h>
#include <rdma/fi_rma.h>
#include <rdma/fi_cm.h>

int main(int argc, char **argv) {

    FabricInfo hints;

    hints->addr_format = FI_SOCKADDR_IN; // want addresses we are used to
    hints->ep_attr->type = FI_EP_MSG;
    hints->domain_attr->mr_mode = FI_MR_BASIC;
    hints->caps = FI_MSG | FI_RMA; // want RMA and MSG
    hints->mode = FI_CONTEXT | FI_LOCAL_MR | FI_RX_CQ_DATA;

    FabricInfo info(FIVersion, nullptr, "8080", 0, hints);

    Fabric fabric(info);

    fi_eq_attr eq_attr = {
            .size = 0,
            .flags = 0,
            .wait_obj = FI_WAIT_UNSPEC,
            .signaling_vector = 0,
            .wait_set = NULL,
    };

    EventQueue eq(fabric, &eq_attr);

    AccessDomain domain(fabric, info);

    struct fi_cq_attr cq_attr = {
            .size = 0,
            .flags = 0,
            .format = FI_CQ_FORMAT_MSG,
            .wait_obj = FI_WAIT_UNSPEC,
            .signaling_vector = 0,
            .wait_cond = FI_CQ_COND_NONE,
            .wait_set = NULL,
    };

    CompletionQueue cq(domain, &cq_attr);

    char *buf = new char[256];

    MemoryRegion mr(domain, buf, 256, FI_REMOTE_READ | FI_REMOTE_WRITE | FI_SEND | FI_RECV, 0,
                                             0, 0);

    std::cerr << fi_mr_key(mr.get()) << std::endl;

    fid_pep *pep;

    if (fi_passive_ep(fabric.get(), info.get(), &pep, NULL)) {
        perror("");
    }

    if (fi_pep_bind(pep, &eq->fid, 0)) {
        perror("");
    }

    if (fi_listen(pep)) {
        perror("");
    }

    if (fi_close(&pep->fid)) {
        perror("");
    }

    delete[] buf;

    return 0;
}