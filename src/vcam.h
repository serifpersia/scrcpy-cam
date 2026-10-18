#pragma once
#include <cstdint>
#include <windows.h>
#include <objbase.h>
#include <oaidl.h>

struct IVCamRenderer : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE SetCurrentDevice(const wchar_t* name) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetOutputFormat(long width, long height, long fps, long format, long one_size_only) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetOutputFormat(long* width, long* height, long* fps, long* format, long* one_size_only) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetFillMode(long mode) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetFillMode(long* mode) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetMirror(long mirror) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetFlip(long flip) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetRotateRight(long rotate) = 0;
    virtual HRESULT STDMETHODCALLTYPE SendFrame(const unsigned char* rgb24_pixels, long width, long height, long stride) = 0;
    virtual HRESULT STDMETHODCALLTYPE SendFrame32(const unsigned char* rgb32_pixels, long width, long height, long stride) = 0;
    virtual HRESULT STDMETHODCALLTYPE SendFrameYUY2(const unsigned char* yuy2, long width, long height, long stride) = 0;
    virtual HRESULT STDMETHODCALLTYPE SendFrameNV12(const unsigned char* nv12, long width, long height, long stride) = 0;
    virtual HRESULT STDMETHODCALLTYPE SendFrameEx(VARIANT rgb24_array, long width, long height) = 0;
    virtual HRESULT STDMETHODCALLTYPE CaptureScreen(long x, long y, long width, long height) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetConnectionNotificationEvent(__int64 handle) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetConnectedCount(long* count) = 0;
    virtual HRESULT STDMETHODCALLTYPE IsOpened() = 0;
    virtual HRESULT STDMETHODCALLTYPE Snapshot(const wchar_t* name) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetIdleFileName(const wchar_t* name) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetFriendlyName(const wchar_t* name) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetLicenseCode(const wchar_t* name) = 0;
};

struct VCamRenderer {
    IVCamRenderer* renderer = nullptr;
    long cur_w = 0, cur_h = 0, cur_fps = 30;
    int cur_mirror = 0, cur_flip = 0, cur_rotate = 0;
    HRESULT last_hr = S_OK;

    bool init();
    bool ok() const { return renderer != nullptr; }
    void set_format(int w, int h, int fps);
    void push_rgb24(const uint8_t* data, int w, int h, int fps);
    void set_mirror(int on);
    void set_flip(int on);
    void set_rotate(int on);
    void shutdown();
};
