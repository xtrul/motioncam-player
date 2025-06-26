#ifndef SINGLE_INSTANCE_H
#define SINGLE_INSTANCE_H

struct SingleInstanceGuard {
    bool acquired;
    SingleInstanceGuard();
    ~SingleInstanceGuard();
    SingleInstanceGuard(const SingleInstanceGuard&) = delete;
    SingleInstanceGuard& operator=(const SingleInstanceGuard&) = delete;
};

#endif // SINGLE_INSTANCE_H
