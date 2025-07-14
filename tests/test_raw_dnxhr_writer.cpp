#include "Export/SliceWriter.h"
#include "Export/RawDnxhrWriter.h"
#include <cassert>
#include <cstdio>
#include <string>

std::string g_AppBasePath;

int main(){
    SliceWriter sw;
    std::vector<Slice> slices(2);
    slices[0].coeffs = std::vector<int16_t>(64, 1);
    slices[1].coeffs = std::vector<int16_t>(64, 2);
    std::vector<uint8_t> frame;
    sw.writeFrame(slices, 16, 8, frame);
    const std::string path = "test_frame.dnx";
    bool ok = writeRawDnxhr(frame, 16, 8, path);
    assert(ok);
    verifyDnxhrFile(path);
    std::remove(path.c_str());
    return 0;
}
