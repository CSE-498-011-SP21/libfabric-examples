//
// Created by depaulsmiller on 2/17/21.
//

#include <rdma/fabric.h>
#include <rdma/fi_domain.h>
#include <cstdio>

#ifndef NETWORKLAYER_FABRICCXX_HH
#define NETWORKLAYER_FABRICCXX_HH

const int FIVersion = FI_VERSION(FI_MAJOR_VERSION, FI_MINOR_VERSION);

struct EventQueue {

    EventQueue(fid_fabric *fabric, fi_eq_attr *attr) {
        if (fi_eq_open(fabric, attr, &eq, NULL)) {
            perror("Opening event queue:");
        }
    }

    ~EventQueue() {
        if (fi_close(&eq->fid)) {
            perror("Closing event queue:");
        }
    }

    fid_eq *eq;
};

struct AccessDomain {
    AccessDomain(fid_fabric *fabric, fi_info *info) {
        if (fi_domain(fabric, info, &domain, NULL))
            perror("Creating access domain:");
    }

    ~AccessDomain() {
            if (fi_close(&domain->fid)) {
                perror("Closing access domain:");
            }
    }

    fid_domain *domain;
};

struct CompletionQueue {
    CompletionQueue(AccessDomain &d, fi_cq_attr *attr) {
        if (fi_cq_open(d.domain, attr, &cq, NULL)) {
            perror("Completion queue open:");
        }
    }

    ~CompletionQueue() {
        if (fi_close(&cq->fid)) {
            perror("Closing completion queue:");
        }
    }

    fid_cq *cq;
};

struct MemoryRegion {
    MemoryRegion(AccessDomain &d, const void *buf, size_t len, uint64_t access, uint64_t offset, uint64_t requested_key,
                 uint64_t flags) {
        if (fi_mr_reg(d.domain, buf, len, access, offset, requested_key, flags, &mr, NULL)) {
            perror("");
        }
    }

    ~MemoryRegion() {
        if (fi_close(&mr->fid))
            perror("");
    }

    fid_mr *mr;
};


#endif //NETWORKLAYER_FABRICCXX_HH
