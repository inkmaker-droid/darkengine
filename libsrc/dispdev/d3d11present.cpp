#include <windows.h>
#include <ddraw.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <string.h>

#include "d3d11present.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

template <class T>
static void ReleaseInterface(T *&object)
{
    if (object)
    {
        object->Release();
        object = NULL;
    }
}

struct sGammaConstants
{
    float gamma;
    float padding[3];
};

struct cD3D11Presenter::sImpl
{
    HWND hwnd;
    DWORD sourceWidth;
    DWORD sourceHeight;
    DWORD targetWidth;
    DWORD targetHeight;
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    IDXGISwapChain *swapChain;
    ID3D11RenderTargetView *renderTarget;
    ID3D11Texture2D *sourceTexture;
    ID3D11ShaderResourceView *sourceView;
    ID3D11VertexShader *vertexShader;
    ID3D11PixelShader *pixelShader;
    ID3D11SamplerState *sampler;
    ID3D11Buffer *gammaBuffer;
    float gamma;

    sImpl()
      : hwnd(NULL),
        sourceWidth(0),
        sourceHeight(0),
        targetWidth(0),
        targetHeight(0),
        device(NULL),
        context(NULL),
        swapChain(NULL),
        renderTarget(NULL),
        sourceTexture(NULL),
        sourceView(NULL),
        vertexShader(NULL),
        pixelShader(NULL),
        sampler(NULL),
        gammaBuffer(NULL),
        gamma(1.0f)
    {
    }
};

static HRESULT CompileShader(const char *source, const char *entry,
                             const char *target, ID3DBlob **shader)
{
    ID3DBlob *errors = NULL;
    HRESULT result = D3DCompile(source, strlen(source), NULL, NULL, NULL,
                                entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3,
                                0, shader, &errors);
    ReleaseInterface(errors);
    return result;
}

cD3D11Presenter::cD3D11Presenter()
  : m_pImpl(new sImpl)
{
}

cD3D11Presenter::~cD3D11Presenter()
{
    Stop();
    delete m_pImpl;
}

void cD3D11Presenter::Stop()
{
    if (!m_pImpl)
        return;

    if (m_pImpl->context)
        m_pImpl->context->ClearState();

    ReleaseInterface(m_pImpl->gammaBuffer);
    ReleaseInterface(m_pImpl->sampler);
    ReleaseInterface(m_pImpl->pixelShader);
    ReleaseInterface(m_pImpl->vertexShader);
    ReleaseInterface(m_pImpl->sourceView);
    ReleaseInterface(m_pImpl->sourceTexture);
    ReleaseInterface(m_pImpl->renderTarget);
    ReleaseInterface(m_pImpl->swapChain);
    ReleaseInterface(m_pImpl->context);
    ReleaseInterface(m_pImpl->device);
    m_pImpl->hwnd = NULL;
    m_pImpl->sourceWidth = 0;
    m_pImpl->sourceHeight = 0;
    m_pImpl->targetWidth = 0;
    m_pImpl->targetHeight = 0;
}

HRESULT cD3D11Presenter::CreateRenderTarget(sImpl *impl)
{
    ID3D11Texture2D *backBuffer = NULL;
    HRESULT result = impl->swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                                 (void **)&backBuffer);
    if (SUCCEEDED(result))
        result = impl->device->CreateRenderTargetView(backBuffer, NULL,
                                                       &impl->renderTarget);
    ReleaseInterface(backBuffer);
    return result;
}

BOOL cD3D11Presenter::Start(HWND hwnd, DWORD sourceWidth, DWORD sourceHeight)
{
    static const char vertexSource[] =
        "struct VOut { float4 position : SV_Position; float2 uv : TEXCOORD0; };"
        "VOut main(uint id : SV_VertexID) {"
        "  VOut output;"
        "  float2 uv = float2((id << 1) & 2, id & 2);"
        "  output.position = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.0, 1.0);"
        "  output.uv = uv;"
        "  return output;"
        "}";
    static const char pixelSource[] =
        "cbuffer GammaConstants : register(b0) { float gammaValue; float3 padding; };"
        "Texture2D sourceTexture : register(t0);"
        "SamplerState sourceSampler : register(s0);"
        "float4 main(float4 position : SV_Position, float2 uv : TEXCOORD0) : SV_Target {"
        "  float4 color = sourceTexture.Sample(sourceSampler, uv);"
        "  color.rgb = pow(saturate(color.rgb), gammaValue);"
        "  return color;"
        "}";

    RECT clientRect;
    DXGI_SWAP_CHAIN_DESC swapDesc;
    D3D_FEATURE_LEVEL requestedLevels[] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };
    D3D_FEATURE_LEVEL selectedLevel;
    D3D11_TEXTURE2D_DESC textureDesc;
    D3D11_SAMPLER_DESC samplerDesc;
    D3D11_BUFFER_DESC bufferDesc;
    D3D11_SUBRESOURCE_DATA bufferData;
    sGammaConstants gammaConstants;
    ID3DBlob *vertexBytecode = NULL;
    ID3DBlob *pixelBytecode = NULL;
    HRESULT result;

    Stop();
    if (!hwnd || !sourceWidth || !sourceHeight)
        return FALSE;

    GetClientRect(hwnd, &clientRect);
    ZeroMemory(&swapDesc, sizeof(swapDesc));
    swapDesc.BufferDesc.Width = clientRect.right - clientRect.left;
    swapDesc.BufferDesc.Height = clientRect.bottom - clientRect.top;
    swapDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapDesc.SampleDesc.Count = 1;
    swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapDesc.BufferCount = 1;
    swapDesc.OutputWindow = hwnd;
    swapDesc.Windowed = TRUE;
    swapDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    result = D3D11CreateDeviceAndSwapChain(
        NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        requestedLevels, sizeof(requestedLevels) / sizeof(requestedLevels[0]),
        D3D11_SDK_VERSION, &swapDesc, &m_pImpl->swapChain, &m_pImpl->device,
        &selectedLevel, &m_pImpl->context);
    if (FAILED(result))
    {
        result = D3D11CreateDeviceAndSwapChain(
            NULL, D3D_DRIVER_TYPE_WARP, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            requestedLevels, sizeof(requestedLevels) / sizeof(requestedLevels[0]),
            D3D11_SDK_VERSION, &swapDesc, &m_pImpl->swapChain, &m_pImpl->device,
            &selectedLevel, &m_pImpl->context);
    }
    if (FAILED(result))
        goto failure;

    result = CreateRenderTarget(m_pImpl);
    if (FAILED(result))
        goto failure;

    ZeroMemory(&textureDesc, sizeof(textureDesc));
    textureDesc.Width = sourceWidth;
    textureDesc.Height = sourceHeight;
    textureDesc.MipLevels = 1;
    textureDesc.ArraySize = 1;
    textureDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.Usage = D3D11_USAGE_DYNAMIC;
    textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    textureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    result = m_pImpl->device->CreateTexture2D(&textureDesc, NULL,
                                               &m_pImpl->sourceTexture);
    if (FAILED(result))
        goto failure;
    result = m_pImpl->device->CreateShaderResourceView(m_pImpl->sourceTexture,
                                                        NULL,
                                                        &m_pImpl->sourceView);
    if (FAILED(result))
        goto failure;

    result = CompileShader(vertexSource, "main", "vs_4_0", &vertexBytecode);
    if (FAILED(result))
        goto failure;
    result = m_pImpl->device->CreateVertexShader(vertexBytecode->GetBufferPointer(),
                                                  vertexBytecode->GetBufferSize(),
                                                  NULL, &m_pImpl->vertexShader);
    ReleaseInterface(vertexBytecode);
    if (FAILED(result))
        goto failure;

    result = CompileShader(pixelSource, "main", "ps_4_0", &pixelBytecode);
    if (FAILED(result))
        goto failure;
    result = m_pImpl->device->CreatePixelShader(pixelBytecode->GetBufferPointer(),
                                                 pixelBytecode->GetBufferSize(),
                                                 NULL, &m_pImpl->pixelShader);
    ReleaseInterface(pixelBytecode);
    if (FAILED(result))
        goto failure;

    ZeroMemory(&samplerDesc, sizeof(samplerDesc));
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
    result = m_pImpl->device->CreateSamplerState(&samplerDesc,
                                                  &m_pImpl->sampler);
    if (FAILED(result))
        goto failure;

    ZeroMemory(&bufferDesc, sizeof(bufferDesc));
    bufferDesc.ByteWidth = sizeof(sGammaConstants);
    bufferDesc.Usage = D3D11_USAGE_DEFAULT;
    bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    ZeroMemory(&gammaConstants, sizeof(gammaConstants));
    gammaConstants.gamma = m_pImpl->gamma;
    ZeroMemory(&bufferData, sizeof(bufferData));
    bufferData.pSysMem = &gammaConstants;
    result = m_pImpl->device->CreateBuffer(&bufferDesc, &bufferData,
                                            &m_pImpl->gammaBuffer);
    if (FAILED(result))
        goto failure;

    m_pImpl->hwnd = hwnd;
    m_pImpl->sourceWidth = sourceWidth;
    m_pImpl->sourceHeight = sourceHeight;
    m_pImpl->targetWidth = swapDesc.BufferDesc.Width;
    m_pImpl->targetHeight = swapDesc.BufferDesc.Height;
    return TRUE;

failure:
    ReleaseInterface(vertexBytecode);
    ReleaseInterface(pixelBytecode);
    Stop();
    return FALSE;
}

BOOL cD3D11Presenter::SetGamma(double gamma)
{
    sGammaConstants constants;

    if (!m_pImpl || gamma <= 0.0)
        return FALSE;

    m_pImpl->gamma = (float)gamma;
    if (m_pImpl->context && m_pImpl->gammaBuffer)
    {
        ZeroMemory(&constants, sizeof(constants));
        constants.gamma = m_pImpl->gamma;
        m_pImpl->context->UpdateSubresource(m_pImpl->gammaBuffer, 0, NULL,
                                             &constants, 0, 0);
    }
    return TRUE;
}

static BYTE Expand5(DWORD value)
{
    return (BYTE)((value << 3) | (value >> 2));
}

static BYTE Expand6(DWORD value)
{
    return (BYTE)((value << 2) | (value >> 4));
}

BOOL cD3D11Presenter::Present(IDirectDrawSurface *surface)
{
    DDSURFACEDESC surfaceDesc;
    D3D11_MAPPED_SUBRESOURCE mapped;
    D3D11_VIEWPORT viewport;
    RECT clientRect;
    float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    DWORD clientWidth;
    DWORD clientHeight;
    float sourceAspect;
    float clientAspect;
    DWORD y;
    HRESULT result;

    if (!m_pImpl || !m_pImpl->device || !surface)
        return FALSE;

    ZeroMemory(&surfaceDesc, sizeof(surfaceDesc));
    surfaceDesc.dwSize = sizeof(surfaceDesc);
    result = surface->Lock(NULL, &surfaceDesc, DDLOCK_READONLY | DDLOCK_WAIT, NULL);
    if (FAILED(result))
        return FALSE;

    if (surfaceDesc.ddpfPixelFormat.dwRGBBitCount != 15 &&
        surfaceDesc.ddpfPixelFormat.dwRGBBitCount != 16 &&
        surfaceDesc.ddpfPixelFormat.dwRGBBitCount != 32)
    {
        surface->Unlock(NULL);
        return FALSE;
    }

    result = m_pImpl->context->Map(m_pImpl->sourceTexture, 0,
                                    D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(result))
    {
        surface->Unlock(NULL);
        return FALSE;
    }

    for (y = 0; y < m_pImpl->sourceHeight; ++y)
    {
        const BYTE *sourceRow = (const BYTE *)surfaceDesc.lpSurface + y * surfaceDesc.lPitch;
        DWORD *destination = (DWORD *)((BYTE *)mapped.pData + y * mapped.RowPitch);
        DWORD x;

        if (surfaceDesc.ddpfPixelFormat.dwRGBBitCount == 32)
        {
            const DWORD *source = (const DWORD *)sourceRow;
            for (x = 0; x < m_pImpl->sourceWidth; ++x)
                destination[x] = source[x] | 0xff000000;
        }
        else
        {
            const WORD *source = (const WORD *)sourceRow;
            const BOOL rgb555 = surfaceDesc.ddpfPixelFormat.dwGBitMask == 0x03e0;
            for (x = 0; x < m_pImpl->sourceWidth; ++x)
            {
                WORD pixel = source[x];
                BYTE red;
                BYTE green;
                BYTE blue;
                if (rgb555)
                {
                    red = Expand5((pixel >> 10) & 0x1f);
                    green = Expand5((pixel >> 5) & 0x1f);
                    blue = Expand5(pixel & 0x1f);
                }
                else
                {
                    red = Expand5((pixel >> 11) & 0x1f);
                    green = Expand6((pixel >> 5) & 0x3f);
                    blue = Expand5(pixel & 0x1f);
                }
                destination[x] = 0xff000000 | ((DWORD)red << 16) |
                                 ((DWORD)green << 8) | blue;
            }
        }
    }

    m_pImpl->context->Unmap(m_pImpl->sourceTexture, 0);
    surface->Unlock(NULL);

    GetClientRect(m_pImpl->hwnd, &clientRect);
    clientWidth = clientRect.right - clientRect.left;
    clientHeight = clientRect.bottom - clientRect.top;
    if (!clientWidth || !clientHeight)
        return FALSE;

    if (clientWidth != m_pImpl->targetWidth ||
        clientHeight != m_pImpl->targetHeight)
    {
        m_pImpl->context->OMSetRenderTargets(0, NULL, NULL);
        ReleaseInterface(m_pImpl->renderTarget);
        result = m_pImpl->swapChain->ResizeBuffers(1, clientWidth, clientHeight,
                                                    DXGI_FORMAT_UNKNOWN, 0);
        if (FAILED(result) || FAILED(CreateRenderTarget(m_pImpl)))
            return FALSE;
        m_pImpl->targetWidth = clientWidth;
        m_pImpl->targetHeight = clientHeight;
    }

    sourceAspect = (float)m_pImpl->sourceWidth / (float)m_pImpl->sourceHeight;
    clientAspect = (float)clientWidth / (float)clientHeight;
    ZeroMemory(&viewport, sizeof(viewport));
    if (clientAspect > sourceAspect)
    {
        viewport.Height = (float)clientHeight;
        viewport.Width = viewport.Height * sourceAspect;
        viewport.TopLeftX = ((float)clientWidth - viewport.Width) * 0.5f;
    }
    else
    {
        viewport.Width = (float)clientWidth;
        viewport.Height = viewport.Width / sourceAspect;
        viewport.TopLeftY = ((float)clientHeight - viewport.Height) * 0.5f;
    }
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    m_pImpl->context->OMSetRenderTargets(1, &m_pImpl->renderTarget, NULL);
    m_pImpl->context->ClearRenderTargetView(m_pImpl->renderTarget, clearColor);
    m_pImpl->context->RSSetViewports(1, &viewport);
    m_pImpl->context->IASetInputLayout(NULL);
    m_pImpl->context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_pImpl->context->VSSetShader(m_pImpl->vertexShader, NULL, 0);
    m_pImpl->context->PSSetShader(m_pImpl->pixelShader, NULL, 0);
    m_pImpl->context->PSSetConstantBuffers(0, 1, &m_pImpl->gammaBuffer);
    m_pImpl->context->PSSetShaderResources(0, 1, &m_pImpl->sourceView);
    m_pImpl->context->PSSetSamplers(0, 1, &m_pImpl->sampler);
    m_pImpl->context->Draw(3, 0);
    result = m_pImpl->swapChain->Present(1, 0);
    return SUCCEEDED(result);
}
