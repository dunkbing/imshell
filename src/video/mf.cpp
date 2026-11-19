#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "mf.hpp"

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <windows.h>

namespace imshell::video {
    using Microsoft::WRL::ComPtr;

    namespace {
        constexpr LONGLONG kUnits = 10000000; // media foundation time: 100 ns
        constexpr UINT32 kRate = 48000;

        bool failed(std::string* error, const char* what, HRESULT hr) {
            if (error) {
                char code[16];
                std::snprintf(code, sizeof code, "0x%08lx", static_cast<unsigned long>(hr));
                *error = std::string(what) + " (" + code + ")";
            }
            return false;
        }

        std::wstring widen(const std::string& s) {
            if (s.empty()) {
                return {};
            }
            const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
            std::wstring w(static_cast<size_t>(n), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
            return w;
        }

        // media foundation up for the lifetime of one user
        struct Runtime {
            bool ok = false;
            bool com = false;
            Runtime() {
                const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
                com = SUCCEEDED(hr); // RPC_E_CHANGED_MODE: the thread already has COM, use it as it is
                ok = SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_LITE));
            }
            ~Runtime() {
                if (ok) {
                    MFShutdown();
                }
                if (com) {
                    CoUninitialize();
                }
            }
        };
    } // namespace

    struct MfWriter::Impl {
        Runtime runtime;
        ComPtr<IMFSinkWriter> writer;
        DWORD videoStream = 0, audioStream = 0;
        int width = 0, height = 0, fps = 1;
        bool withAudio = false;
    };

    MfWriter::MfWriter() : impl_(std::make_unique<Impl>()) {}
    MfWriter::~MfWriter() = default;

    bool MfWriter::open(const std::string& path, int width, int height, int fps, bool withAudio, std::string* error) {
        Impl& m = *impl_;
        if (!m.runtime.ok) {
            return failed(error, "Media Foundation did not start", E_FAIL);
        }
        m.width = width;
        m.height = height;
        m.fps = fps;
        m.withAudio = withAudio;
        DeleteFileW(widen(path).c_str());

        ComPtr<IMFAttributes> attributes;
        MFCreateAttributes(&attributes, 1);
        attributes->SetUINT32(MF_READWRITE_ENABLE_VIDEO_PROCESSING, TRUE); // rgb in, the encoder's yuv out
        HRESULT hr = MFCreateSinkWriterFromURL(widen(path).c_str(), nullptr, attributes.Get(), &m.writer);
        if (FAILED(hr)) {
            return failed(error, "could not create the mp4 writer", hr);
        }

        ComPtr<IMFMediaType> out;
        MFCreateMediaType(&out);
        out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        out->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
        out->SetUINT32(MF_MT_AVG_BITRATE, static_cast<UINT32>(static_cast<double>(width) * height * fps * 0.15));
        out->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        MFSetAttributeSize(out.Get(), MF_MT_FRAME_SIZE, width, height);
        MFSetAttributeRatio(out.Get(), MF_MT_FRAME_RATE, fps, 1);
        MFSetAttributeRatio(out.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        hr = m.writer->AddStream(out.Get(), &m.videoStream);
        if (FAILED(hr)) {
            return failed(error, "no h.264 encoder", hr);
        }
        ComPtr<IMFMediaType> in;
        MFCreateMediaType(&in);
        in->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        in->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        in->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        in->SetUINT32(MF_MT_DEFAULT_STRIDE, static_cast<UINT32>(width * 4)); // positive: top-down
        MFSetAttributeSize(in.Get(), MF_MT_FRAME_SIZE, width, height);
        MFSetAttributeRatio(in.Get(), MF_MT_FRAME_RATE, fps, 1);
        MFSetAttributeRatio(in.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        hr = m.writer->SetInputMediaType(m.videoStream, in.Get(), nullptr);
        if (FAILED(hr)) {
            return failed(error, "the h.264 encoder does not take this size", hr);
        }

        if (withAudio) {
            ComPtr<IMFMediaType> aacOut;
            MFCreateMediaType(&aacOut);
            aacOut->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
            aacOut->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
            aacOut->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
            aacOut->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, kRate);
            aacOut->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
            aacOut->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 20000); // 160 kbit/s
            aacOut->SetUINT32(MF_MT_AAC_PAYLOAD_TYPE, 0);
            aacOut->SetUINT32(MF_MT_AAC_AUDIO_PROFILE_LEVEL_INDICATION, 0x29);
            hr = m.writer->AddStream(aacOut.Get(), &m.audioStream);
            if (FAILED(hr)) {
                return failed(error, "no aac encoder", hr);
            }
            ComPtr<IMFMediaType> pcm;
            MFCreateMediaType(&pcm);
            pcm->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
            pcm->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float);
            pcm->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 32);
            pcm->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, kRate);
            pcm->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
            pcm->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 8);
            pcm->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, kRate * 8);
            hr = m.writer->SetInputMediaType(m.audioStream, pcm.Get(), nullptr);
            if (FAILED(hr)) {
                return failed(error, "the aac encoder does not take float stereo", hr);
            }
        }
        hr = m.writer->BeginWriting();
        return SUCCEEDED(hr) || failed(error, "the mp4 writer would not start", hr);
    }

    namespace {
        // a sample holding `size` bytes that `fill` writes, at `time` for `duration`
        template <typename Fill>
        bool writeSample(IMFSinkWriter* writer, DWORD stream, DWORD size, LONGLONG time, LONGLONG duration, Fill fill) {
            ComPtr<IMFMediaBuffer> buffer;
            if (FAILED(MFCreateMemoryBuffer(size, &buffer))) {
                return false;
            }
            BYTE* data = nullptr;
            if (FAILED(buffer->Lock(&data, nullptr, nullptr))) {
                return false;
            }
            fill(data);
            buffer->Unlock();
            buffer->SetCurrentLength(size);
            ComPtr<IMFSample> sample;
            if (FAILED(MFCreateSample(&sample)) || FAILED(sample->AddBuffer(buffer.Get()))) {
                return false;
            }
            sample->SetSampleTime(time);
            sample->SetSampleDuration(duration);
            return SUCCEEDED(writer->WriteSample(stream, sample.Get()));
        }
    } // namespace

    bool MfWriter::video(const unsigned char* rgba, int index) {
        Impl& m = *impl_;
        const size_t pixels = static_cast<size_t>(m.width) * m.height;
        return writeSample(m.writer.Get(), m.videoStream, static_cast<DWORD>(pixels * 4), index * kUnits / m.fps, kUnits / m.fps, [&](BYTE* out) {
            for (size_t i = 0; i < pixels; ++i) { // rgba to the bgrx of RGB32
                out[i * 4 + 0] = rgba[i * 4 + 2];
                out[i * 4 + 1] = rgba[i * 4 + 1];
                out[i * 4 + 2] = rgba[i * 4 + 0];
                out[i * 4 + 3] = 255;
            }
        });
    }

    bool MfWriter::audio(const float* interleaved, size_t start, size_t frames) {
        Impl& m = *impl_;
        if (!m.withAudio || frames == 0) {
            return true;
        }
        return writeSample(m.writer.Get(), m.audioStream, static_cast<DWORD>(frames * 8), static_cast<LONGLONG>(start) * kUnits / kRate,
                           static_cast<LONGLONG>(frames) * kUnits / kRate, [&](BYTE* out) { std::memcpy(out, interleaved, frames * 8); });
    }

    bool MfWriter::finish(std::string* error) {
        const HRESULT hr = impl_->writer->Finalize();
        impl_->writer.Reset();
        return SUCCEEDED(hr) || failed(error, "the mp4 could not be finished", hr);
    }

    bool DecodeWithMf(const std::string& path, std::vector<float>& pcm, std::string* error) {
        Runtime runtime;
        if (!runtime.ok) {
            return failed(error, "Media Foundation did not start", E_FAIL);
        }
        ComPtr<IMFSourceReader> reader;
        HRESULT hr = MFCreateSourceReaderFromURL(widen(path).c_str(), nullptr, &reader);
        if (FAILED(hr)) {
            return failed(error, "could not open the sound file", hr);
        }
        reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
        reader->SetStreamSelection(MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE);
        ComPtr<IMFMediaType> want;
        MFCreateMediaType(&want);
        want->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        want->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float);
        want->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 32);
        want->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, kRate);
        want->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
        want->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 8);
        want->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, kRate * 8);
        hr = reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, want.Get()); // the reader adds the decoder and resampler
        if (FAILED(hr)) {
            return failed(error, "no decoder for this sound file", hr);
        }
        pcm.clear();
        while (true) {
            DWORD flags = 0;
            ComPtr<IMFSample> sample;
            hr = reader->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, nullptr, &flags, nullptr, &sample);
            if (FAILED(hr)) {
                return failed(error, "the sound file could not be read", hr);
            }
            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
                break;
            }
            if (!sample) {
                continue;
            }
            ComPtr<IMFMediaBuffer> buffer;
            sample->ConvertToContiguousBuffer(&buffer);
            BYTE* data = nullptr;
            DWORD length = 0;
            if (buffer && SUCCEEDED(buffer->Lock(&data, nullptr, &length))) {
                const float* f = reinterpret_cast<const float*>(data);
                pcm.insert(pcm.end(), f, f + length / sizeof(float));
                buffer->Unlock();
            }
        }
        pcm.resize(pcm.size() / 2 * 2);
        return pcm.size() >= 2 || failed(error, "the sound file has no sound", E_FAIL);
    }
} // namespace imshell::video
#endif
