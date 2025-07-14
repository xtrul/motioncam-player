#include "Export/SliceWriter.h"
#include <cassert>
#include <string>

std::string g_AppBasePath;

int main(){
    SliceWriter writer;
    std::vector<Slice> slices(2);
    slices[0].coeffs = std::vector<int16_t>(64, 1);
    slices[1].coeffs = std::vector<int16_t>(64, 2);
    std::vector<uint8_t> out;
    bool ok = writer.writeFrame(slices, 16, 8, out);
    assert(ok);
    assert(out.size() % 8192 == 0);
    // Verify DNxHR frame start code
    assert(out.size() >= 8);
    assert(out[0] == 0x83 && out[1] == 0x0e && out[6] == 0x02 && out[7] == 0x80);
    return 0;
}
