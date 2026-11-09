#pragma once

#include <cstddef>
#include <cstdint>

// files baked into the binary by mydear_embed_fonts / mydear_embed_images (cmake/MydearEmbed.cmake);
// the host calls both, the generated files define the get* functions
struct EmbeddedFont {
    const char* name; // file name without extension
    const uint8_t* data;
    size_t size;
};

struct EmbeddedImage {
    const char* name;
    const uint8_t* data;
    size_t size;
};

extern "C" const EmbeddedFont* getEmbeddedFonts();
extern "C" size_t getEmbeddedFontCount();
extern "C" const EmbeddedImage* getEmbeddedImages();
extern "C" size_t getEmbeddedImageCount();

const EmbeddedFont* findEmbeddedFont(const char* name);
const EmbeddedImage* findEmbeddedImage(const char* name);
