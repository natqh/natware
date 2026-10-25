#include "texture.h"

// Decoded with stb_image (already in the project) rather than D3DX11, so the exe doesn't need
// d3dx11_43.dll from the old DirectX runtime.
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_NO_STDIO
#include "../../../ext/imgui/stb_image.h"

ID3D11ShaderResourceView* D3D11CreateTextureFromBytes(
    ID3D11Device* pDevice,
    LPCVOID pSrcData,
    SIZE_T SrcDataSize
)
{
    if (!pDevice || !pSrcData || SrcDataSize == 0)
        return nullptr;

    int width = 0, height = 0, channels = 0;
    unsigned char* pixels = stbi_load_from_memory(static_cast<const stbi_uc*>(pSrcData), static_cast<int>(SrcDataSize),
        &width, &height, &channels, 4);
    if (!pixels)
        return nullptr;

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = static_cast<UINT>(width);
    desc.Height = static_cast<UINT>(height);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA data = {};
    data.pSysMem = pixels;
    data.SysMemPitch = static_cast<UINT>(width) * 4;

    ID3D11Texture2D* texture = nullptr;
    ID3D11ShaderResourceView* view = nullptr;
    if (SUCCEEDED(pDevice->CreateTexture2D(&desc, &data, &texture)))
    {
        D3D11_SHADER_RESOURCE_VIEW_DESC srv = {};
        srv.Format = desc.Format;
        srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srv.Texture2D.MipLevels = 1;
        pDevice->CreateShaderResourceView(texture, &srv, &view);
        texture->Release();
    }
    stbi_image_free(pixels);
    return view;
}
