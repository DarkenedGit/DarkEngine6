#include "Media/MovieSource.h"

#include "Core/Log.h"
#include "Media/MovieAudioPack.h"
#include "Media/MovieFrameCopy.h"
#include "Media/MoviePath.h"

#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#    define NOMINMAX
#endif
#include <Windows.h>
#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

#include <chrono>
#include <cstring>
#include <mutex>
#include <utility>

namespace Dark
{

    using Microsoft::WRL::ComPtr;

    namespace
    {

        constexpr uint32_t kMaxH264Level = 52;
        constexpr uint32_t kPoolSurfaces = 3;

        std::mutex g_mfMutex;
        int        g_mfCount = 0;

        MovieStatus statusFromHr(HRESULT hr)
        {
            if (hr == MF_E_UNSUPPORTED_BYTESTREAM_TYPE || hr == MF_E_INVALID_FILE_FORMAT)
                return MovieStatus::UnsupportedContainer;
            if (hr == MF_E_DRM_UNSUPPORTED || hr == MF_E_LICENSE_INCORRECT_RIGHTS || hr == MF_E_LICENSE_OUTOFDATE || hr == MF_E_LICENSE_REQUIRED)
                return MovieStatus::UnsupportedCodec;
            return MovieStatus::DecodeError;
        }

        void logHr(const char* what, HRESULT hr)
        {
            DE_LOG_ERROR(LogCategory::Media, "Movie: {} failed (HRESULT 0x{:08X})", what, static_cast<unsigned>(hr));
        }

        bool sameGuid(const GUID& a, const GUID& b)
        {
            return IsEqualGUID(a, b) == TRUE;
        }

        bool h264ProfileAllowed(uint32_t profile)
        {
            // Constrained Baseline is 256. Baseline 66 is accepted as constrained baseline. Main 77. High 100.
            return profile == 66u || profile == 77u || profile == 100u || profile == 256u;
        }

        int offsetToInt(const MFOffset& offset)
        {
            int value = offset.value;
            if (offset.fract >= 32768 && value < 32767)
                ++value;
            return value < 0 ? 0 : value;
        }

        bool readArea(IMFAttributes* type, const GUID& key, MFVideoArea& area)
        {
            if (!type)
                return false;
            UINT32 bytes = 0;
            const HRESULT hr = type->GetBlob(key, reinterpret_cast<UINT8*>(&area), sizeof(area), &bytes);
            return SUCCEEDED(hr) && bytes >= sizeof(area);
        }

        void flip180(std::vector<uint8_t>& bgra, uint32_t width, uint32_t height)
        {
            if (width == 0 || height == 0 || bgra.size() < static_cast<size_t>(width) * height * 4u)
                return;

            const uint32_t rowBytes = width * 4u;
            std::vector<uint8_t> tmp(rowBytes);
            for (uint32_t y = 0; y < height / 2u; ++y)
            {
                uint8_t* top = bgra.data() + static_cast<size_t>(y) * rowBytes;
                uint8_t* bot = bgra.data() + static_cast<size_t>(height - 1u - y) * rowBytes;
                std::memcpy(tmp.data(), top, rowBytes);
                std::memcpy(top, bot, rowBytes);
                std::memcpy(bot, tmp.data(), rowBytes);
            }
            for (uint32_t y = 0; y < height; ++y)
            {
                uint8_t* row = bgra.data() + static_cast<size_t>(y) * rowBytes;
                for (uint32_t x = 0; x < width / 2u; ++x)
                {
                    uint8_t* a = row + static_cast<size_t>(x) * 4u;
                    uint8_t* b = row + static_cast<size_t>(width - 1u - x) * 4u;
                    uint8_t  px[4];
                    std::memcpy(px, a, 4);
                    std::memcpy(a, b, 4);
                    std::memcpy(b, px, 4);
                }
            }
        }

        bool mpeg2HardwareMft()
        {
            MFT_REGISTER_TYPE_INFO inInfo{};
            inInfo.guidMajorType = MFMediaType_Video;
            inInfo.guidSubtype   = MFVideoFormat_MPEG2;
            MFT_REGISTER_TYPE_INFO outInfo{};
            outInfo.guidMajorType = MFMediaType_Video;
            outInfo.guidSubtype   = MFVideoFormat_NV12;

            IMFActivate** activates = nullptr;
            UINT32        count     = 0;
            const HRESULT hr        = MFTEnumEx(MFT_CATEGORY_VIDEO_DECODER,
                MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_SORTANDFILTER | MFT_ENUM_FLAG_LOCALMFT, &inInfo, &outInfo, &activates, &count);
            if (activates)
            {
                for (UINT32 i = 0; i < count; ++i)
                {
                    if (activates[i])
                        activates[i]->Release();
                }
                CoTaskMemFree(activates);
            }
            return SUCCEEDED(hr) && count > 0;
        }

    } // namespace

    struct MovieSource::Impl
    {
        using Clock = std::chrono::steady_clock;

        ComPtr<ID3D11Device>        device11;
        ComPtr<ID3D11DeviceContext> context11;
        ComPtr<ID3D11Device5>       device5;
        ComPtr<ID3D11DeviceContext4> context4;
        ComPtr<IMFDXGIDeviceManager> manager;
        UINT                        resetToken = 0;
        ComPtr<ID3D12Device>        device12;

        ComPtr<IMFSourceReader> reader;
        ComPtr<IMFMediaType>    nativeVideo;
        ComPtr<ID3D11Texture2D> pool11[kPoolSurfaces];
        ComPtr<ID3D12Resource>  nv12[kPoolSurfaces];
        ComPtr<ID3D12Fence>     fence12;
        ComPtr<ID3D11Fence>     fence11;
        HANDLE                  sharedHandle[kPoolSurfaces]{};
        HANDLE                  fenceHandle = nullptr;

        MovieSourceInfo info{};
        DWORD           videoStream = 0;
        DWORD           audioStream = 0;
        DWORD           streamCount = 0;
        bool            hasVideo    = false;
        bool            hasAudio    = false;
        bool            mpeg2       = false;
        bool            videoEnded  = false;
        bool            audioEnded  = false;
        bool            hardware    = false;
        bool            cpuFlip180   = false;
        bool            audioFloat   = false;
        bool            decodeSeeded = false;
        bool            rotationWarned = false;

        uint32_t codedW      = 0;
        uint32_t codedH      = 0;
        uint32_t cropLeft    = 0;
        uint32_t cropTop     = 0;
        uint32_t audioBits   = 16;
        uint32_t packetFrames = kMovieAudioPacketFrames;
        uint32_t nativeAudioRate     = 0;
        uint32_t nativeAudioChannels = 0;
        float    decodeMs            = 0.f;
        uint64_t nextFence           = 0;

        std::wstring         openedPath;
        std::vector<int16_t> remainder;

        void close();
        bool tryHardware(const MovieSourceRequest& request);
        MovieStatus createReader(bool hardwareReader);
        bool protectedContent() const;
        MovieStatus selectStreams();
        MovieStatus configureVideo();
        void configureAudio();
        bool tryOutput(const GUID& subtype, bool nominalFull);
        bool readPictureFormat(IMFMediaType* type);
        bool apertureEven() const;
        bool createPool();
        void releasePool();
        void releaseDevice();
        void noteDecode(float ms, MovieRead& out);
        bool packSoftware(const uint8_t* data, uint32_t bytes, int32_t stride, std::vector<uint8_t>& dst) const;
        bool copyHardware(IMFSample* sample, uint32_t surface, uint64_t& fenceValue);
        void readStream(bool video, uint32_t surface, MovieRead& out);
    };

    void MovieSource::Impl::close()
    {
        releasePool();
        releaseDevice();
        reader.Reset();
        nativeVideo.Reset();
        info       = {};
        hasVideo   = false;
        hasAudio   = false;
        mpeg2      = false;
        videoEnded = false;
        audioEnded = false;
        hardware   = false;
        cpuFlip180     = false;
        audioFloat     = false;
        decodeSeeded   = false;
        rotationWarned = false;
        codedW = codedH = cropLeft = cropTop = 0;
        audioBits            = 16;
        packetFrames         = kMovieAudioPacketFrames;
        nativeAudioRate      = 0;
        nativeAudioChannels  = 0;
        decodeMs             = 0.f;
        nextFence            = 0;
        videoStream          = 0;
        audioStream          = 0;
        streamCount          = 0;
        openedPath.clear();
        remainder.clear();
    }

    void MovieSource::Impl::releaseDevice()
    {
        manager.Reset();
        context4.Reset();
        context11.Reset();
        device5.Reset();
        device11.Reset();
        device12.Reset();
        resetToken = 0;
    }

    void MovieSource::Impl::releasePool()
    {
        fence11.Reset();
        fence12.Reset();
        for (uint32_t i = 0; i < kPoolSurfaces; ++i)
        {
            pool11[i].Reset();
            nv12[i].Reset();
            if (sharedHandle[i])
            {
                CloseHandle(sharedHandle[i]);
                sharedHandle[i] = nullptr;
            }
        }
        if (fenceHandle)
        {
            CloseHandle(fenceHandle);
            fenceHandle = nullptr;
        }
        nextFence = 0;
    }

    bool MovieSource::Impl::tryHardware(const MovieSourceRequest& request)
    {
        releaseDevice();
        if (!request.device || (request.adapterLow == 0 && request.adapterHigh == 0))
            return false;

        ComPtr<IDXGIFactory1> factory1;
        HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory1));
        if (FAILED(hr))
            return false;

        ComPtr<IDXGIFactory4> factory4;
        hr = factory1.As(&factory4);
        if (FAILED(hr))
            return false;

        LUID luid{};
        luid.LowPart  = static_cast<DWORD>(request.adapterLow);
        luid.HighPart = static_cast<LONG>(request.adapterHigh);

        ComPtr<IDXGIAdapter1> adapter;
        hr = factory4->EnumAdapterByLuid(luid, IID_PPV_ARGS(&adapter));
        if (FAILED(hr))
            return false;

        const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
        const UINT              flags    = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
        D3D_FEATURE_LEVEL       got      = D3D_FEATURE_LEVEL_11_0;
        hr = D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags, levels, 2, D3D11_SDK_VERSION, &device11, &got, &context11);
        if (hr == E_INVALIDARG)
            hr = D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags, &levels[1], 1, D3D11_SDK_VERSION, &device11, &got, &context11);
        if (FAILED(hr) || !device11 || got < D3D_FEATURE_LEVEL_11_0)
        {
            releaseDevice();
            return false;
        }

        if (FAILED(device11.As(&device5)) || FAILED(context11.As(&context4)))
        {
            releaseDevice();
            return false;
        }

        ComPtr<ID3D11Multithread> multithread;
        if (SUCCEEDED(device11->QueryInterface(IID_PPV_ARGS(&multithread))) && multithread)
            multithread->SetMultithreadProtected(TRUE);

        hr = MFCreateDXGIDeviceManager(&resetToken, &manager);
        if (FAILED(hr) || FAILED(manager->ResetDevice(device11.Get(), resetToken)))
        {
            releaseDevice();
            return false;
        }

        device12 = static_cast<ID3D12Device*>(request.device);
        return true;
    }

    MovieStatus MovieSource::Impl::createReader(bool hardwareReader)
    {
        reader.Reset();
        if (openedPath.empty())
        {
            DE_LOG_ERROR(LogCategory::Media, "Movie: empty path");
            return MovieStatus::DecodeError;
        }

        ComPtr<IMFAttributes> attr;
        HRESULT hr = MFCreateAttributes(&attr, 4);
        if (FAILED(hr))
        {
            logHr("MFCreateAttributes", hr);
            return statusFromHr(hr);
        }

        if (hardwareReader && manager)
        {
            hr = attr->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
            if (SUCCEEDED(hr))
                hr = attr->SetUnknown(MF_SOURCE_READER_D3D_MANAGER, manager.Get());
        }
        else
        {
            hr = attr->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, FALSE);
            if (SUCCEEDED(hr))
                hr = attr->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
        }
        if (FAILED(hr))
        {
            logHr("reader attributes", hr);
            return statusFromHr(hr);
        }

        hr = MFCreateSourceReaderFromURL(openedPath.c_str(), attr.Get(), &reader);
        if (FAILED(hr))
        {
            logHr("MFCreateSourceReaderFromURL", hr);
            return statusFromHr(hr);
        }
        return MovieStatus::Ok;
    }

    bool MovieSource::Impl::protectedContent() const
    {
        if (!reader)
            return false;

        ComPtr<IMFMediaSource> source;
        HRESULT hr = reader->GetServiceForStream(static_cast<DWORD>(MF_SOURCE_READER_MEDIASOURCE), GUID_NULL, IID_PPV_ARGS(&source));
        if (FAILED(hr) || !source)
            return false;

        ComPtr<IMFPresentationDescriptor> pd;
        hr = source->CreatePresentationDescriptor(&pd);
        if (FAILED(hr) || !pd)
            return false;

        UINT32 prot = 0;
        if (SUCCEEDED(pd->GetUINT32(MF_SD_PROTECTED, &prot)) && prot != 0)
            return true;

        DWORD count = 0;
        if (FAILED(pd->GetStreamDescriptorCount(&count)))
            return false;
        for (DWORD i = 0; i < count; ++i)
        {
            BOOL selected = FALSE;
            ComPtr<IMFStreamDescriptor> sd;
            if (FAILED(pd->GetStreamDescriptorByIndex(i, &selected, &sd)) || !sd)
                continue;
            UINT32 streamProt = 0;
            if (SUCCEEDED(sd->GetUINT32(MF_SD_PROTECTED, &streamProt)) && streamProt != 0)
                return true;
        }
        return false;
    }

    MovieStatus MovieSource::Impl::selectStreams()
    {
        hasVideo             = false;
        hasAudio             = false;
        mpeg2                = false;
        nativeVideo.Reset();
        nativeAudioRate     = 0;
        nativeAudioChannels = 0;
        streamCount          = 0;

        bool     rejectedAudio = false;
        uint32_t videoCodedW   = 0;
        uint32_t videoCodedH   = 0;

        for (DWORD index = 0;; ++index)
        {
            ComPtr<IMFMediaType> native;
            const HRESULT hr = reader->GetNativeMediaType(index, 0, &native);
            if (hr == MF_E_INVALIDSTREAMNUMBER)
            {
                streamCount = index;
                break;
            }
            if (FAILED(hr))
            {
                logHr("GetNativeMediaType", hr);
                return statusFromHr(hr);
            }

            GUID major = GUID_NULL;
            if (FAILED(native->GetGUID(MF_MT_MAJOR_TYPE, &major)))
                continue;

            if (sameGuid(major, MFMediaType_Video) && !hasVideo)
            {
                GUID sub = GUID_NULL;
                if (FAILED(native->GetGUID(MF_MT_SUBTYPE, &sub)))
                    continue;
                if (!sameGuid(sub, MFVideoFormat_H264) && !sameGuid(sub, MFVideoFormat_MPEG2))
                    continue;

                UINT32 w = 0;
                UINT32 h = 0;
                if (FAILED(MFGetAttributeSize(native.Get(), MF_MT_FRAME_SIZE, &w, &h)) || !movieCodedSizeAllowed(w, h))
                {
                    DE_LOG_ERROR(LogCategory::Media, "Movie: resolution {}x{} is not supported", w, h);
                    return MovieStatus::ResolutionUnsupported;
                }

                if (sameGuid(sub, MFVideoFormat_H264))
                {
                    UINT32 profile = 0;
                    if (SUCCEEDED(native->GetUINT32(MF_MT_MPEG2_PROFILE, &profile)) && !h264ProfileAllowed(profile))
                    {
                        DE_LOG_ERROR(LogCategory::Media, "Movie: H.264 profile {} is not supported", profile);
                        return MovieStatus::UnsupportedCodec;
                    }
                    UINT32 level = 0;
                    if (SUCCEEDED(native->GetUINT32(MF_MT_MPEG2_LEVEL, &level)) && level > kMaxH264Level)
                    {
                        DE_LOG_ERROR(LogCategory::Media, "Movie: H.264 level {} is not supported", level);
                        return MovieStatus::UnsupportedCodec;
                    }
                    info.codec = "h264";
                    mpeg2      = false;
                }
                else
                {
                    if (!device11 || !mpeg2HardwareMft())
                    {
                        DE_LOG_ERROR(LogCategory::Media, "Movie: MPEG-2 hardware decoder is not available");
                        return MovieStatus::UnsupportedCodec;
                    }
                    info.codec = "mpeg2";
                    mpeg2      = true;
                }

                videoStream = index;
                hasVideo    = true;
                videoCodedW = w;
                videoCodedH = h;
                nativeVideo = native;
            }
            else if (sameGuid(major, MFMediaType_Audio) && !hasAudio)
            {
                GUID sub = GUID_NULL;
                native->GetGUID(MF_MT_SUBTYPE, &sub);
                const bool known = sameGuid(sub, MFAudioFormat_AAC) || sameGuid(sub, MFAudioFormat_ADTS) || sameGuid(sub, MFAudioFormat_MP3) || sameGuid(sub, MFAudioFormat_MPEG)
                    || sameGuid(sub, MFAudioFormat_PCM) || sameGuid(sub, MFAudioFormat_Float);
                UINT32 channels = 0;
                native->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels);
                if (!known || (channels != 1 && channels != 2 && channels != 6))
                {
                    rejectedAudio = true;
                    continue;
                }
                UINT32 rate = 0;
                native->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate);
                audioStream          = index;
                hasAudio             = true;
                nativeAudioChannels  = channels;
                nativeAudioRate      = rate;
            }
        }

        if (!hasVideo)
        {
            DE_LOG_ERROR(LogCategory::Media, "Movie: unsupported video subtype");
            return MovieStatus::UnsupportedCodec;
        }

        codedW = videoCodedW;
        codedH = videoCodedH;

        for (DWORD index = 0; index < streamCount; ++index)
            reader->SetStreamSelection(index, FALSE);
        if (FAILED(reader->SetStreamSelection(videoStream, TRUE)))
        {
            DE_LOG_ERROR(LogCategory::Media, "Movie: video stream selection failed");
            return MovieStatus::DecodeError;
        }
        if (hasAudio)
        {
            if (FAILED(reader->SetStreamSelection(audioStream, TRUE)))
            {
                hasAudio = false;
                DE_LOG_WARN(LogCategory::Media, "Movie: audio track dropped");
            }
        }
        else if (rejectedAudio)
        {
            DE_LOG_WARN(LogCategory::Media, "Movie: audio track dropped");
        }
        return MovieStatus::Ok;
    }

    bool MovieSource::Impl::tryOutput(const GUID& subtype, bool nominalFull)
    {
        ComPtr<IMFMediaType> outType;
        HRESULT hr = MFCreateMediaType(&outType);
        if (FAILED(hr))
            return false;

        outType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        outType->SetGUID(MF_MT_SUBTYPE, subtype);
        outType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        if (FAILED(MFSetAttributeSize(outType.Get(), MF_MT_FRAME_SIZE, codedW, codedH)))
            return false;

        UINT32 num = 0;
        UINT32 den = 0;
        if (nativeVideo && SUCCEEDED(MFGetAttributeRatio(nativeVideo.Get(), MF_MT_FRAME_RATE, &num, &den)) && num != 0 && den != 0)
            MFSetAttributeRatio(outType.Get(), MF_MT_FRAME_RATE, num, den);
        if (nominalFull)
            outType->SetUINT32(MF_MT_VIDEO_NOMINAL_RANGE, static_cast<UINT32>(MFNominalRange_0_255));

        hr = reader->SetCurrentMediaType(videoStream, nullptr, outType.Get());
        if (FAILED(hr))
            return false;

        ComPtr<IMFMediaType> current;
        if (FAILED(reader->GetCurrentMediaType(videoStream, &current)) || !current)
            return false;
        UINT32 w = 0;
        UINT32 h = 0;
        if (FAILED(MFGetAttributeSize(current.Get(), MF_MT_FRAME_SIZE, &w, &h)) || w != codedW || h != codedH)
            return false;
        return true;
    }

    bool MovieSource::Impl::readPictureFormat(IMFMediaType* type)
    {
        IMFMediaType* native = nativeVideo.Get();
        cropLeft             = 0;
        cropTop              = 0;
        info.width           = codedW;
        info.height          = codedH;

        MFVideoArea area{};
        IMFMediaType* areaType = type;
        if (!readArea(type, MF_MT_MINIMUM_DISPLAY_APERTURE, area) && !readArea(type, MF_MT_GEOMETRIC_APERTURE, area))
        {
            areaType = native;
            if (!readArea(native, MF_MT_MINIMUM_DISPLAY_APERTURE, area) && !readArea(native, MF_MT_GEOMETRIC_APERTURE, area))
                areaType = nullptr;
        }
        if (areaType)
        {
            const uint32_t w = area.Area.cx > 0 ? static_cast<uint32_t>(area.Area.cx) : 0;
            const uint32_t h = area.Area.cy > 0 ? static_cast<uint32_t>(area.Area.cy) : 0;
            const uint32_t l = static_cast<uint32_t>(offsetToInt(area.OffsetX));
            const uint32_t t = static_cast<uint32_t>(offsetToInt(area.OffsetY));
            if (w > 0 && h > 0 && l < codedW && t < codedH && w <= codedW - l && h <= codedH - t)
            {
                cropLeft    = l;
                cropTop     = t;
                info.width  = w;
                info.height = h;
            }
        }

        UINT32 range    = 0;
        bool   haveRange = type && SUCCEEDED(type->GetUINT32(MF_MT_VIDEO_NOMINAL_RANGE, &range));
        if (!haveRange && native)
            haveRange = SUCCEEDED(native->GetUINT32(MF_MT_VIDEO_NOMINAL_RANGE, &range));
        if (haveRange && range == static_cast<UINT32>(MFNominalRange_48_208))
        {
            DE_LOG_WARN(LogCategory::Media, "Movie: nominal range 48-208 uses the limited matrix");
            info.fullRange = false;
        }
        else if (haveRange && range == 1u)
            info.fullRange = true;
        else if (!haveRange && !hardware)
            info.fullRange = true;
        else
            info.fullRange = false;

        info.bt709 = codedH > 576u;
        if (!info.fullRange && !info.bt709)
            info.colorMode = 0;
        else if (!info.fullRange && info.bt709)
            info.colorMode = 1;
        else if (info.fullRange && !info.bt709)
            info.colorMode = 2;
        else
            info.colorMode = 3;

        UINT32 rotation = 0;
        if (!type || FAILED(type->GetUINT32(MF_MT_VIDEO_ROTATION, &rotation)))
        {
            if (!native || FAILED(native->GetUINT32(MF_MT_VIDEO_ROTATION, &rotation)))
                rotation = 0;
        }
        if (!rotationWarned && (rotation == static_cast<UINT32>(MFVideoRotationFormat_90) || rotation == static_cast<UINT32>(MFVideoRotationFormat_270)))
        {
            rotationWarned = true;
            DE_LOG_WARN(LogCategory::Media, "Movie: rotation {} is not applied", rotation);
        }
        const bool rot180 = rotation == static_cast<UINT32>(MFVideoRotationFormat_180);
        info.flip180      = hardware && rot180;
        cpuFlip180        = !hardware && rot180;

        UINT32 num = 0;
        UINT32 den = 0;
        if (!type || FAILED(MFGetAttributeRatio(type, MF_MT_FRAME_RATE, &num, &den)) || num == 0 || den == 0)
        {
            num = 0;
            den = 0;
            if (native)
                MFGetAttributeRatio(native, MF_MT_FRAME_RATE, &num, &den);
        }
        if (num == 0 || den == 0)
        {
            DE_LOG_WARN(LogCategory::Media, "Movie: missing frame rate, using 30/1");
            num = 30;
            den = 1;
        }
        info.fpsNum        = num;
        info.fpsDen        = den;
        info.frameDuration = static_cast<double>(den) / static_cast<double>(num);

        info.durationSec = 0.0;
        PROPVARIANT duration;
        PropVariantInit(&duration);
        if (SUCCEEDED(reader->GetPresentationAttribute(static_cast<DWORD>(MF_SOURCE_READER_MEDIASOURCE), MF_PD_DURATION, &duration)))
        {
            unsigned long long hns = 0;
            if (duration.vt == VT_UI8)
                hns = duration.uhVal.QuadPart;
            else if (duration.vt == VT_I8 && duration.hVal.QuadPart > 0)
                hns = static_cast<unsigned long long>(duration.hVal.QuadPart);
            info.durationSec = static_cast<double>(hns) / 10000000.0;
        }
        PropVariantClear(&duration);
        return info.width > 0 && info.height > 0;
    }

    bool MovieSource::Impl::apertureEven() const
    {
        if (info.width == 0 || info.height == 0)
            return false;
        return ((info.width | info.height | cropLeft | cropTop) & 1u) == 0;
    }

    bool MovieSource::Impl::createPool()
    {
        releasePool();
        if (!device11 || !device5 || !device12 || !apertureEven())
            return false;

        D3D11_TEXTURE2D_DESC desc{};
        desc.Width          = info.width;
        desc.Height         = info.height;
        desc.MipLevels      = 1;
        desc.ArraySize      = 1;
        desc.Format         = DXGI_FORMAT_NV12;
        desc.SampleDesc.Count = 1;
        desc.Usage          = D3D11_USAGE_DEFAULT;
        desc.BindFlags      = D3D11_BIND_SHADER_RESOURCE;
        desc.MiscFlags      = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;

        for (uint32_t i = 0; i < kPoolSurfaces; ++i)
        {
            HRESULT hr = device11->CreateTexture2D(&desc, nullptr, &pool11[i]);
            if (FAILED(hr))
            {
                releasePool();
                return false;
            }
            ComPtr<IDXGIResource1> shared;
            hr = pool11[i].As(&shared);
            if (FAILED(hr))
            {
                releasePool();
                return false;
            }
            hr = shared->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &sharedHandle[i]);
            if (FAILED(hr))
            {
                releasePool();
                return false;
            }
            hr = device12->OpenSharedHandle(sharedHandle[i], IID_PPV_ARGS(&nv12[i]));
            CloseHandle(sharedHandle[i]);
            sharedHandle[i] = nullptr;
            if (FAILED(hr))
            {
                releasePool();
                return false;
            }
        }

        HRESULT hr = device12->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&fence12));
        if (FAILED(hr))
        {
            releasePool();
            return false;
        }
        hr = device12->CreateSharedHandle(fence12.Get(), nullptr, GENERIC_ALL, nullptr, &fenceHandle);
        if (FAILED(hr))
        {
            releasePool();
            return false;
        }
        hr = device5->OpenSharedFence(fenceHandle, IID_PPV_ARGS(&fence11));
        CloseHandle(fenceHandle);
        fenceHandle = nullptr;
        if (FAILED(hr))
        {
            releasePool();
            return false;
        }

        static const wchar_t* kNames[kPoolSurfaces] = { L"DE.MovieNv12.0", L"DE.MovieNv12.1", L"DE.MovieNv12.2" };
        for (uint32_t i = 0; i < kPoolSurfaces; ++i)
            nv12[i]->SetName(kNames[i]);
        fence12->SetName(L"DE.MovieDecodeFence");
        nextFence = 0;
        return true;
    }

    MovieStatus MovieSource::Impl::configureVideo()
    {
        hardware   = false;
        cpuFlip180 = false;
        const bool allowSoft = !mpeg2 && movieSoftwareFallbackAllowed(codedW, codedH);

        bool hwOk = false;
        if (device11 && manager && tryOutput(MFVideoFormat_NV12, false))
        {
            ComPtr<IMFMediaType> current;
            reader->GetCurrentMediaType(videoStream, &current);
            if (readPictureFormat(current.Get()) && apertureEven() && createPool())
                hwOk = true;
            else
                releasePool();
        }

        if (!hwOk)
        {
            releasePool();
            if (!allowSoft)
            {
                if (mpeg2)
                    DE_LOG_ERROR(LogCategory::Media, "Movie: MPEG-2 hardware decoder is not available");
                else
                    DE_LOG_ERROR(LogCategory::Media, "Movie: H.264 DXVA profile is not available");
                return MovieStatus::UnsupportedCodec;
            }

            DE_LOG_WARN(LogCategory::Media, "Movie: software RGB32 fallback");
            releaseDevice();
            const MovieStatus readerStatus = createReader(false);
            if (readerStatus != MovieStatus::Ok)
                return readerStatus;
            const MovieStatus selectStatus = selectStreams();
            if (selectStatus != MovieStatus::Ok)
                return selectStatus;
            if (!tryOutput(MFVideoFormat_RGB32, true))
            {
                DE_LOG_ERROR(LogCategory::Media, "Movie: software RGB32 type failed");
                return MovieStatus::UnsupportedCodec;
            }
        }

        hardware = hwOk;
        ComPtr<IMFMediaType> current;
        if (FAILED(reader->GetCurrentMediaType(videoStream, &current)) || !readPictureFormat(current.Get()))
        {
            DE_LOG_ERROR(LogCategory::Media, "Movie: video format failed");
            return MovieStatus::DecodeError;
        }
        info.hardware = hardware;
        if (!hardware && (info.width == 0 || info.height == 0 || !movieSoftwareFallbackAllowed(info.width, info.height)))
        {
            DE_LOG_ERROR(LogCategory::Media, "Movie: software RGB32 type failed");
            return MovieStatus::UnsupportedCodec;
        }
        return MovieStatus::Ok;
    }

    void MovieSource::Impl::configureAudio()
    {
        info.hasAudio      = false;
        info.audioChannels = 0;
        info.sampleRate    = 48000;
        audioFloat         = false;
        audioBits          = 16;
        if (!hasAudio)
            return;

        struct Attempt
        {
            uint32_t rate;
            uint32_t channels;
        };
        Attempt attempts[3]{};
        int     attemptCount = 0;
        attempts[attemptCount++] = { 48000u, 2u };
        if (nativeAudioRate != 0 && nativeAudioRate != 48000u)
            attempts[attemptCount++] = { nativeAudioRate, 2u };
        if (nativeAudioChannels == 1u || nativeAudioChannels == 6u)
        {
            const uint32_t rate = nativeAudioRate != 0 ? nativeAudioRate : 48000u;
            attempts[attemptCount++] = { rate, nativeAudioChannels };
        }

        bool negotiated = false;
        for (int i = 0; i < attemptCount; ++i)
        {
            const uint32_t rate     = attempts[i].rate;
            const uint32_t channels = attempts[i].channels;
            if (rate == 0 || channels == 0)
                continue;

            ComPtr<IMFMediaType> outType;
            if (FAILED(MFCreateMediaType(&outType)))
                continue;
            outType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
            outType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
            outType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
            outType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, channels);
            outType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, rate);
            outType->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, channels * 2u);
            outType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, rate * channels * 2u);
            outType->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
            if (FAILED(reader->SetCurrentMediaType(audioStream, nullptr, outType.Get())))
                continue;

            ComPtr<IMFMediaType> got;
            if (FAILED(reader->GetCurrentMediaType(audioStream, &got)) || !got)
                continue;
            GUID sub = GUID_NULL;
            got->GetGUID(MF_MT_SUBTYPE, &sub);
            UINT32 gotChannels = 0;
            UINT32 gotBits     = 0;
            UINT32 gotRate     = 0;
            got->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &gotChannels);
            got->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &gotBits);
            got->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &gotRate);
            const bool isFloat = sameGuid(sub, MFAudioFormat_Float);
            const bool isPcm   = sameGuid(sub, MFAudioFormat_PCM) || isFloat;
            if (!isPcm || (gotChannels != 1 && gotChannels != 2 && gotChannels != 6) || gotRate == 0)
                continue;
            if (gotBits != 16 && !(isFloat && gotBits == 32))
                continue;

            info.hasAudio      = true;
            info.audioChannels = gotChannels;
            info.sampleRate    = gotRate;
            audioBits          = gotBits;
            audioFloat         = isFloat;
            uint32_t packets   = gotRate / 50u;
            if (packets < 1u)
                packets = 1u;
            if (packets > kMovieAudioPacketFrames)
                packets = kMovieAudioPacketFrames;
            packetFrames = packets;
            negotiated   = true;
            break;
        }

        if (!negotiated)
        {
            reader->SetStreamSelection(audioStream, FALSE);
            hasAudio = false;
            DE_LOG_WARN(LogCategory::Media, "Movie: audio track dropped");
        }
    }

    void MovieSource::Impl::noteDecode(float ms, MovieRead& out)
    {
        if (!decodeSeeded)
        {
            decodeMs     = ms;
            decodeSeeded = true;
        }
        else
            decodeMs = decodeMs * 0.9f + ms * 0.1f;
        out.decodeMs = decodeMs;
    }

    bool MovieSource::Impl::packSoftware(const uint8_t* data, uint32_t bytes, int32_t stride, std::vector<uint8_t>& dst) const
    {
        if (!data || stride == 0 || stride == INT32_MIN || codedW == 0 || codedH == 0)
            return false;

        const int64_t abs64 = stride < 0 ? -static_cast<int64_t>(stride) : static_cast<int64_t>(stride);
        const int64_t minS  = static_cast<int64_t>(codedW) * 4;
        const int64_t maxS  = minS + 1024;
        if (abs64 < minS || abs64 > maxS)
            return false;
        const uint32_t absStride = static_cast<uint32_t>(abs64);
        if (absStride > (UINT32_MAX / codedH))
            return false;
        const uint64_t need = static_cast<uint64_t>(absStride) * codedH;
        if (need > bytes)
            return false;

        const uint32_t width  = info.width;
        const uint32_t height = info.height;
        if (width == 0 || height == 0 || cropLeft > codedW || cropTop > codedH || width > codedW - cropLeft || height > codedH - cropTop)
            return false;

        const size_t tightBytes = static_cast<size_t>(width) * height * 4u;
        std::vector<uint8_t> tight(tightBytes);
        dst.assign(tightBytes, 0);
        for (uint32_t y = 0; y < height; ++y)
        {
            const uint32_t srcY = stride >= 0 ? (cropTop + y) : (codedH - 1u - (cropTop + y));
            const size_t   off  = static_cast<size_t>(srcY) * absStride + static_cast<size_t>(cropLeft) * 4u;
            if (off + static_cast<size_t>(width) * 4u > bytes)
                return false;
            std::memcpy(tight.data() + static_cast<size_t>(y) * width * 4u, data + off, static_cast<size_t>(width) * 4u);
        }
        if (!copyMovieFrameBgra(tight.data(), tight.size(), static_cast<int32_t>(width * 4u), width, height, dst.data(), dst.size()))
            return false;
        if (cpuFlip180)
            flip180(dst, width, height);
        return true;
    }

    bool MovieSource::Impl::copyHardware(IMFSample* sample, uint32_t surface, uint64_t& fenceValue)
    {
        if (!sample || surface >= kPoolSurfaces || !pool11[surface] || !context11 || !context4 || !fence11)
            return false;

        ComPtr<IMFMediaBuffer> buffer;
        if (FAILED(sample->GetBufferByIndex(0, &buffer)) || !buffer)
            return false;
        ComPtr<IMFDXGIBuffer> dxgi;
        if (FAILED(buffer.As(&dxgi)) || !dxgi)
            return false;
        ComPtr<ID3D11Texture2D> src;
        if (FAILED(dxgi->GetResource(IID_PPV_ARGS(&src))) || !src)
            return false;
        UINT subresource = 0;
        dxgi->GetSubresourceIndex(&subresource);

        D3D11_TEXTURE2D_DESC srcDesc{};
        src->GetDesc(&srcDesc);
        D3D11_BOX box{};
        box.front = 0;
        box.back  = 1;
        if (srcDesc.Width == info.width && srcDesc.Height == info.height)
        {
            box.left   = 0;
            box.top    = 0;
            box.right  = info.width;
            box.bottom = info.height;
        }
        else if (cropLeft + info.width <= srcDesc.Width && cropTop + info.height <= srcDesc.Height)
        {
            box.left   = cropLeft;
            box.top    = cropTop;
            box.right  = cropLeft + info.width;
            box.bottom = cropTop + info.height;
        }
        else
            return false;

        context11->CopySubresourceRegion(pool11[surface].Get(), 0, 0, 0, 0, src.Get(), subresource, &box);
        const uint64_t value = ++nextFence;
        if (FAILED(context4->Signal(fence11.Get(), value)))
            return false;
        context11->Flush();
        fenceValue = value;
        return true;
    }

    void MovieSource::Impl::readStream(bool video, uint32_t surface, MovieRead& out)
    {
        const Clock::time_point t0 = Clock::now();
        const DWORD stream = video ? videoStream : audioStream;
        DWORD       flags  = 0;
        LONGLONG    timestamp = 0;
        ComPtr<IMFSample> sample;
        const HRESULT hr = reader->ReadSample(stream, 0, nullptr, &flags, &timestamp, &sample);
        if (FAILED(hr) || (flags & static_cast<DWORD>(MF_SOURCE_READERF_ERROR)) != 0)
        {
            const HRESULT reported = FAILED(hr) ? hr : E_FAIL;
            DE_LOG_ERROR(LogCategory::Media, "Movie: ReadSample failed (HRESULT 0x{:08X})", static_cast<unsigned>(reported));
            out.kind   = MovieReadKind::Error;
            out.status = statusFromHr(reported);
            noteDecode(std::chrono::duration<float, std::milli>(Clock::now() - t0).count(), out);
            return;
        }

        if (video && ((flags & static_cast<DWORD>(MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED)) != 0 || (flags & static_cast<DWORD>(MF_SOURCE_READERF_NATIVEMEDIATYPECHANGED)) != 0))
        {
            ComPtr<IMFMediaType> current;
            UINT32 w = 0;
            UINT32 h = 0;
            if (SUCCEEDED(reader->GetCurrentMediaType(videoStream, &current)) && current && SUCCEEDED(MFGetAttributeSize(current.Get(), MF_MT_FRAME_SIZE, &w, &h)) && (w != codedW || h != codedH))
            {
                DE_LOG_ERROR(LogCategory::Media, "Movie: frame size changed");
                out.kind   = MovieReadKind::Error;
                out.status = MovieStatus::DecodeError;
                noteDecode(std::chrono::duration<float, std::milli>(Clock::now() - t0).count(), out);
                return;
            }
        }

        const bool eos = (flags & static_cast<DWORD>(MF_SOURCE_READERF_ENDOFSTREAM)) != 0;
        if (eos)
        {
            if (video)
                videoEnded = true;
            else
                audioEnded = true;
        }
        out.end  = eos;
        out.kind = video ? MovieReadKind::Video : MovieReadKind::Audio;

        auto takeRemainder = [&]() {
            if (remainder.size() < 2)
            {
                remainder.clear();
                return;
            }
            if ((remainder.size() % 2u) != 0)
                remainder.pop_back();
            if (remainder.size() >= 2)
                out.audio.packets.push_back(std::move(remainder));
            remainder.clear();
        };

        if (!sample || (flags & static_cast<DWORD>(MF_SOURCE_READERF_STREAMTICK)) != 0)
        {
            out.hasSample = false;
            if (!video && eos)
            {
                takeRemainder();
                out.hasSample = !out.audio.packets.empty();
                if (timestamp >= 0)
                    out.audio.pts = static_cast<double>(timestamp) / 10000000.0;
            }
            noteDecode(std::chrono::duration<float, std::milli>(Clock::now() - t0).count(), out);
            return;
        }

        double pts = 0.0;
        if (timestamp >= 0)
            pts = static_cast<double>(timestamp) / 10000000.0;
        else
        {
            LONGLONG sampleTime = 0;
            if (SUCCEEDED(sample->GetSampleTime(&sampleTime)) && sampleTime >= 0)
                pts = static_cast<double>(sampleTime) / 10000000.0;
        }
        double duration = 0.0;
        LONGLONG sampleDuration = 0;
        if (SUCCEEDED(sample->GetSampleDuration(&sampleDuration)) && sampleDuration > 0)
            duration = static_cast<double>(sampleDuration) / 10000000.0;

        if (video)
        {
            if (duration <= 0.0)
                duration = info.frameDuration;
            out.video.pts      = pts;
            out.video.duration = duration;
            out.video.hardware = hardware;
            out.video.surface  = surface;
            if (hardware)
            {
                uint64_t fenceValue = 0;
                if (!copyHardware(sample.Get(), surface, fenceValue))
                {
                    DE_LOG_ERROR(LogCategory::Media, "Movie: NV12 copy failed");
                    out.kind   = MovieReadKind::Error;
                    out.status = MovieStatus::DecodeError;
                    noteDecode(std::chrono::duration<float, std::milli>(Clock::now() - t0).count(), out);
                    return;
                }
                out.video.fenceValue = fenceValue;
            }
            else
            {
                ComPtr<IMFMediaBuffer> contiguous;
                if (FAILED(sample->ConvertToContiguousBuffer(&contiguous)) || !contiguous)
                {
                    DE_LOG_ERROR(LogCategory::Media, "Movie: ConvertToContiguousBuffer failed");
                    out.kind   = MovieReadKind::Error;
                    out.status = MovieStatus::DecodeError;
                    noteDecode(std::chrono::duration<float, std::milli>(Clock::now() - t0).count(), out);
                    return;
                }
                BYTE* data    = nullptr;
                DWORD maxLen  = 0;
                DWORD current = 0;
                if (FAILED(contiguous->Lock(&data, &maxLen, &current)) || !data)
                {
                    DE_LOG_ERROR(LogCategory::Media, "Movie: buffer lock failed");
                    out.kind   = MovieReadKind::Error;
                    out.status = MovieStatus::DecodeError;
                    noteDecode(std::chrono::duration<float, std::milli>(Clock::now() - t0).count(), out);
                    return;
                }
                INT32 stride = static_cast<INT32>(codedW * 4u);
                ComPtr<IMFMediaType> currentType;
                UINT32 strideBits = 0;
                if (SUCCEEDED(reader->GetCurrentMediaType(videoStream, &currentType)) && currentType && SUCCEEDED(currentType->GetUINT32(MF_MT_DEFAULT_STRIDE, &strideBits)))
                    stride = static_cast<INT32>(strideBits);
                const bool packed = packSoftware(data, current, stride, out.video.bgra);
                contiguous->Unlock();
                if (!packed)
                {
                    DE_LOG_ERROR(LogCategory::Media, "Movie: frame stride is not supported");
                    out.kind   = MovieReadKind::Error;
                    out.status = MovieStatus::DecodeError;
                    noteDecode(std::chrono::duration<float, std::milli>(Clock::now() - t0).count(), out);
                    return;
                }
            }
            out.hasSample = true;
        }
        else
        {
            ComPtr<IMFMediaBuffer> contiguous;
            if (FAILED(sample->ConvertToContiguousBuffer(&contiguous)) || !contiguous)
            {
                DE_LOG_ERROR(LogCategory::Media, "Movie: ConvertToContiguousBuffer failed");
                out.kind   = MovieReadKind::Error;
                out.status = MovieStatus::DecodeError;
                noteDecode(std::chrono::duration<float, std::milli>(Clock::now() - t0).count(), out);
                return;
            }
            BYTE* data    = nullptr;
            DWORD maxLen  = 0;
            DWORD current = 0;
            if (FAILED(contiguous->Lock(&data, &maxLen, &current)))
            {
                out.kind   = MovieReadKind::Error;
                out.status = MovieStatus::DecodeError;
                noteDecode(std::chrono::duration<float, std::milli>(Clock::now() - t0).count(), out);
                return;
            }
            const uint32_t bytesPer = info.audioChannels * (audioBits / 8u);
            uint32_t frames = 0;
            if (bytesPer != 0)
                frames = current / bytesPer;
            MovieAudioPackInput pack{};
            pack.samples    = data;
            pack.frameCount = frames;
            pack.channels   = info.audioChannels;
            pack.float32    = audioFloat;
            const bool packed = packMovieAudio(pack, remainder, out.audio.packets, packetFrames);
            contiguous->Unlock();
            if (!packed)
            {
                DE_LOG_WARN(LogCategory::Media, "Movie: audio track dropped");
                hasAudio      = false;
                info.hasAudio = false;
                audioEnded    = true;
                out.hasSample = false;
                out.end       = true;
                noteDecode(std::chrono::duration<float, std::milli>(Clock::now() - t0).count(), out);
                return;
            }
            if (eos)
                takeRemainder();
            if (duration <= 0.0 && info.sampleRate != 0)
                duration = static_cast<double>(frames) / static_cast<double>(info.sampleRate);
            out.audio.pts      = pts;
            out.audio.duration = duration;
            out.hasSample      = !out.audio.packets.empty() || frames > 0;
            if (out.audio.packets.empty())
                out.hasSample = false;
        }

        noteDecode(std::chrono::duration<float, std::milli>(Clock::now() - t0).count(), out);
    }

    MovieSource::MovieSource()
        : m(std::make_unique<Impl>())
    {
    }

    MovieSource::~MovieSource()
    {
        close();
    }

    MovieStatus MovieSource::startup()
    {
        std::lock_guard<std::mutex> lock(g_mfMutex);
        if (g_mfCount == 0)
        {
            const HRESULT hr = MFStartup(MF_VERSION);
            if (FAILED(hr))
            {
                DE_LOG_ERROR(LogCategory::Media, "Movie: MFStartup failed (HRESULT 0x{:08X})", static_cast<unsigned>(hr));
                return MovieStatus::DecodeError;
            }
        }
        ++g_mfCount;
        return MovieStatus::Ok;
    }

    void MovieSource::shutdown()
    {
        std::lock_guard<std::mutex> lock(g_mfMutex);
        if (g_mfCount <= 0)
            return;
        --g_mfCount;
        if (g_mfCount == 0)
            MFShutdown();
    }

    MovieStatus MovieSource::open(const MovieSourceRequest& request, MovieSourceInfo& info)
    {
        close();
        if (!m)
            return MovieStatus::DecodeError;
        if (request.absolutePath.empty())
        {
            DE_LOG_ERROR(LogCategory::Media, "Movie: empty path");
            return MovieStatus::DecodeError;
        }

        m->openedPath = request.absolutePath;
        m->tryHardware(request);
        MovieStatus status = m->createReader(m->manager != nullptr);
        if (status != MovieStatus::Ok)
        {
            close();
            return status;
        }
        if (m->protectedContent())
        {
            DE_LOG_ERROR(LogCategory::Media, "Movie: protected content is not supported");
            close();
            return MovieStatus::UnsupportedCodec;
        }
        status = m->selectStreams();
        if (status != MovieStatus::Ok)
        {
            close();
            return status;
        }
        status = m->configureVideo();
        if (status != MovieStatus::Ok)
        {
            close();
            return status;
        }
        m->configureAudio();
        m->videoEnded = false;
        m->audioEnded = false;
        m->remainder.clear();
        info = m->info;
        return MovieStatus::Ok;
    }

    void MovieSource::close()
    {
        if (m)
            m->close();
    }

    void MovieSource::read(bool preferVideo, uint32_t surface, MovieRead& out)
    {
        out = MovieRead{};
        if (!m || !m->reader || !m->hasVideo)
        {
            out.kind   = MovieReadKind::Error;
            out.status = MovieStatus::DecodeError;
            return;
        }

        bool video = preferVideo;
        if (video && m->videoEnded)
            video = false;
        if (!video && (!m->hasAudio || !m->info.hasAudio || m->audioEnded))
        {
            if (!m->videoEnded)
                video = true;
            else
            {
                out.kind = MovieReadKind::None;
                out.end  = true;
                return;
            }
        }
        m->readStream(video, surface, out);
    }

    MovieStatus MovieSource::setPosition(double seconds)
    {
        if (!m || !m->reader)
            return MovieStatus::DecodeError;
        if (seconds < 0.0)
            seconds = 0.0;
        const double hns = seconds * 10000000.0;
        if (hns > static_cast<double>(INT64_MAX))
            return MovieStatus::DecodeError;

        PROPVARIANT position;
        PropVariantInit(&position);
        position.vt            = VT_I8;
        position.hVal.QuadPart = static_cast<LONGLONG>(hns);
        const HRESULT hr       = m->reader->SetCurrentPosition(GUID_NULL, position);
        PropVariantClear(&position);
        if (FAILED(hr))
        {
            logHr("SetCurrentPosition", hr);
            return statusFromHr(hr);
        }
        m->videoEnded = false;
        m->audioEnded = false;
        m->remainder.clear();
        return MovieStatus::Ok;
    }

    void* MovieSource::nv12(uint32_t surface) const
    {
        if (!m || surface >= kPoolSurfaces)
            return nullptr;
        return m->nv12[surface].Get();
    }

    void* MovieSource::decodeFence() const
    {
        if (!m)
            return nullptr;
        return m->fence12.Get();
    }

    const MovieSourceInfo& MovieSource::info() const
    {
        static const MovieSourceInfo kEmpty{};
        if (!m)
            return kEmpty;
        return m->info;
    }

} // namespace Dark
