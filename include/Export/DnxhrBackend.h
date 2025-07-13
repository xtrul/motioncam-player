#ifndef DNXHR_BACKEND_H
#define DNXHR_BACKEND_H

#include <string>
#include "Export/SliceBuffer.h"

class DnxhrBackend {
public:
    bool initialize(const std::string& path, int width, int height,
                    int fpsNum, int fpsDen);
    bool writeSlice(const SliceBuffer& slice);
    void finalize();
};

#endif // DNXHR_BACKEND_H
