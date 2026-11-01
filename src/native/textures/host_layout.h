#pragma once
// Host texture layout terms shared by the D3D12 and Vulkan backends.
//
// Texture loads are planned once in D3D12 footprint terms (subresource
// footprints, boxes, DXGI formats) and executed by either API. On Windows the
// real D3D12 definitions are used. Elsewhere only Vulkan executes the plans,
// so the same plain structures and the DXGI format values Native plans with
// are declared here with identical layout and values (DXGI enumeration and
// D3D12 structure definitions, as published in Microsoft's DirectX-Headers).
// The format is carried as data on that path, never interpreted.
// X360N_HOST_LAYOUT_D3D12 selects d3d12.h elsewhere too: syntax checks of the
// D3D12 backend off Windows with Microsoft's DirectX-Headers.
#if defined(_WIN32) || defined(X360N_HOST_LAYOUT_D3D12)
#include <d3d12.h>
#else
#include <cstdint>
enum DXGI_FORMAT : uint32_t {
  DXGI_FORMAT_UNKNOWN = 0,
  DXGI_FORMAT_R32G32B32A32_FLOAT = 2,
  DXGI_FORMAT_R32G32B32A32_UINT = 3,
  DXGI_FORMAT_R16G16B16A16_FLOAT = 10,
  DXGI_FORMAT_R16G16B16A16_UNORM = 11,
  DXGI_FORMAT_R16G16B16A16_SNORM = 13,
  DXGI_FORMAT_R32G32_FLOAT = 16,
  DXGI_FORMAT_R32G32_UINT = 17,
  DXGI_FORMAT_R10G10B10A2_UNORM = 24,
  DXGI_FORMAT_R8G8B8A8_UNORM = 28,
  DXGI_FORMAT_R8G8B8A8_SNORM = 31,
  DXGI_FORMAT_R16G16_FLOAT = 34,
  DXGI_FORMAT_R16G16_UNORM = 35,
  DXGI_FORMAT_R16G16_SNORM = 37,
  DXGI_FORMAT_R32_FLOAT = 41,
  DXGI_FORMAT_R32_UINT = 42,
  DXGI_FORMAT_R8G8_UNORM = 49,
  DXGI_FORMAT_R8G8_SNORM = 51,
  DXGI_FORMAT_R16_FLOAT = 54,
  DXGI_FORMAT_R16_UNORM = 56,
  DXGI_FORMAT_R16_UINT = 57,
  DXGI_FORMAT_R16_SNORM = 58,
  DXGI_FORMAT_R8_UNORM = 61,
  DXGI_FORMAT_R8_UINT = 62,
  DXGI_FORMAT_R8_SNORM = 63,
  DXGI_FORMAT_BC1_UNORM = 71,
  DXGI_FORMAT_BC2_UNORM = 74,
  DXGI_FORMAT_BC3_UNORM = 77,
  DXGI_FORMAT_BC4_UNORM = 80,
  DXGI_FORMAT_BC5_UNORM = 83,
};
struct D3D12_BOX { uint32_t left, top, front, right, bottom, back; };
struct D3D12_SUBRESOURCE_FOOTPRINT { DXGI_FORMAT Format; uint32_t Width, Height, Depth, RowPitch; };
struct D3D12_PLACED_SUBRESOURCE_FOOTPRINT { uint64_t Offset; D3D12_SUBRESOURCE_FOOTPRINT Footprint; };
inline constexpr uint32_t D3D12_TEXTURE_DATA_PITCH_ALIGNMENT = 256;
inline constexpr uint32_t D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT = 512;
#endif
