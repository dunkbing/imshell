#include "png.hpp"

#include <cstdint>
#include <algorithm>
#include <cstdio>

namespace imshell::video {
    namespace {
        uint32_t crc32(uint32_t crc, const unsigned char* data, size_t size) {
            static uint32_t table[256];
            static bool ready = false;
            if (!ready) {
                for (uint32_t n = 0; n < 256; ++n) {
                    uint32_t c = n;
                    for (int k = 0; k < 8; ++k) {
                        c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
                    }
                    table[n] = c;
                }
                ready = true;
            }
            crc = ~crc;
            for (size_t i = 0; i < size; ++i) {
                crc = table[(crc ^ data[i]) & 0xff] ^ (crc >> 8);
            }
            return ~crc;
        }

        void put32(std::vector<unsigned char>& out, uint32_t v) {
            for (int s = 24; s >= 0; s -= 8) {
                out.push_back(static_cast<unsigned char>(v >> s));
            }
        }

        void chunk(std::vector<unsigned char>& out, const char* type, const std::vector<unsigned char>& data) {
            put32(out, static_cast<uint32_t>(data.size()));
            std::vector<unsigned char> body(type, type + 4);
            body.insert(body.end(), data.begin(), data.end());
            out.insert(out.end(), body.begin(), body.end());
            put32(out, crc32(0, body.data(), body.size()));
        }
    } // namespace

    // shortcut: deflate "stored" blocks, so a frame is its raw size (about 8 MB at 1080p); a real
    // compressor when a PNG has to be small
    std::vector<unsigned char> EncodePng(int width, int height, const unsigned char* rgba) {
        std::vector<unsigned char> raw; // each row led by filter 0
        raw.reserve(static_cast<size_t>(height) * (static_cast<size_t>(width) * 4 + 1));
        for (int y = 0; y < height; ++y) {
            raw.push_back(0);
            raw.insert(raw.end(), rgba + static_cast<size_t>(y) * width * 4, rgba + static_cast<size_t>(y + 1) * width * 4);
        }
        std::vector<unsigned char> z = {0x78, 0x01};
        uint32_t a = 1, b = 0;
        for (size_t i = 0; i < raw.size(); i += 65535) {
            const size_t n = std::min<size_t>(65535, raw.size() - i);
            z.push_back(i + n >= raw.size() ? 1 : 0);
            z.push_back(static_cast<unsigned char>(n));
            z.push_back(static_cast<unsigned char>(n >> 8));
            z.push_back(static_cast<unsigned char>(~n));
            z.push_back(static_cast<unsigned char>(~n >> 8));
            z.insert(z.end(), raw.begin() + static_cast<long>(i), raw.begin() + static_cast<long>(i + n));
        }
        for (unsigned char c : raw) { // adler-32
            a = (a + c) % 65521;
            b = (b + a) % 65521;
        }
        put32(z, (b << 16) | a);

        std::vector<unsigned char> png = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
        std::vector<unsigned char> header;
        put32(header, static_cast<uint32_t>(width));
        put32(header, static_cast<uint32_t>(height));
        header.insert(header.end(), {8, 6, 0, 0, 0}); // 8 bit rgba
        chunk(png, "IHDR", header);
        chunk(png, "IDAT", z);
        chunk(png, "IEND", {});
        return png;
    }
} // namespace imshell::video
