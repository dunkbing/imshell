#include "frame_skip.hpp"

#include <cstring>
#include <vector>

namespace mydear::detail {
    namespace {
        std::vector<char> last, current;

        template <class T> void put(const T& value) {
            const auto* p = reinterpret_cast<const char*>(&value);
            current.insert(current.end(), p, p + sizeof value);
        }
        void putBytes(const void* data, size_t size) {
            const auto* p = static_cast<const char*>(data);
            current.insert(current.end(), p, p + size);
        }
    } // namespace

    bool& frameSkipped() {
        static bool skipped = false;
        return skipped;
    }

    // an exact copy of the last frame rather than a hash: memcmp is as fast and never collides
    bool sameAsLastFrame(const ImDrawData* d, const ImVec4 clear) {
        current.clear();
        bool drawable = d && d->Valid;
        if (drawable && d->Textures)
            for (const ImTextureData* t : *d->Textures)
                drawable &= t->Status == ImTextureStatus_OK; // an upload rides on this frame
        if (drawable) {
            put(d->DisplayPos);
            put(d->DisplaySize);
            put(d->FramebufferScale);
            put(clear);
            put(d->CmdListsCount);
            for (const ImDrawList* list : d->CmdLists) {
                put(list->CmdBuffer.Size);
                // field by field: struct padding is not guaranteed to match
                for (const ImDrawCmd& cmd : list->CmdBuffer) {
                    drawable &= cmd.UserCallback == nullptr;
                    put(cmd.ClipRect);
                    put(cmd.TexRef._TexData);
                    put(cmd.TexRef._TexID);
                    put(cmd.VtxOffset);
                    put(cmd.IdxOffset);
                    put(cmd.ElemCount);
                }
                put(list->VtxBuffer.Size);
                putBytes(list->VtxBuffer.Data, list->VtxBuffer.size_in_bytes());
                put(list->IdxBuffer.Size);
                putBytes(list->IdxBuffer.Data, list->IdxBuffer.size_in_bytes());
            }
        }
        if (!drawable) {
            last.clear(); // the next frame draws too
            return false;
        }
        const bool same = current == last;
        last.swap(current);
        return same;
    }
} // namespace mydear::detail
