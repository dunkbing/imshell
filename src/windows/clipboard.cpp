// the clipboard on windows: text (CF_UNICODETEXT), html ("HTML Format"), rtf, pictures ("PNG",
// and CF_DIB both ways through wic), copied files (CF_HDROP, an item each) and any format
// registered under a mime type name. ReadText and WriteText stay on imgui's hooks (glfw)
#include "imshell/clipboard.hpp"

#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <wincodec.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>

namespace imshell::Clipboard {
    namespace {
        // another app may hold the clipboard for a moment
        bool open() {
            for (int i = 0; i < 10; ++i) {
                if (OpenClipboard(nullptr))
                    return true;
                Sleep(10);
            }
            return false;
        }

        std::string utf8(const wchar_t* w, int length = -1) {
            const int n = WideCharToMultiByte(CP_UTF8, 0, w, length, nullptr, 0, nullptr, nullptr);
            std::string out(n > 0 ? n : 0, '\0');
            if (n > 0)
                WideCharToMultiByte(CP_UTF8, 0, w, length, out.data(), n, nullptr, nullptr);
            if (length < 0 && !out.empty())
                out.pop_back(); // the terminator
            return out;
        }

        std::wstring wide(std::string_view s) {
            const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
            std::wstring out(n, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
            return out;
        }

        std::string bytesOf(UINT format) {
            HANDLE h = GetClipboardData(format);
            const void* p = h ? GlobalLock(h) : nullptr;
            if (!p)
                return {};
            std::string out(static_cast<const char*>(p), GlobalSize(h));
            GlobalUnlock(h);
            return out;
        }

        bool put(UINT format, const void* data, size_t size) {
            HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, size);
            if (!h)
                return false;
            std::memcpy(GlobalLock(h), data, size);
            GlobalUnlock(h);
            if (SetClipboardData(format, h))
                return true;
            GlobalFree(h);
            return false;
        }

        UINT htmlFormat() {
            static const UINT f = RegisterClipboardFormatW(L"HTML Format");
            return f;
        }
        UINT rtfFormat() {
            static const UINT f = RegisterClipboardFormatW(L"Rich Text Format");
            return f;
        }
        UINT pngFormat() {
            static const UINT f = RegisterClipboardFormatW(L"PNG");
            return f;
        }

        // "HTML Format" is the html after a header of byte offsets (StartHTML:0000000105 ...)
        std::string htmlFrom(const std::string& cf) {
            auto offset = [&](const char* key) -> size_t {
                const size_t at = cf.find(key);
                return at == cf.npos ? cf.npos : std::strtoul(cf.c_str() + at + std::strlen(key), nullptr, 10);
            };
            const size_t start = offset("StartHTML:"), end = offset("EndHTML:");
            if (start >= cf.size())
                return {};
            std::string html = cf.substr(start, end > start && end <= cf.size() ? end - start : cf.npos);
            return html.substr(0, html.find('\0'));
        }
        std::string htmlTo(const std::string& html) {
            const bool whole = html.find("<html") != html.npos || html.find("<HTML") != html.npos;
            const std::string before = whole ? "" : "<html><body><!--StartFragment-->";
            const std::string after = whole ? "" : "<!--EndFragment--></body></html>";
            const char* header = "Version:0.9\r\nStartHTML:%010zu\r\nEndHTML:%010zu\r\n"
                                 "StartFragment:%010zu\r\nEndFragment:%010zu\r\n";
            const size_t headerSize = 105; // the header above, with its numbers filled in
            const size_t startHtml = headerSize, endHtml = startHtml + before.size() + html.size() + after.size();
            const size_t startFragment = startHtml + before.size(), endFragment = startFragment + html.size();
            char head[128];
            std::snprintf(head, sizeof head, header, startHtml, endHtml, startFragment, endFragment);
            return head + before + html + after;
        }

        // a file:// url for a path (C:\a b.txt -> file:///C:/a%20b.txt), and back is Item::file
        std::string urlOf(const wchar_t* path) {
            wchar_t url[2084];
            DWORD n = ARRAYSIZE(url);
            return SUCCEEDED(UrlCreateFromPathW(path, url, &n, 0)) ? utf8(url) : std::string();
        }

        // pictures through wic, on a com apartment of their own when the thread has none
        struct Com {
            const bool started = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
            ~Com() {
                if (started)
                    CoUninitialize();
            }
        };

        IWICImagingFactory* wic() {
            IWICImagingFactory* factory = nullptr;
            CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
            return factory;
        }

        template <class T> void release(T*& p) {
            if (p)
                p->Release();
            p = nullptr;
        }

        // an encoded picture (bmp, png) decoded to 32-bit bgra, top row first
        bool decode(IWICImagingFactory* factory, const std::string& bytes, UINT& w, UINT& h, std::string& bgra) {
            IStream* stream = SHCreateMemStream(reinterpret_cast<const BYTE*>(bytes.data()), static_cast<UINT>(bytes.size()));
            IWICBitmapDecoder* decoder = nullptr;
            IWICBitmapFrameDecode* frame = nullptr;
            IWICFormatConverter* converter = nullptr;
            bool ok = stream &&
                      SUCCEEDED(factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, &decoder)) &&
                      SUCCEEDED(decoder->GetFrame(0, &frame)) && SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
                      SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone,
                                                      nullptr, 0, WICBitmapPaletteTypeCustom)) &&
                      SUCCEEDED(converter->GetSize(&w, &h));
            if (ok) {
                bgra.resize(static_cast<size_t>(w) * h * 4);
                ok = SUCCEEDED(converter->CopyPixels(nullptr, w * 4, static_cast<UINT>(bgra.size()),
                                                     reinterpret_cast<BYTE*>(bgra.data())));
            }
            release(converter);
            release(frame);
            release(decoder);
            release(stream);
            return ok;
        }

        // CF_DIB (a bitmap without its file header) as png
        std::string pngFromDib(const std::string& dib) {
            if (dib.size() < sizeof(BITMAPINFOHEADER))
                return {};
            Com com;
            IWICImagingFactory* factory = wic();
            if (!factory)
                return {};
            // a bmp file is the dib after a file header that says where the pixels start
            const auto* info = reinterpret_cast<const BITMAPINFOHEADER*>(dib.data());
            DWORD colours = info->biClrUsed ? info->biClrUsed : (info->biBitCount <= 8 ? 1u << info->biBitCount : 0);
            DWORD masks = info->biSize == sizeof(BITMAPINFOHEADER) && info->biCompression == BI_BITFIELDS ? 12 : 0;
            BITMAPFILEHEADER file{};
            file.bfType = 0x4D42; // "BM"
            file.bfSize = static_cast<DWORD>(sizeof file + dib.size());
            file.bfOffBits = static_cast<DWORD>(sizeof file + info->biSize + masks + colours * 4);
            std::string bmp(reinterpret_cast<const char*>(&file), sizeof file);
            bmp += dib;

            UINT w = 0, h = 0;
            std::string bgra, png;
            IStream* out = SHCreateMemStream(nullptr, 0);
            IWICBitmapEncoder* encoder = nullptr;
            IWICBitmapFrameEncode* frame = nullptr;
            WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
            if (decode(factory, bmp, w, h, bgra) && out &&
                SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) &&
                SUCCEEDED(encoder->Initialize(out, WICBitmapEncoderNoCache)) &&
                SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) && SUCCEEDED(frame->Initialize(nullptr)) &&
                SUCCEEDED(frame->SetSize(w, h)) && SUCCEEDED(frame->SetPixelFormat(&format)) &&
                SUCCEEDED(frame->WritePixels(h, w * 4, static_cast<UINT>(bgra.size()), reinterpret_cast<BYTE*>(bgra.data()))) &&
                SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit())) {
                STATSTG stat{};
                out->Stat(&stat, STATFLAG_NONAME);
                png.resize(static_cast<size_t>(stat.cbSize.QuadPart));
                const LARGE_INTEGER zero{};
                ULONG read = 0;
                out->Seek(zero, STREAM_SEEK_SET, nullptr);
                out->Read(png.data(), static_cast<ULONG>(png.size()), &read);
                png.resize(read);
            }
            release(frame);
            release(encoder);
            release(out);
            release(factory);
            return png;
        }

        // png as CF_DIBV5 (top-down 32-bit with alpha), what paint and office paste
        std::string dibFromPng(const std::string& png) {
            Com com;
            IWICImagingFactory* factory = wic();
            UINT w = 0, h = 0;
            std::string bgra;
            const bool ok = factory && decode(factory, png, w, h, bgra);
            release(factory);
            if (!ok)
                return {};
            BITMAPV5HEADER header{};
            header.bV5Size = sizeof header;
            header.bV5Width = static_cast<LONG>(w);
            header.bV5Height = -static_cast<LONG>(h); // top-down, as decoded
            header.bV5Planes = 1;
            header.bV5BitCount = 32;
            header.bV5Compression = BI_BITFIELDS;
            header.bV5SizeImage = static_cast<DWORD>(bgra.size());
            header.bV5RedMask = 0x00FF0000;
            header.bV5GreenMask = 0x0000FF00;
            header.bV5BlueMask = 0x000000FF;
            header.bV5AlphaMask = 0xFF000000;
            header.bV5CSType = LCS_sRGB;
            header.bV5Intent = LCS_GM_IMAGES;
            return std::string(reinterpret_cast<const char*>(&header), sizeof header) + bgra;
        }
    } // namespace

    std::vector<Item> Read() {
        if (!open())
            return {};
        Item item;
        std::vector<Item> files;
        for (UINT format = EnumClipboardFormats(0); format; format = EnumClipboardFormats(format)) {
            if (format == CF_UNICODETEXT) {
                const std::string bytes = bytesOf(format);
                const std::wstring text(reinterpret_cast<const wchar_t*>(bytes.data()), bytes.size() / sizeof(wchar_t));
                item.data["text/plain"] = utf8(text.c_str()); // up to its terminator
            } else if (format == CF_HDROP) {
                if (HANDLE h = GetClipboardData(CF_HDROP)) {
                    auto drop = static_cast<HDROP>(h);
                    const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
                    for (UINT i = 0; i < count; ++i) {
                        std::wstring path(DragQueryFileW(drop, i, nullptr, 0) + 1, L'\0');
                        DragQueryFileW(drop, i, path.data(), static_cast<UINT>(path.size()));
                        files.push_back(Item{{{"text/uri-list", urlOf(path.c_str())}}});
                    }
                }
            } else if (format == htmlFormat()) {
                item.data["text/html"] = htmlFrom(bytesOf(format));
            } else if (format == rtfFormat()) {
                item.data["text/rtf"] = bytesOf(format);
            } else if (format == pngFormat()) {
                item.data["image/png"] = bytesOf(format);
            } else if (format >= 0xC000) { // registered: kept when its name is a mime type
                wchar_t name[256];
                const int n = GetClipboardFormatNameW(format, name, ARRAYSIZE(name));
                const std::string mime = n > 0 ? utf8(name, n) : "";
                if (mime.find('/') != mime.npos && !item.has(mime))
                    item.data[mime] = bytesOf(format);
            }
        }
        // a picture is always offered as png (most apps copy a bitmap only)
        if (!item.has("image/png") && IsClipboardFormatAvailable(CF_DIB)) {
            const std::string png = pngFromDib(bytesOf(CF_DIB));
            if (!png.empty())
                item.data["image/png"] = png;
        }
        CloseClipboard();
        if (files.size() > 0)
            return files;
        return item.data.empty() ? std::vector<Item>{} : std::vector<Item>{item};
    }

    void Write(const std::vector<Item>& items) {
        if (!open())
            return;
        EmptyClipboard();
        // one set of formats: files from every item become one drop list, the rest comes from
        // the first item
        std::wstring paths;
        for (const Item& item : items) {
            const std::string path = item.file();
            if (!path.empty())
                paths += wide(path) + L'\0';
        }
        if (!paths.empty()) {
            paths += L'\0';
            std::string drop(sizeof(DROPFILES), '\0');
            auto* header = reinterpret_cast<DROPFILES*>(drop.data());
            header->pFiles = sizeof(DROPFILES);
            header->fWide = TRUE;
            drop.append(reinterpret_cast<const char*>(paths.data()), paths.size() * sizeof(wchar_t));
            put(CF_HDROP, drop.data(), drop.size());
        }
        if (!items.empty()) {
            for (const auto& [mime, bytes] : items[0].data) {
                if (mime == "text/plain") {
                    const std::wstring text = wide(bytes);
                    put(CF_UNICODETEXT, text.c_str(), (text.size() + 1) * sizeof(wchar_t));
                } else if (mime == "text/html") {
                    const std::string cf = htmlTo(bytes);
                    put(htmlFormat(), cf.c_str(), cf.size() + 1);
                } else if (mime == "text/rtf") {
                    put(rtfFormat(), bytes.data(), bytes.size());
                } else if (mime == "image/png") {
                    put(pngFormat(), bytes.data(), bytes.size());
                    const std::string dib = dibFromPng(bytes);
                    if (!dib.empty())
                        put(CF_DIBV5, dib.data(), dib.size());
                } else if (mime != "text/uri-list") {
                    put(RegisterClipboardFormatW(wide(mime).c_str()), bytes.data(), bytes.size());
                }
            }
        }
        CloseClipboard();
    }
} // namespace imshell::Clipboard
