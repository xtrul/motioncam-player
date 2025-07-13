#ifndef GPU_SLICE_TRANSFER_H
#define GPU_SLICE_TRANSFER_H

#include <vector>
#include "Export/SliceBuffer.h"

class GpuSliceTransfer {
public:
    bool mapSlices(void* buffer, size_t size);
    bool unmap();
    const std::vector<SliceBuffer>& getSlices() const { return m_slices; }
private:
    std::vector<SliceBuffer> m_slices;
};

#endif // GPU_SLICE_TRANSFER_H
