#ifndef __D3D11LEGACY_H
#define __D3D11LEGACY_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sD3D11LegacyVertex {
  float x, y, z, rhw;
  float r, g, b, a;
  float fog;
  float u0, v0;
  float u1, v1;
} sD3D11LegacyVertex;

enum eD3D11LegacyPrimitive {
  kD3D11LegacyTriangles = 0,
  kD3D11LegacyLines = 1,
  kD3D11LegacyPoints = 2
};

enum eD3D11LegacyBlend {
  kD3D11LegacyBlendOpaque = 0,
  kD3D11LegacyBlendAlpha = 1,
  kD3D11LegacyBlendMultiply = 2,
  kD3D11LegacyBlendAdd = 3
};

BOOL D3D11LegacyAvailable(void);
BOOL D3D11LegacyBeginFrame(void);
void D3D11LegacyEndFrame(void);
void D3D11LegacyDeactivateScene(void);
void D3D11LegacyClearDepth(void);
void D3D11LegacySetDepth(BOOL compareEnabled, BOOL writeEnabled);
void D3D11LegacySetBlend(int blendMode);
void D3D11LegacySetAlphaTest(BOOL enabled);
void D3D11LegacySetSampler(int level, BOOL wrap, BOOL smooth);
void D3D11LegacySetFog(BOOL enabled, DWORD rgb);

void *D3D11LegacyCreateTexture(int width, int height, const void *rgba,
                               int rowBytes);
BOOL D3D11LegacyUpdateTexture(void *texture, int width, int height,
                              const void *rgba, int rowBytes);
void D3D11LegacyDestroyTexture(void *texture);
void D3D11LegacyBindTexture(int level, void *texture);

BOOL D3D11LegacyDraw(int primitive, const sD3D11LegacyVertex *vertices,
                     int vertexCount, BOOL useSecondTexture);

#ifdef __cplusplus
}
#endif

#endif
