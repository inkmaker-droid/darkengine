#ifndef __D3D11PRESENT_H
#define __D3D11PRESENT_H

struct IDirectDrawSurface;

// Presents the engine's legacy CPU canvas through a native D3D11 swap chain.
// Keeping this at the display boundary lets the 2D UI, movies, and software
// rasterizer continue to use their existing 16-bit canvas while Windows sees
// only a normal 32-bit borderless window.
class cD3D11Presenter
{
public:
    cD3D11Presenter();
    ~cD3D11Presenter();

    BOOL Start(HWND hwnd, DWORD sourceWidth, DWORD sourceHeight);
    void Stop();
    BOOL SetGamma(double gamma);
    BOOL Present(IDirectDrawSurface *surface);

private:
    struct sImpl;
    static HRESULT CreateRenderTarget(sImpl *impl);
    sImpl *m_pImpl;

    cD3D11Presenter(const cD3D11Presenter &);
    cD3D11Presenter &operator=(const cD3D11Presenter &);
};

#endif
