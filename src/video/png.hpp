#pragma once

#include <vector>

namespace imshell::video {
    // a png file's bytes for width * height rgba pixels (uncompressed deflate)
    std::vector<unsigned char> EncodePng(int width, int height, const unsigned char* rgba);
}
