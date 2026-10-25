#pragma once
#include <d3d11.h>

ID3D11ShaderResourceView* D3D11CreateTextureFromBytes(
    ID3D11Device* pDevice,
    LPCVOID pSrcData,
    SIZE_T SrcDataSize
);
