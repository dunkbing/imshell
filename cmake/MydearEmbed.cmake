# mydear_embed_fonts(<dir>) / mydear_embed_images(<dir>): every font (.ttf .otf) or image (.png .jpg) in <dir> as a C++ byte array, regenerated when a file changes; sets EMBEDDED_FONTS_FILE / EMBEDDED_IMAGES_FILE to add to the target

function(mydear_embed_fonts dir)
  # Embed font files as resources
  file(GLOB FONT_FILES "${dir}/*.ttf" "${dir}/*.otf")
  set(RESOURCE_FILE "${CMAKE_CURRENT_BINARY_DIR}/embedded_fonts.cpp")
  set(GENERATOR_SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/generate_fonts.cmake")

  # create a generator script that will be executed only when fonts change.
  # it builds the source in memory and writes it once: an append per font reopens a
  # file a virus scanner may still hold, and a failed append leaves it half written
  file(
        WRITE ${GENERATOR_SCRIPT}
        "
file(GLOB FONT_FILES \"${dir}/*.ttf\" \"${dir}/*.otf\")
set(RESOURCE_FILE \"${RESOURCE_FILE}\")

set(OUT \"#include <cstdint>\\n#include <cstddef>\\n\\n\")
string(APPEND OUT \"struct EmbeddedFont {\\n\")
string(APPEND OUT \"    const char* name;\\n\")
string(APPEND OUT \"    const uint8_t* data;\\n\")
string(APPEND OUT \"    size_t size;\\n\")
string(APPEND OUT \"};\\n\\n\")

if(FONT_FILES)
    foreach(FONT_FILE \${FONT_FILES})
        get_filename_component(FONT_NAME \${FONT_FILE} NAME_WE)
        string(MAKE_C_IDENTIFIER \${FONT_NAME} FONT_VAR)

        file(READ \${FONT_FILE} FONT_DATA HEX)
        string(REGEX REPLACE \"([0-9a-fA-F][0-9a-fA-F])\" \"0x\\\\1,\" FONT_DATA \${FONT_DATA})
        string(REGEX REPLACE \",\$\" \"\" FONT_DATA \${FONT_DATA})

        string(APPEND OUT \"static const uint8_t \${FONT_VAR}_data[] = {\${FONT_DATA}};\\n\")
    endforeach()

    string(APPEND OUT \"\\nstatic const EmbeddedFont embedded_fonts[] = {\\n\")
    foreach(FONT_FILE \${FONT_FILES})
        get_filename_component(FONT_NAME \${FONT_FILE} NAME_WE)
        string(MAKE_C_IDENTIFIER \${FONT_NAME} FONT_VAR)
        string(APPEND OUT \"    {\\\"\${FONT_NAME}\\\", \${FONT_VAR}_data, sizeof(\${FONT_VAR}_data)},\\n\")
    endforeach()
    string(APPEND OUT \"};\\n\\n\")

    string(APPEND OUT \"extern \\\"C\\\" const EmbeddedFont* getEmbeddedFonts() { return embedded_fonts; }\\n\")
    string(APPEND OUT \"extern \\\"C\\\" size_t getEmbeddedFontCount() { return sizeof(embedded_fonts) / sizeof(embedded_fonts[0]); }\\n\")
else()
    string(APPEND OUT \"extern \\\"C\\\" const EmbeddedFont* getEmbeddedFonts() { return nullptr; }\\n\")
    string(APPEND OUT \"extern \\\"C\\\" size_t getEmbeddedFontCount() { return 0; }\\n\")
endif()

file(WRITE \${RESOURCE_FILE} \"\${OUT}\")
"
    )

  # add custom command that only runs when font files change
  add_custom_command(
        OUTPUT ${RESOURCE_FILE}
        COMMAND ${CMAKE_COMMAND} -P ${GENERATOR_SCRIPT}
        DEPENDS ${FONT_FILES}
        COMMENT "Embedding font resources"
        VERBATIM
    )

  # make the resource file available to parent scope
  set(EMBEDDED_FONTS_FILE ${RESOURCE_FILE} PARENT_SCOPE)
endfunction()

function(mydear_embed_images dir)
  # Embed image files as resources
  file(
        GLOB IMAGE_FILES
        "${dir}/*.png"
        "${dir}/*.jpg"
        "${dir}/*.jpeg"
    )
  set(IMAGE_RESOURCE_FILE "${CMAKE_CURRENT_BINARY_DIR}/embedded_images.cpp")
  set(GENERATOR_SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/generate_images.cmake")

  # create a generator script that will be executed only when images change.
  # one write, as for the fonts
  file(
        WRITE ${GENERATOR_SCRIPT}
        "
file(GLOB IMAGE_FILES \"${dir}/*.png\" \"${dir}/*.jpg\" \"${dir}/*.jpeg\")
set(IMAGE_RESOURCE_FILE \"${IMAGE_RESOURCE_FILE}\")

set(OUT \"#include <cstdint>\\n#include <cstddef>\\n\\n\")
string(APPEND OUT \"struct EmbeddedImage {\\n\")
string(APPEND OUT \"    const char* name;\\n\")
string(APPEND OUT \"    const uint8_t* data;\\n\")
string(APPEND OUT \"    size_t size;\\n\")
string(APPEND OUT \"};\\n\\n\")

if(IMAGE_FILES)
    foreach(IMAGE_FILE \${IMAGE_FILES})
        get_filename_component(IMAGE_NAME \${IMAGE_FILE} NAME_WE)
        string(MAKE_C_IDENTIFIER \${IMAGE_NAME} IMAGE_VAR)

        file(READ \${IMAGE_FILE} IMAGE_DATA HEX)
        string(REGEX REPLACE \"([0-9a-fA-F][0-9a-fA-F])\" \"0x\\\\1,\" IMAGE_DATA \${IMAGE_DATA})
        string(REGEX REPLACE \",\$\" \"\" IMAGE_DATA \${IMAGE_DATA})

        string(APPEND OUT \"static const uint8_t \${IMAGE_VAR}_data[] = {\${IMAGE_DATA}};\\n\")
    endforeach()

    string(APPEND OUT \"\\nstatic const EmbeddedImage embedded_images[] = {\\n\")
    foreach(IMAGE_FILE \${IMAGE_FILES})
        get_filename_component(IMAGE_NAME \${IMAGE_FILE} NAME_WE)
        string(MAKE_C_IDENTIFIER \${IMAGE_NAME} IMAGE_VAR)
        string(APPEND OUT \"    {\\\"\${IMAGE_NAME}\\\", \${IMAGE_VAR}_data, sizeof(\${IMAGE_VAR}_data)},\\n\")
    endforeach()
    string(APPEND OUT \"};\\n\\n\")

    string(APPEND OUT \"extern \\\"C\\\" const EmbeddedImage* getEmbeddedImages() { return embedded_images; }\\n\")
    string(APPEND OUT \"extern \\\"C\\\" size_t getEmbeddedImageCount() { return sizeof(embedded_images) / sizeof(embedded_images[0]); }\\n\")
else()
    string(APPEND OUT \"extern \\\"C\\\" const EmbeddedImage* getEmbeddedImages() { return nullptr; }\\n\")
    string(APPEND OUT \"extern \\\"C\\\" size_t getEmbeddedImageCount() { return 0; }\\n\")
endif()

file(WRITE \${IMAGE_RESOURCE_FILE} \"\${OUT}\")
"
    )

  # add custom command that only runs when image files change
  add_custom_command(
        OUTPUT ${IMAGE_RESOURCE_FILE}
        COMMAND ${CMAKE_COMMAND} -P ${GENERATOR_SCRIPT}
        DEPENDS ${IMAGE_FILES}
        COMMENT "Embedding image resources"
        VERBATIM
    )

  # make the resource file available to parent scope
  set(EMBEDDED_IMAGES_FILE ${IMAGE_RESOURCE_FILE} PARENT_SCOPE)
endfunction()
