//
// Created by depaulsmiller on 2/17/21.
//

#include <rdma/fabric.h>
#include <rdma/fi_domain.h>
#include <rdma/fi_endpoint.h>

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <atomic>
#include <memory>

#ifndef NETWORKLAYER_FABRICCXX_HH
#define NETWORKLAYER_FABRICCXX_HH

#define ERRCHK(x) error_check((x), __FILE__, __LINE__);

inline void error_check(int err, std::string file, int line) {
    if (err) {
        std::cerr << "ERROR (" << err << "): " << fi_strerror(-err) << std::endl;
        std::cerr << file << ":" << line << std::endl;

        exit(1);
    }
}

const int FIVersion = FI_VERSION(FI_MAJOR_VERSION, FI_MINOR_VERSION);

class FabricInfo {
public:
    FabricInfo() : ref(new std::atomic_uint(1)) {
        info = fi_allocinfo();
    }

    FabricInfo(uint32_t version, const char *node, const char *service, uint64_t flags,
               FabricInfo &hints) : ref(new std::atomic_uint(1)) {
        ERRCHK(fi_getinfo(version, node, service, flags, hints.get(), &info));
    }

    FabricInfo(const FabricInfo &other) {
        other.ref->operator++();
        info = other.info;
        ref = other.ref;
    }

    FabricInfo(FabricInfo &&other) {
        info = other.info;
        ref = other.ref;
        other.ref = nullptr;
        other.info = nullptr;
    }

    ~FabricInfo() {
        if (info && ref->operator--() == 0) {
            fi_freeinfo(info);
            delete ref;
        }
    }

    FabricInfo& operator=(const FabricInfo &other) {
        if(&other == this)
            return *this;
        other.ref->operator++();
        info = other.info;
        ref = other.ref;
        return *this;
    }

    FabricInfo& operator=(FabricInfo &&other) noexcept {
        if(&other == this)
            return *this;
        info = other.info;
        ref = other.ref;
        other.ref = nullptr;
        other.info = nullptr;
        return *this;
    }


    fi_info *operator->() const {
        return info;
    }

    fi_info *get() const {
        return info;
    }

private:
    fi_info *info;
    std::atomic_uint *ref;
};

class Fabric {
public:
    Fabric(FabricInfo &info) : info_(info), ref(new std::atomic_uint(1)) {
        ERRCHK(fi_fabric(info->fabric_attr, &fabric, nullptr));
    }

    Fabric(const Fabric &other) {
        other.ref->operator++();
        ref = other.ref;
        fabric = other.fabric;
        info_ = other.info_;
    }

    Fabric(Fabric &&other) {
        fabric = other.fabric;
        ref = other.ref;
        other.ref = nullptr;
        other.fabric = nullptr;
        info_ = std::move(other.info_);
    }

    ~Fabric() {
        if (fabric && ref->operator--() == 0) {
            ERRCHK(fi_close(&fabric->fid));
            delete ref;
        }
    }

    fid_fabric *operator->() const {
        return fabric;
    }

    fid_fabric *get() const {
        return fabric;
    }

private:
    fid_fabric *fabric;
    std::atomic_uint *ref;
    FabricInfo info_;
};

class EventQueue {
public:
    EventQueue(Fabric &fabric, fi_eq_attr *attr) : fabric_(fabric) {
        if (fi_eq_open(fabric.get(), attr, &eq, nullptr)) {
            perror("Opening event queue:");
        }
    }

    EventQueue(const EventQueue &) = delete;

    EventQueue(EventQueue &&) = delete;

    ~EventQueue() {
        if (fi_close(&eq->fid)) {
            perror("Closing event queue:");
        }
    }

    fid_eq *operator->() const {
        return eq;
    }

    fid_eq *get() const {
        return eq;
    }

private:
    Fabric fabric_;
    fid_eq *eq;
};

class AccessDomain {
public:
    AccessDomain(Fabric &fabric, FabricInfo &info) : ref(new std::atomic_uint(1)), info_(info), fabric_(fabric) {
        if (fi_domain(fabric.get(), info.get(), &domain, nullptr))
            perror("Creating access domain:");
    }

    AccessDomain(AccessDomain &&other) : info_(std::move(other.info_)), fabric_(std::move(other.fabric_)) {
        domain = other.domain;
        ref = other.ref;
        other.ref = nullptr;
        other.domain = nullptr;
    }

    AccessDomain(const AccessDomain & other) : info_(other.info_), fabric_(other.fabric_) {
        other.ref->operator++();
        domain = other.domain;
        ref = other.ref;
        info_ = other.info_;
    }

    ~AccessDomain() {
        if (domain && ref->operator--() == 0) {
            if (fi_close(&domain->fid)) {
                perror("Closing access domain:");
            }
            delete ref;
        }
    }

    fid_domain *operator->() const {
        return domain;
    }

    fid_domain *get() const {
        return domain;
    }

private:
    fid_domain *domain;
    std::atomic_uint *ref;
    FabricInfo info_;
    Fabric fabric_;
};

class CompletionQueue {
public:
    CompletionQueue(AccessDomain &domain, fi_cq_attr *attr) : domain_(domain) {
        if (fi_cq_open(domain_.get(), attr, &cq, nullptr)) {
            perror("Completion queue open:");
        }
    }

    CompletionQueue(const CompletionQueue &) = delete;

    CompletionQueue(CompletionQueue &&) = delete;

    ~CompletionQueue() {
        if (fi_close(&cq->fid)) {
            perror("Closing completion queue:");
        }
    }

    fid_cq *operator->() const {
        return cq;
    }

    fid_cq *get() const {
        return cq;
    }

private:

    fid_cq *cq;
    AccessDomain domain_;
};

class MemoryRegion {
public:
    MemoryRegion(AccessDomain &domain, const void *buf, size_t len, uint64_t access, uint64_t offset,
                 uint64_t requested_key,
                 uint64_t flags) : domain_(domain) {
        if (fi_mr_reg(domain_.get(), buf, len, access, offset, requested_key, flags, &mr, NULL)) {
            perror("");
        }
    }

    MemoryRegion(const MemoryRegion &) = delete;

    MemoryRegion(MemoryRegion &&) = delete;

    ~MemoryRegion() {
        if (fi_close(&mr->fid))
            perror("");
    }

    fid_mr *operator->() const {
        return mr;
    }

    fid_mr *get() const {
        return mr;
    }
private:

    fid_mr *mr;
    AccessDomain domain_;
};

class ActiveEndpoint {
public:

    ActiveEndpoint(AccessDomain &domain, FabricInfo &info) : ref(new std::atomic_uint(1)), domain_(domain),
                                                             info_(info) {
        ERRCHK(fi_endpoint(domain.get(), info.get(),
                           &ep, nullptr));
    }

    ActiveEndpoint(const ActiveEndpoint &other) : domain_(other.domain_), info_(other.info_) {
        other.ref->fetch_add(1);
        ep = other.ep;
        ref = other.ref;
    }

    ActiveEndpoint(ActiveEndpoint &&other) noexcept: domain_(std::move(other.domain_)), info_(std::move(other.info_)) {
        ep = other.ep;
        ref = other.ref;
        other.ep = nullptr;
        other.ref = nullptr;
    }

    ~ActiveEndpoint() {
        if (ep) {
            std::cerr << "Closing endpoint" << std::endl;
            ERRCHK(fi_close(&ep->fid));
        }
    }

    fid_ep *operator->() {
        return ep;
    }

    fid_ep *get() {
        return ep;
    }

    void enable() {
        ERRCHK(fi_enable(ep));
    }

    //void bind(CompletionQueue &cq, uint64_t flags) {
    //    ERRCHK(fi_ep_bind(ep, &cq->fid, flags));
    //    boundCQs.push_front(cq);
    //}

private:
    fid_ep *ep;
    std::atomic_uint *ref;
    AccessDomain domain_;
    FabricInfo info_;
};

class AddressVector {
public:

    AddressVector(AccessDomain &domain, fi_av_attr *attr) : ref(new std::atomic_uint(1)), domain_(domain) {
        ERRCHK(fi_av_open(domain.get(), attr, &av, nullptr));
    }

    AddressVector(const AddressVector &other) : domain_(other.domain_) {
        av = other.av;
        ref = other.ref;
        ref->operator++();
    }

    AddressVector(AddressVector &&other) noexcept: domain_(std::move(other.domain_)) {
        av = other.av;
        ref = other.ref;
        other.av = nullptr;
        other.ref = nullptr;
    }

    ~AddressVector() {
        if (av && ref->fetch_sub(1) - 1 == 0) {
            ERRCHK(fi_close(&av->fid));
        }
    }

    fid_av *operator->() {
        return av;
    }

    fid_av *get() {
        return av;
    }

private:
    fid_av *av;
    std::atomic_uint *ref;
    AccessDomain domain_;
};

#endif //NETWORKLAYER_FABRICCXX_HH
