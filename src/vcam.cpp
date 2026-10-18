#include "vcam.h"

static const CLSID CLSID_VCamRenderer =
    {0x3D2F839E, 0x1186, 0x4FCE, {0xB7, 0x72, 0xB6, 0x1F, 0xAE, 0x1A, 0xCE, 0xD7}};
static const IID IID_IVCamRenderer =
    {0x855DAEFD, 0x6664, 0x49C4, {0xBB, 0xAA, 0x81, 0x3A, 0x9A, 0x41, 0x65, 0x9E}};

bool VCamRenderer::init() {
    shutdown();
    last_hr = CoCreateInstance(CLSID_VCamRenderer, nullptr, CLSCTX_INPROC_SERVER,
                               IID_IVCamRenderer, (void**)&renderer);
    if (FAILED(last_hr) || !renderer) { renderer = nullptr; return false; }
    return true;
}

void VCamRenderer::set_format(int w, int h, int fps) {
    if (!renderer || w <= 0 || h <= 0) return;
    if (fps < 1) fps = 30;
    renderer->SetOutputFormat(w, h, fps, 1, 1);
    cur_w = w;
    cur_h = h;
    cur_fps = fps;
}

void VCamRenderer::push_rgb24(const uint8_t* data, int w, int h, int fps) {
    if (!renderer || !data || w <= 0 || h <= 0) return;
    if (w != cur_w || h != cur_h || fps != cur_fps)
        set_format(w, h, fps);
    long stride = (w * 3 + 3) & ~3L;
    renderer->SendFrame(data, w, h, stride);
}

void VCamRenderer::shutdown() {
    if (renderer) { renderer->Release(); renderer = nullptr; }
    cur_w = cur_h = 0;
}

void VCamRenderer::set_mirror(int on) {
    if (!renderer) return;
    renderer->SetMirror(on ? 1 : 0);
    cur_mirror = on ? 1 : 0;
}

void VCamRenderer::set_flip(int on) {
    if (!renderer) return;
    renderer->SetFlip(on ? 1 : 0);
    cur_flip = on ? 1 : 0;
}

void VCamRenderer::set_rotate(int on) {
    if (!renderer) return;
    renderer->SetRotateRight(on ? 1 : 0);
    cur_rotate = on ? 1 : 0;
}
