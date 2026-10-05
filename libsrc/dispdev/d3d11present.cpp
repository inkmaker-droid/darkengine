#include "d3d11present.h"
#include "d3d11legacy.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <ddraw.h>
#include <string.h>
#include <vector>
#include <windows.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

template <class T> static void ReleaseInterface(T *&p) {
  if (p) {
    p->Release();
    p = NULL;
  }
}

struct sCompositeConstants {
  float gamma, hasScene, padding[2];
};
struct sSceneConstants {
  float width, height, fogEnabled, useTexture1;
  float fogColor[4];
  float alphaTest, padding[3];
};
struct sD3D11LegacyTexture {
  ID3D11Texture2D *texture;
  ID3D11ShaderResourceView *view;
  int width, height;
  sD3D11LegacyTexture() : texture(NULL), view(NULL), width(0), height(0) {}
};

struct sD3D11LegacyCommand {
  UINT firstVertex, vertexCount;
  int primitive, blendMode, depthState;
  BOOL useSecondTexture, fogEnabled, alphaTest;
  DWORD fogColor;
  ID3D11ShaderResourceView *textures[2];
  ID3D11SamplerState *samplers[2];
};

struct cD3D11Presenter::sImpl {
  HWND hwnd;
  DWORD sourceWidth, sourceHeight, targetWidth, targetHeight;
  IDirectDrawSurface *legacySurface;
  ID3D11Device *device;
  ID3D11DeviceContext *context;
  IDXGISwapChain *swapChain;
  ID3D11RenderTargetView *renderTarget;
  ID3D11Texture2D *sourceTexture, *sceneTexture, *depthTexture;
  ID3D11ShaderResourceView *sourceView, *sceneView;
  ID3D11RenderTargetView *sceneTarget;
  ID3D11DepthStencilView *depthView;
  ID3D11VertexShader *compositeVS, *sceneVS;
  ID3D11PixelShader *compositePS, *scenePS;
  ID3D11SamplerState *compositeSampler, *samplers[4];
  ID3D11Buffer *compositeConstants, *sceneConstants, *sceneVertices;
  ID3D11InputLayout *sceneLayout;
  UINT sceneVertexCapacity;
  ID3D11BlendState *blendStates[4];
  ID3D11DepthStencilState *depthStates[4];
  ID3D11RasterizerState *rasterizer;
  ID3D11ShaderResourceView *boundTextures[2];
  ID3D11SamplerState *boundSamplers[2];
  std::vector<sD3D11LegacyVertex> queuedVertices;
  std::vector<sD3D11LegacyCommand> queuedCommands;
  std::vector<DWORD> overlayPixels;
  float gamma;
  BOOL hardwareFrame, sceneActive, sceneOpen, depthCompare, depthWrite,
      fogEnabled, alphaTest;
  int blendMode;
  DWORD fogColor;
  sImpl()
      : hwnd(NULL), sourceWidth(0), sourceHeight(0), targetWidth(0),
        targetHeight(0), legacySurface(NULL), device(NULL), context(NULL),
        swapChain(NULL), renderTarget(NULL), sourceTexture(NULL),
        sceneTexture(NULL), depthTexture(NULL), sourceView(NULL),
        sceneView(NULL), sceneTarget(NULL), depthView(NULL), compositeVS(NULL),
        sceneVS(NULL), compositePS(NULL), scenePS(NULL), compositeSampler(NULL),
        compositeConstants(NULL), sceneConstants(NULL), sceneVertices(NULL),
        sceneLayout(NULL), sceneVertexCapacity(0), rasterizer(NULL), gamma(1),
        hardwareFrame(FALSE), sceneActive(FALSE), sceneOpen(FALSE),
        depthCompare(FALSE), depthWrite(FALSE), fogEnabled(FALSE),
        alphaTest(FALSE), blendMode(kD3D11LegacyBlendOpaque), fogColor(0) {
    ZeroMemory(samplers, sizeof(samplers));
    ZeroMemory(blendStates, sizeof(blendStates));
    ZeroMemory(depthStates, sizeof(depthStates));
    ZeroMemory(boundTextures, sizeof(boundTextures));
    ZeroMemory(boundSamplers, sizeof(boundSamplers));
  }
  void ReleaseQueuedCommands() {
    size_t i;
    for (i = 0; i < queuedCommands.size(); ++i) {
      ReleaseInterface(queuedCommands[i].textures[0]);
      ReleaseInterface(queuedCommands[i].textures[1]);
    }
    queuedCommands.clear();
    queuedVertices.clear();
  }
};

static cD3D11Presenter *g_pPresenter = NULL;
static HRESULT CompileShader(const char *s, const char *entry,
                             const char *target, ID3DBlob **blob) {
  ID3DBlob *errors = NULL;
  HRESULT hr = D3DCompile(s, strlen(s), NULL, NULL, NULL, entry, target,
                          D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, blob, &errors);
  if (FAILED(hr) && errors)
    OutputDebugStringA((const char *)errors->GetBufferPointer());
  ReleaseInterface(errors);
  return hr;
}
static HRESULT CreateDynamicBuffer(ID3D11Device *d, UINT bytes, UINT bind,
                                   ID3D11Buffer **out) {
  D3D11_BUFFER_DESC x;
  ZeroMemory(&x, sizeof(x));
  x.ByteWidth = bytes;
  x.Usage = D3D11_USAGE_DYNAMIC;
  x.BindFlags = bind;
  x.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  return d->CreateBuffer(&x, NULL, out);
}

cD3D11Presenter::cD3D11Presenter() : m_pImpl(new sImpl) {}
cD3D11Presenter::~cD3D11Presenter() {
  Stop();
  delete m_pImpl;
}
void cD3D11Presenter::Stop() {
  int i;
  if (!m_pImpl)
    return;
  if (g_pPresenter == this)
    g_pPresenter = NULL;
  m_pImpl->ReleaseQueuedCommands();
  if (m_pImpl->context)
    m_pImpl->context->ClearState();
  for (i = 0; i < 4; ++i) {
    ReleaseInterface(m_pImpl->blendStates[i]);
    ReleaseInterface(m_pImpl->depthStates[i]);
    ReleaseInterface(m_pImpl->samplers[i]);
  }
  ReleaseInterface(m_pImpl->rasterizer);
  ReleaseInterface(m_pImpl->sceneConstants);
  ReleaseInterface(m_pImpl->sceneVertices);
  ReleaseInterface(m_pImpl->sceneLayout);
  ReleaseInterface(m_pImpl->scenePS);
  ReleaseInterface(m_pImpl->sceneVS);
  ReleaseInterface(m_pImpl->compositeConstants);
  ReleaseInterface(m_pImpl->compositeSampler);
  ReleaseInterface(m_pImpl->compositePS);
  ReleaseInterface(m_pImpl->compositeVS);
  ReleaseInterface(m_pImpl->depthView);
  ReleaseInterface(m_pImpl->depthTexture);
  ReleaseInterface(m_pImpl->sceneTarget);
  ReleaseInterface(m_pImpl->sceneView);
  ReleaseInterface(m_pImpl->sceneTexture);
  ReleaseInterface(m_pImpl->sourceView);
  ReleaseInterface(m_pImpl->sourceTexture);
  ReleaseInterface(m_pImpl->renderTarget);
  ReleaseInterface(m_pImpl->swapChain);
  ReleaseInterface(m_pImpl->context);
  ReleaseInterface(m_pImpl->device);
  ReleaseInterface(m_pImpl->legacySurface);
  m_pImpl->hwnd = NULL;
  m_pImpl->sourceWidth = m_pImpl->sourceHeight = 0;
  m_pImpl->targetWidth = m_pImpl->targetHeight = 0;
  m_pImpl->hardwareFrame = m_pImpl->sceneActive = m_pImpl->sceneOpen = FALSE;
  m_pImpl->sceneVertexCapacity = 0;
  m_pImpl->overlayPixels.clear();
  m_pImpl->depthCompare = m_pImpl->depthWrite = FALSE;
  m_pImpl->fogEnabled = m_pImpl->alphaTest = FALSE;
  m_pImpl->blendMode = kD3D11LegacyBlendOpaque;
  m_pImpl->fogColor = 0;
  ZeroMemory(m_pImpl->boundTextures, sizeof(m_pImpl->boundTextures));
  ZeroMemory(m_pImpl->boundSamplers, sizeof(m_pImpl->boundSamplers));
}
HRESULT cD3D11Presenter::CreateRenderTarget(sImpl *p) {
  ID3D11Texture2D *b = NULL;
  HRESULT hr =
      p->swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&b);
  if (SUCCEEDED(hr))
    hr = p->device->CreateRenderTargetView(b, NULL, &p->renderTarget);
  ReleaseInterface(b);
  return hr;
}

BOOL cD3D11Presenter::Start(HWND hwnd, DWORD sw, DWORD sh,
                            IDirectDrawSurface *surface) {
  static const char cvs[] =
      "struct O{float4 p:SV_Position;float2 u:TEXCOORD0;};O main(uint "
      "i:SV_VertexID){O o;float2 "
      "u=float2((i<<1)&2,i&2);o.p=float4(u.x*2-1,1-u.y*2,0,1);o.u=u;return o;}";
  static const char cps[] =
      "cbuffer C:register(b0){float g;float hs;float2 z;}Texture2D "
      "ui:register(t0);Texture2D sc:register(t1);SamplerState "
      "s:register(s0);float4 main(float4 p:SV_Position,float2 "
      "u:TEXCOORD0):SV_Target{float4 a=ui.Sample(s,u);float3 "
      "c=a.rgb;if(hs>.5)c=lerp(sc.Sample(s,u).rgb,a.rgb,a.a);return "
      "float4(pow(saturate(c),g),1);}";
  static const char svs[] =
      "cbuffer C:register(b0){float w;float h;float fe;float ut;float4 "
      "fc;float at;float3 pad;}struct I{float4 p:POSITION;float4 "
      "c:COLOR0;float f:FOG;float2 "
      "u:TEXCOORD0;float2 v:TEXCOORD1;};struct O{float4 "
      "p:SV_Position;noperspective float4 "
      "c:COLOR0;noperspective float f:FOG;float2 u:TEXCOORD0;float2 "
      "v:TEXCOORD1;};O main(I "
      "i){O o;float "
      "q=1/max(i.p.w,.000001);o.p=float4((i.p.x/w*2-1)*q,(1-i.p.y/"
      "h*2)*q,i.p.z*q,q);o.c=i.c;o.f=i.f;o.u=i.u;o.v=i.v;return o;}";
  static const char sps[] =
      "cbuffer C:register(b0){float w;float h;float fe;float ut;float4 "
      "fc;float at;float3 pad;}Texture2D a:register(t0);Texture2D "
      "b:register(t1);SamplerState "
      "x:register(s0);SamplerState y:register(s1);struct I{float4 "
      "p:SV_Position;noperspective float4 c:COLOR0;noperspective float "
      "f:FOG;float2 u:TEXCOORD0;float2 "
      "v:TEXCOORD1;};float4 main(I i):SV_Target{float4 "
      "c=i.c*a.Sample(x,i.u);if(ut>.5)c*=b.Sample(y,i.v);if(at>.5)clip(c.a-.5);"
      "if(fe>.5)c.rgb=lerp("
      "fc.rgb,c.rgb,saturate(i.f));return c;}";
  RECT rc;
  DXGI_SWAP_CHAIN_DESC sd;
  D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                D3D_FEATURE_LEVEL_10_0},
                    got;
  D3D11_TEXTURE2D_DESC td;
  D3D11_SUBRESOURCE_DATA sourceData;
  D3D11_SAMPLER_DESC samp;
  D3D11_BUFFER_DESC bd;
  D3D11_BLEND_DESC bl;
  D3D11_DEPTH_STENCIL_DESC ds;
  D3D11_RASTERIZER_DESC rs;
  ID3DBlob *vb = NULL, *pb = NULL;
  HRESULT hr;
  int i;
  D3D11_INPUT_ELEMENT_DESC il[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0,
       D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16,
       D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"FOG", 0, DXGI_FORMAT_R32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 36,
       D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, 44,
       D3D11_INPUT_PER_VERTEX_DATA, 0}};
  Stop();
  if (!hwnd || !sw || !sh || !surface)
    return FALSE;
  m_pImpl->overlayPixels.assign(sw * sh, 0);
  // A typical mission produces tens of thousands of expanded triangle
  // vertices.  Retain that storage between frames instead of repeatedly
  // growing the command streams during the first frames of play.
  m_pImpl->queuedVertices.reserve(65536);
  m_pImpl->queuedCommands.reserve(2048);
  GetClientRect(hwnd, &rc);
  ZeroMemory(&sd, sizeof(sd));
  sd.BufferDesc.Width = rc.right - rc.left;
  sd.BufferDesc.Height = rc.bottom - rc.top;
  sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  sd.SampleDesc.Count = 1;
  sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  sd.BufferCount = 1;
  sd.OutputWindow = hwnd;
  sd.Windowed = TRUE;
  sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
  hr = D3D11CreateDeviceAndSwapChain(
      NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
      levels, 3, D3D11_SDK_VERSION, &sd, &m_pImpl->swapChain, &m_pImpl->device,
      &got, &m_pImpl->context);
  if (FAILED(hr))
    hr = D3D11CreateDeviceAndSwapChain(
        NULL, D3D_DRIVER_TYPE_WARP, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        levels, 3, D3D11_SDK_VERSION, &sd, &m_pImpl->swapChain,
        &m_pImpl->device, &got, &m_pImpl->context);
  if (FAILED(hr) || FAILED(CreateRenderTarget(m_pImpl)))
    goto fail;
  ZeroMemory(&td, sizeof(td));
  td.Width = sw;
  td.Height = sh;
  td.MipLevels = td.ArraySize = 1;
  td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  td.SampleDesc.Count = 1;
  td.Usage = D3D11_USAGE_DEFAULT;
  td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  td.CPUAccessFlags = 0;
  ZeroMemory(&sourceData, sizeof(sourceData));
  sourceData.pSysMem = &m_pImpl->overlayPixels[0];
  sourceData.SysMemPitch = sw * sizeof(DWORD);
  if (FAILED(m_pImpl->device->CreateTexture2D(&td, &sourceData,
                                              &m_pImpl->sourceTexture)) ||
      FAILED(m_pImpl->device->CreateShaderResourceView(
          m_pImpl->sourceTexture, NULL, &m_pImpl->sourceView)))
    goto fail;
  td.Usage = D3D11_USAGE_DEFAULT;
  td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
  td.CPUAccessFlags = 0;
  if (FAILED(m_pImpl->device->CreateTexture2D(&td, NULL,
                                              &m_pImpl->sceneTexture)) ||
      FAILED(m_pImpl->device->CreateRenderTargetView(
          m_pImpl->sceneTexture, NULL, &m_pImpl->sceneTarget)) ||
      FAILED(m_pImpl->device->CreateShaderResourceView(
          m_pImpl->sceneTexture, NULL, &m_pImpl->sceneView)))
    goto fail;
  td.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
  td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
  if (FAILED(m_pImpl->device->CreateTexture2D(&td, NULL,
                                              &m_pImpl->depthTexture)) ||
      FAILED(m_pImpl->device->CreateDepthStencilView(
          m_pImpl->depthTexture, NULL, &m_pImpl->depthView)))
    goto fail;
  if (FAILED(CompileShader(cvs, "main", "vs_4_0", &vb)) ||
      FAILED(m_pImpl->device->CreateVertexShader(vb->GetBufferPointer(),
                                                 vb->GetBufferSize(), NULL,
                                                 &m_pImpl->compositeVS)))
    goto fail;
  ReleaseInterface(vb);
  if (FAILED(CompileShader(cps, "main", "ps_4_0", &pb)) ||
      FAILED(m_pImpl->device->CreatePixelShader(pb->GetBufferPointer(),
                                                pb->GetBufferSize(), NULL,
                                                &m_pImpl->compositePS)))
    goto fail;
  ReleaseInterface(pb);
  if (FAILED(CompileShader(svs, "main", "vs_4_0", &vb)) ||
      FAILED(m_pImpl->device->CreateVertexShader(vb->GetBufferPointer(),
                                                 vb->GetBufferSize(), NULL,
                                                 &m_pImpl->sceneVS)) ||
      FAILED(m_pImpl->device->CreateInputLayout(il, 5, vb->GetBufferPointer(),
                                                vb->GetBufferSize(),
                                                &m_pImpl->sceneLayout)))
    goto fail;
  ReleaseInterface(vb);
  if (FAILED(CompileShader(sps, "main", "ps_4_0", &pb)) ||
      FAILED(m_pImpl->device->CreatePixelShader(pb->GetBufferPointer(),
                                                pb->GetBufferSize(), NULL,
                                                &m_pImpl->scenePS)))
    goto fail;
  ReleaseInterface(pb);
  ZeroMemory(&samp, sizeof(samp));
  samp.MaxLOD = D3D11_FLOAT32_MAX;
  for (i = 0; i < 4; ++i) {
    samp.Filter = (i & 1) ? D3D11_FILTER_MIN_MAG_MIP_LINEAR
                          : D3D11_FILTER_MIN_MAG_MIP_POINT;
    samp.AddressU = samp.AddressV = samp.AddressW =
        (i & 2) ? D3D11_TEXTURE_ADDRESS_WRAP : D3D11_TEXTURE_ADDRESS_CLAMP;
    if (FAILED(
            m_pImpl->device->CreateSamplerState(&samp, &m_pImpl->samplers[i])))
      goto fail;
  }
  samp.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
  samp.AddressU = samp.AddressV = samp.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
  if (FAILED(m_pImpl->device->CreateSamplerState(&samp,
                                                 &m_pImpl->compositeSampler)))
    goto fail;
  m_pImpl->boundSamplers[0] = m_pImpl->samplers[1];
  m_pImpl->boundSamplers[1] = m_pImpl->samplers[1];
  ZeroMemory(&bl, sizeof(bl));
  bl.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
  if (FAILED(m_pImpl->device->CreateBlendState(&bl, &m_pImpl->blendStates[0])))
    goto fail;
  bl.RenderTarget[0].BlendEnable = TRUE;
  bl.RenderTarget[0].BlendOp = bl.RenderTarget[0].BlendOpAlpha =
      D3D11_BLEND_OP_ADD;
  bl.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
  bl.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
  bl.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
  bl.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
  if (FAILED(m_pImpl->device->CreateBlendState(&bl, &m_pImpl->blendStates[1])))
    goto fail;
  bl.RenderTarget[0].SrcBlend = D3D11_BLEND_DEST_COLOR;
  bl.RenderTarget[0].DestBlend = D3D11_BLEND_ZERO;
  bl.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
  bl.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
  if (FAILED(m_pImpl->device->CreateBlendState(&bl, &m_pImpl->blendStates[2])))
    goto fail;
  bl.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
  bl.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
  bl.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
  bl.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
  if (FAILED(m_pImpl->device->CreateBlendState(&bl, &m_pImpl->blendStates[3])))
    goto fail;
  for (i = 0; i < 4; ++i) {
    ZeroMemory(&ds, sizeof(ds));
    ds.DepthEnable = i != 0;
    ds.DepthWriteMask =
        (i & 2) ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
    ds.DepthFunc =
        (i & 1) ? D3D11_COMPARISON_LESS_EQUAL : D3D11_COMPARISON_ALWAYS;
    if (FAILED(m_pImpl->device->CreateDepthStencilState(
            &ds, &m_pImpl->depthStates[i])))
      goto fail;
  }
  ZeroMemory(&rs, sizeof(rs));
  rs.FillMode = D3D11_FILL_SOLID;
  rs.CullMode = D3D11_CULL_NONE;
  rs.DepthClipEnable = TRUE;
  if (FAILED(m_pImpl->device->CreateRasterizerState(&rs, &m_pImpl->rasterizer)))
    goto fail;
  ZeroMemory(&bd, sizeof(bd));
  bd.Usage = D3D11_USAGE_DEFAULT;
  bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  bd.ByteWidth = sizeof(sCompositeConstants);
  if (FAILED(m_pImpl->device->CreateBuffer(&bd, NULL,
                                           &m_pImpl->compositeConstants)))
    goto fail;
  bd.ByteWidth = sizeof(sSceneConstants);
  if (FAILED(
          m_pImpl->device->CreateBuffer(&bd, NULL, &m_pImpl->sceneConstants)))
    goto fail;
  m_pImpl->sceneVertexCapacity = 4096;
  if (FAILED(CreateDynamicBuffer(
          m_pImpl->device,
          m_pImpl->sceneVertexCapacity * sizeof(sD3D11LegacyVertex),
          D3D11_BIND_VERTEX_BUFFER, &m_pImpl->sceneVertices)))
    goto fail;
  m_pImpl->hwnd = hwnd;
  m_pImpl->sourceWidth = sw;
  m_pImpl->sourceHeight = sh;
  m_pImpl->targetWidth = sd.BufferDesc.Width;
  m_pImpl->targetHeight = sd.BufferDesc.Height;
  m_pImpl->legacySurface = surface;
  surface->AddRef();
  g_pPresenter = this;
  return TRUE;
fail:
  ReleaseInterface(vb);
  ReleaseInterface(pb);
  Stop();
  return FALSE;
}

BOOL cD3D11Presenter::SetGamma(double g) {
  if (!m_pImpl || g <= 0)
    return FALSE;
  m_pImpl->gamma = (float)g;
  return TRUE;
}
static BYTE ExpandChannel(DWORD p, DWORD m) {
  DWORD s = 0, n;
  if (!m)
    return 0;
  while (!(m & 1)) {
    m >>= 1;
    ++s;
  }
  n = m;
  return (BYTE)((((p >> s) & n) * 255 + n / 2) / n);
}
BOOL cD3D11Presenter::BeginHardwareFrame() {
  D3D11_VIEWPORT v;
  ID3D11ShaderResourceView *nulls[2] = {NULL, NULL};
  float c[4] = {0, 0, 0, 1};
  if (!m_pImpl || !m_pImpl->device)
    return FALSE;
  if (!m_pImpl->hardwareFrame) {
    m_pImpl->ReleaseQueuedCommands();
    DDBLTFX f;
    ZeroMemory(&f, sizeof(f));
    f.dwSize = sizeof(f);
    if (m_pImpl->legacySurface)
      m_pImpl->legacySurface->Blt(NULL, NULL, NULL,
                                  DDBLT_COLORFILL | DDBLT_WAIT, &f);
    m_pImpl->context->PSSetShaderResources(0, 2, nulls);
    m_pImpl->context->ClearRenderTargetView(m_pImpl->sceneTarget, c);
    m_pImpl->context->ClearDepthStencilView(
        m_pImpl->depthView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1, 0);
    m_pImpl->hardwareFrame = TRUE;
    m_pImpl->sceneActive = TRUE;
  }
  ZeroMemory(&v, sizeof(v));
  v.Width = (float)m_pImpl->sourceWidth;
  v.Height = (float)m_pImpl->sourceHeight;
  v.MaxDepth = 1;
  m_pImpl->context->OMSetRenderTargets(1, &m_pImpl->sceneTarget,
                                       m_pImpl->depthView);
  m_pImpl->context->RSSetViewports(1, &v);
  m_pImpl->context->RSSetState(m_pImpl->rasterizer);
  m_pImpl->sceneOpen = TRUE;
  return TRUE;
}
void cD3D11Presenter::EndHardwareFrame() {
  if (m_pImpl) {
    FlushHardwareCommands();
    m_pImpl->sceneOpen = FALSE;
  }
}
void cD3D11Presenter::DeactivateScene() {
  if (!m_pImpl)
    return;
  m_pImpl->ReleaseQueuedCommands();
  m_pImpl->hardwareFrame = FALSE;
  m_pImpl->sceneActive = FALSE;
  m_pImpl->sceneOpen = FALSE;
}
void cD3D11Presenter::ClearDepth() {
  if (m_pImpl && m_pImpl->depthView) {
    FlushHardwareCommands();
    m_pImpl->context->ClearDepthStencilView(m_pImpl->depthView,
                                            D3D11_CLEAR_DEPTH, 1, 0);
  }
}
void cD3D11Presenter::SetDepth(BOOL c, BOOL w) {
  m_pImpl->depthCompare = c;
  m_pImpl->depthWrite = w;
}
void cD3D11Presenter::SetBlend(int m) {
  if (m >= 0 && m < 4)
    m_pImpl->blendMode = m;
}
void cD3D11Presenter::SetAlphaTest(BOOL enabled) {
  if (m_pImpl)
    m_pImpl->alphaTest = enabled;
}
void cD3D11Presenter::SetSampler(int l, BOOL w, BOOL s) {
  if (l >= 0 && l < 2)
    m_pImpl->boundSamplers[l] = m_pImpl->samplers[(w ? 2 : 0) | (s ? 1 : 0)];
}
void cD3D11Presenter::SetFog(BOOL e, DWORD c) {
  m_pImpl->fogEnabled = e;
  m_pImpl->fogColor = c;
}
void *cD3D11Presenter::CreateTexture(int w, int h, const void *p, int row) {
  sD3D11LegacyTexture *t = new sD3D11LegacyTexture;
  D3D11_TEXTURE2D_DESC d;
  D3D11_SUBRESOURCE_DATA x;
  ZeroMemory(&d, sizeof(d));
  d.Width = w;
  d.Height = h;
  d.MipLevels = d.ArraySize = 1;
  d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  d.SampleDesc.Count = 1;
  d.Usage = D3D11_USAGE_DEFAULT;
  d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  ZeroMemory(&x, sizeof(x));
  x.pSysMem = p;
  x.SysMemPitch = row;
  if (FAILED(m_pImpl->device->CreateTexture2D(&d, &x, &t->texture)) ||
      FAILED(m_pImpl->device->CreateShaderResourceView(t->texture, NULL,
                                                       &t->view))) {
    ReleaseInterface(t->view);
    ReleaseInterface(t->texture);
    delete t;
    return NULL;
  }
  t->width = w;
  t->height = h;
  return t;
}
BOOL cD3D11Presenter::UpdateTexture(void *h, int w, int he, const void *p,
                                    int row) {
  sD3D11LegacyTexture *t = (sD3D11LegacyTexture *)h;
  if (!t || t->width != w || t->height != he)
    return FALSE;
  FlushHardwareCommands();
  m_pImpl->context->UpdateSubresource(t->texture, 0, NULL, p, row, 0);
  return TRUE;
}
void cD3D11Presenter::DestroyTexture(void *h) {
  sD3D11LegacyTexture *t = (sD3D11LegacyTexture *)h;
  int i;
  if (!t)
    return;
  FlushHardwareCommands();
  for (i = 0; i < 2; ++i)
    if (m_pImpl->boundTextures[i] == t->view)
      m_pImpl->boundTextures[i] = NULL;
  ReleaseInterface(t->view);
  ReleaseInterface(t->texture);
  delete t;
}
void cD3D11Presenter::BindTexture(int l, void *h) {
  sD3D11LegacyTexture *t = (sD3D11LegacyTexture *)h;
  if (l >= 0 && l < 2)
    m_pImpl->boundTextures[l] = t ? t->view : NULL;
}
BOOL cD3D11Presenter::Draw(int prim, const sD3D11LegacyVertex *v, int n,
                           BOOL second) {
  sD3D11LegacyCommand cmd;
  int di = (m_pImpl->depthCompare ? 1 : 0) | (m_pImpl->depthWrite ? 2 : 0);
  if (!m_pImpl->sceneOpen || !v || n <= 0)
    return FALSE;
  ZeroMemory(&cmd, sizeof(cmd));
  cmd.firstVertex = (UINT)m_pImpl->queuedVertices.size();
  cmd.vertexCount = n;
  cmd.primitive = prim;
  cmd.blendMode = m_pImpl->blendMode;
  cmd.depthState = di;
  cmd.useSecondTexture = second;
  cmd.fogEnabled = m_pImpl->fogEnabled;
  cmd.alphaTest = m_pImpl->alphaTest;
  cmd.fogColor = m_pImpl->fogColor;
  cmd.textures[0] = m_pImpl->boundTextures[0];
  cmd.textures[1] = m_pImpl->boundTextures[1];
  cmd.samplers[0] = m_pImpl->boundSamplers[0];
  cmd.samplers[1] = m_pImpl->boundSamplers[1];
  m_pImpl->queuedVertices.insert(m_pImpl->queuedVertices.end(), v, v + n);
  if (!m_pImpl->queuedCommands.empty()) {
    sD3D11LegacyCommand &last = m_pImpl->queuedCommands.back();
    if (last.firstVertex + last.vertexCount == cmd.firstVertex &&
        last.primitive == cmd.primitive && last.blendMode == cmd.blendMode &&
        last.depthState == cmd.depthState &&
        last.useSecondTexture == cmd.useSecondTexture &&
        last.fogEnabled == cmd.fogEnabled && last.alphaTest == cmd.alphaTest &&
        last.fogColor == cmd.fogColor && last.textures[0] == cmd.textures[0] &&
        last.textures[1] == cmd.textures[1] &&
        last.samplers[0] == cmd.samplers[0] &&
        last.samplers[1] == cmd.samplers[1]) {
      last.vertexCount += n;
      return TRUE;
    }
  }
  if (cmd.textures[0])
    cmd.textures[0]->AddRef();
  if (cmd.textures[1])
    cmd.textures[1]->AddRef();
  m_pImpl->queuedCommands.push_back(cmd);
  return TRUE;
}

BOOL cD3D11Presenter::FlushHardwareCommands() {
  D3D11_MAPPED_SUBRESOURCE map;
  UINT stride = sizeof(sD3D11LegacyVertex), offset = 0;
  size_t i;
  int oldBlend = -1, oldDepth = -1, oldPrimitive = -1;
  ID3D11ShaderResourceView *oldTextures[2] = {NULL, NULL};
  ID3D11SamplerState *oldSamplers[2] = {NULL, NULL};
  BOOL constantsValid = FALSE, oldSecond = FALSE, oldFog = FALSE,
       oldAlphaTest = FALSE;
  DWORD oldFogColor = 0;
  float blendFactor[4] = {0, 0, 0, 0};
  if (!m_pImpl || m_pImpl->queuedCommands.empty())
    return TRUE;
  if (m_pImpl->queuedVertices.size() > m_pImpl->sceneVertexCapacity) {
    ReleaseInterface(m_pImpl->sceneVertices);
    m_pImpl->sceneVertexCapacity = (UINT)m_pImpl->queuedVertices.size() + 4096;
    if (FAILED(CreateDynamicBuffer(
            m_pImpl->device,
            m_pImpl->sceneVertexCapacity * sizeof(sD3D11LegacyVertex),
            D3D11_BIND_VERTEX_BUFFER, &m_pImpl->sceneVertices))) {
      m_pImpl->ReleaseQueuedCommands();
      return FALSE;
    }
  }
  if (FAILED(m_pImpl->context->Map(m_pImpl->sceneVertices, 0,
                                   D3D11_MAP_WRITE_DISCARD, 0, &map))) {
    m_pImpl->ReleaseQueuedCommands();
    return FALSE;
  }
  memcpy(map.pData, &m_pImpl->queuedVertices[0],
         m_pImpl->queuedVertices.size() * sizeof(sD3D11LegacyVertex));
  m_pImpl->context->Unmap(m_pImpl->sceneVertices, 0);
  m_pImpl->context->IASetInputLayout(m_pImpl->sceneLayout);
  m_pImpl->context->IASetVertexBuffers(0, 1, &m_pImpl->sceneVertices, &stride,
                                       &offset);
  m_pImpl->context->VSSetShader(m_pImpl->sceneVS, NULL, 0);
  m_pImpl->context->VSSetConstantBuffers(0, 1, &m_pImpl->sceneConstants);
  m_pImpl->context->PSSetShader(m_pImpl->scenePS, NULL, 0);
  m_pImpl->context->PSSetConstantBuffers(0, 1, &m_pImpl->sceneConstants);
  for (i = 0; i < m_pImpl->queuedCommands.size(); ++i) {
    const sD3D11LegacyCommand &cmd = m_pImpl->queuedCommands[i];
    if (!constantsValid || oldSecond != cmd.useSecondTexture ||
        oldFog != cmd.fogEnabled || oldAlphaTest != cmd.alphaTest ||
        oldFogColor != cmd.fogColor) {
      sSceneConstants c;
      ZeroMemory(&c, sizeof(c));
      c.width = (float)m_pImpl->sourceWidth;
      c.height = (float)m_pImpl->sourceHeight;
      c.fogEnabled = cmd.fogEnabled ? 1.0f : 0.0f;
      c.useTexture1 = cmd.useSecondTexture ? 1.0f : 0.0f;
      c.fogColor[0] = ((cmd.fogColor >> 16) & 255) / 255.0f;
      c.fogColor[1] = ((cmd.fogColor >> 8) & 255) / 255.0f;
      c.fogColor[2] = (cmd.fogColor & 255) / 255.0f;
      c.fogColor[3] = 1;
      c.alphaTest = cmd.alphaTest ? 1.0f : 0.0f;
      m_pImpl->context->UpdateSubresource(m_pImpl->sceneConstants, 0, NULL, &c,
                                          0, 0);
      constantsValid = TRUE;
      oldSecond = cmd.useSecondTexture;
      oldFog = cmd.fogEnabled;
      oldAlphaTest = cmd.alphaTest;
      oldFogColor = cmd.fogColor;
    }
    if (oldTextures[0] != cmd.textures[0] ||
        oldTextures[1] != cmd.textures[1]) {
      m_pImpl->context->PSSetShaderResources(0, 2, cmd.textures);
      oldTextures[0] = cmd.textures[0];
      oldTextures[1] = cmd.textures[1];
    }
    if (oldSamplers[0] != cmd.samplers[0] ||
        oldSamplers[1] != cmd.samplers[1]) {
      m_pImpl->context->PSSetSamplers(0, 2, cmd.samplers);
      oldSamplers[0] = cmd.samplers[0];
      oldSamplers[1] = cmd.samplers[1];
    }
    if (oldBlend != cmd.blendMode) {
      m_pImpl->context->OMSetBlendState(m_pImpl->blendStates[cmd.blendMode],
                                        blendFactor, ~0u);
      oldBlend = cmd.blendMode;
    }
    if (oldDepth != cmd.depthState) {
      m_pImpl->context->OMSetDepthStencilState(
          m_pImpl->depthStates[cmd.depthState], 0);
      oldDepth = cmd.depthState;
    }
    if (oldPrimitive != cmd.primitive) {
      m_pImpl->context->IASetPrimitiveTopology(
          cmd.primitive == kD3D11LegacyLines ? D3D11_PRIMITIVE_TOPOLOGY_LINELIST
          : cmd.primitive == kD3D11LegacyPoints
              ? D3D11_PRIMITIVE_TOPOLOGY_POINTLIST
              : D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
      oldPrimitive = cmd.primitive;
    }
    m_pImpl->context->Draw(cmd.vertexCount, cmd.firstVertex);
  }
  m_pImpl->ReleaseQueuedCommands();
  return TRUE;
}

BOOL cD3D11Presenter::Present(IDirectDrawSurface *surface, int x0, int y0,
                              int x1, int y1) {
  static DWORD opaque16[65536];
  static DWORD overlay16[65536];
  static DWORD cachedRedMask = 0, cachedGreenMask = 0, cachedBlueMask = 0;
  DDSURFACEDESC sd;
  D3D11_BOX uploadBox;
  D3D11_VIEWPORT v;
  RECT rc;
  DWORD cw, ch;
  int x, y;
  BOOL transparentOverlay;
  HRESULT hr;
  float black[4] = {0, 0, 0, 1}, sa, ca;
  sCompositeConstants c;
  ID3D11ShaderResourceView *views[2];
  if (!m_pImpl || !m_pImpl->device || !surface)
    return FALSE;
  x0 = max(0, min(x0, (int)m_pImpl->sourceWidth));
  y0 = max(0, min(y0, (int)m_pImpl->sourceHeight));
  x1 = max(x0, min(x1, (int)m_pImpl->sourceWidth));
  y1 = max(y0, min(y1, (int)m_pImpl->sourceHeight));
  if (x0 == x1 || y0 == y1)
    return TRUE;
  transparentOverlay = m_pImpl->hardwareFrame || m_pImpl->sceneActive;
  ZeroMemory(&sd, sizeof(sd));
  sd.dwSize = sizeof(sd);
  if (FAILED(surface->Lock(NULL, &sd, DDLOCK_READONLY | DDLOCK_WAIT, NULL)))
    return FALSE;
  if (sd.ddpfPixelFormat.dwRGBBitCount != 15 &&
      sd.ddpfPixelFormat.dwRGBBitCount != 16 &&
      sd.ddpfPixelFormat.dwRGBBitCount != 32) {
    surface->Unlock(NULL);
    return FALSE;
  }
  if (sd.ddpfPixelFormat.dwRGBBitCount != 32) {
    DWORD redMask = sd.ddpfPixelFormat.dwRBitMask;
    DWORD greenMask = sd.ddpfPixelFormat.dwGBitMask;
    DWORD blueMask = sd.ddpfPixelFormat.dwBBitMask;
    if (redMask != cachedRedMask || greenMask != cachedGreenMask ||
        blueMask != cachedBlueMask) {
      DWORD pixel;
      for (pixel = 0; pixel < 65536; ++pixel) {
        DWORD rgba = ExpandChannel(pixel, redMask) |
                     ((DWORD)ExpandChannel(pixel, greenMask) << 8) |
                     ((DWORD)ExpandChannel(pixel, blueMask) << 16) | 0xff000000;
        opaque16[pixel] = rgba;
        overlay16[pixel] = pixel ? rgba : 0;
      }
      cachedRedMask = redMask;
      cachedGreenMask = greenMask;
      cachedBlueMask = blueMask;
    }
    for (y = y0; y < y1; ++y) {
      const WORD *src =
          (const WORD *)((const BYTE *)sd.lpSurface + y * sd.lPitch);
      DWORD *dst = &m_pImpl->overlayPixels[y * m_pImpl->sourceWidth];
      const DWORD *table = transparentOverlay ? overlay16 : opaque16;
      for (x = x0; x < x1; ++x)
        dst[x] = table[src[x]];
    }
  } else {
    for (y = y0; y < y1; ++y) {
      const DWORD *src =
          (const DWORD *)((const BYTE *)sd.lpSurface + y * sd.lPitch);
      DWORD *dst = &m_pImpl->overlayPixels[y * m_pImpl->sourceWidth];
      for (x = x0; x < x1; ++x) {
        DWORD pixel = src[x];
        dst[x] = ((pixel >> 16) & 255) | (pixel & 0xff00) |
                 ((pixel & 255) << 16) |
                 ((transparentOverlay && !(pixel & 0xffffff)) ? 0 : 0xff000000);
      }
    }
  }
  surface->Unlock(NULL);
  uploadBox.left = x0;
  uploadBox.top = y0;
  uploadBox.front = 0;
  uploadBox.right = x1;
  uploadBox.bottom = y1;
  uploadBox.back = 1;
  m_pImpl->context->UpdateSubresource(
      m_pImpl->sourceTexture, 0, &uploadBox,
      &m_pImpl->overlayPixels[y0 * m_pImpl->sourceWidth + x0],
      m_pImpl->sourceWidth * sizeof(DWORD), 0);
  GetClientRect(m_pImpl->hwnd, &rc);
  cw = rc.right - rc.left;
  ch = rc.bottom - rc.top;
  if (!cw || !ch)
    return FALSE;
  if (cw != m_pImpl->targetWidth || ch != m_pImpl->targetHeight) {
    m_pImpl->context->OMSetRenderTargets(0, NULL, NULL);
    ReleaseInterface(m_pImpl->renderTarget);
    hr = m_pImpl->swapChain->ResizeBuffers(1, cw, ch, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr) || FAILED(CreateRenderTarget(m_pImpl)))
      return FALSE;
    m_pImpl->targetWidth = cw;
    m_pImpl->targetHeight = ch;
  }
  sa = (float)m_pImpl->sourceWidth / m_pImpl->sourceHeight;
  ca = (float)cw / ch;
  ZeroMemory(&v, sizeof(v));
  if (ca > sa) {
    v.Height = (float)ch;
    v.Width = v.Height * sa;
    v.TopLeftX = (cw - v.Width) * .5f;
  } else {
    v.Width = (float)cw;
    v.Height = v.Width / sa;
    v.TopLeftY = (ch - v.Height) * .5f;
  }
  v.MaxDepth = 1;
  m_pImpl->context->OMSetRenderTargets(1, &m_pImpl->renderTarget, NULL);
  m_pImpl->context->ClearRenderTargetView(m_pImpl->renderTarget, black);
  m_pImpl->context->RSSetViewports(1, &v);
  c.gamma = m_pImpl->gamma;
  c.hasScene = m_pImpl->sceneActive ? 1.0f : 0.0f;
  c.padding[0] = c.padding[1] = 0;
  m_pImpl->context->UpdateSubresource(m_pImpl->compositeConstants, 0, NULL, &c,
                                      0, 0);
  views[0] = m_pImpl->sourceView;
  views[1] = m_pImpl->sceneView;
  m_pImpl->context->IASetInputLayout(NULL);
  m_pImpl->context->IASetPrimitiveTopology(
      D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  m_pImpl->context->VSSetShader(m_pImpl->compositeVS, NULL, 0);
  m_pImpl->context->PSSetShader(m_pImpl->compositePS, NULL, 0);
  m_pImpl->context->PSSetConstantBuffers(0, 1, &m_pImpl->compositeConstants);
  m_pImpl->context->PSSetShaderResources(0, 2, views);
  m_pImpl->context->PSSetSamplers(0, 1, &m_pImpl->compositeSampler);
  m_pImpl->context->Draw(3, 0);
  // The engine owns frame pacing. Do not block each legacy display flush on
  // the desktop refresh interval.
  hr = m_pImpl->swapChain->Present(0, 0);
  m_pImpl->hardwareFrame = FALSE;
  return SUCCEEDED(hr);
}

extern "C" BOOL D3D11LegacyAvailable(void) { return g_pPresenter != NULL; }
extern "C" BOOL D3D11LegacyBeginFrame(void) {
  return g_pPresenter && g_pPresenter->BeginHardwareFrame();
}
extern "C" void D3D11LegacyEndFrame(void) {
  if (g_pPresenter)
    g_pPresenter->EndHardwareFrame();
}
extern "C" void D3D11LegacyDeactivateScene(void) {
  if (g_pPresenter)
    g_pPresenter->DeactivateScene();
}
extern "C" void D3D11LegacyClearDepth(void) {
  if (g_pPresenter)
    g_pPresenter->ClearDepth();
}
extern "C" void D3D11LegacySetDepth(BOOL c, BOOL w) {
  if (g_pPresenter)
    g_pPresenter->SetDepth(c, w);
}
extern "C" void D3D11LegacySetBlend(int m) {
  if (g_pPresenter)
    g_pPresenter->SetBlend(m);
}
extern "C" void D3D11LegacySetAlphaTest(BOOL e) {
  if (g_pPresenter)
    g_pPresenter->SetAlphaTest(e);
}
extern "C" void D3D11LegacySetSampler(int l, BOOL w, BOOL s) {
  if (g_pPresenter)
    g_pPresenter->SetSampler(l, w, s);
}
extern "C" void D3D11LegacySetFog(BOOL e, DWORD c) {
  if (g_pPresenter)
    g_pPresenter->SetFog(e, c);
}
extern "C" void *D3D11LegacyCreateTexture(int w, int h, const void *p, int r) {
  return g_pPresenter ? g_pPresenter->CreateTexture(w, h, p, r) : NULL;
}
extern "C" BOOL D3D11LegacyUpdateTexture(void *t, int w, int h, const void *p,
                                         int r) {
  return g_pPresenter && g_pPresenter->UpdateTexture(t, w, h, p, r);
}
extern "C" void D3D11LegacyDestroyTexture(void *t) {
  if (g_pPresenter)
    g_pPresenter->DestroyTexture(t);
}
extern "C" void D3D11LegacyBindTexture(int l, void *t) {
  if (g_pPresenter)
    g_pPresenter->BindTexture(l, t);
}
extern "C" BOOL D3D11LegacyDraw(int p, const sD3D11LegacyVertex *v, int n,
                                BOOL t) {
  return g_pPresenter && g_pPresenter->Draw(p, v, n, t);
}
