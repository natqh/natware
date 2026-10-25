#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_PNG
#define STBI_NO_STDIO

#include <explorer.h>
#include <stb_image.h>
#include "../../dicons/dex_icons.h"
#include <render.h>

bool explorer::explorer_t::load_texture_from_memory(const unsigned char* data, unsigned int data_size, const std::string& name)
{
	if (!render || !render->detail || !render->detail->device)
	{
		return false;
	}

	int width, height, channels;
	unsigned char* image_data = stbi_load_from_memory(data, data_size, &width, &height, &channels, 4);

	if (!image_data)
	{
		return false;
	}

	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = width;
	desc.Height = height;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

	D3D11_SUBRESOURCE_DATA subResource = {};
	subResource.pSysMem = image_data;
	subResource.SysMemPitch = width * 4;

	ID3D11Texture2D* texture = nullptr;
	HRESULT hr = render->detail->device->CreateTexture2D(&desc, &subResource, &texture);

	stbi_image_free(image_data);

	if (FAILED(hr))
	{
		return false;
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;

	ID3D11ShaderResourceView* srv = nullptr;
	hr = render->detail->device->CreateShaderResourceView(texture, &srvDesc, &srv);
	texture->Release();

	if (FAILED(hr))
	{
		return false;
	}

	icon_texture_t tex;
	tex.texture = srv;
	tex.width = width;
	tex.height = height;
	icon_cache[name] = tex;

	return true;
}

#define LOAD_ICON(name) \
	do { \
		extern unsigned char name##_png[]; \
		extern unsigned int name##_png_len; \
		load_texture_from_memory(name##_png, name##_png_len, #name); \
	} while(0)

void explorer::explorer_t::load_all_icons()
{
	if (textures_loaded || !render || !render->detail || !render->detail->device)
	{
		return;
	}

	#include "#include ../../dicons/dex_icons_loader.inc"

	textures_loaded = true;
}

explorer::icon_texture_t* explorer::explorer_t::get_icon_for_classname(const std::string& classname)
{
	auto it = icon_cache.find(classname);
	if (it != icon_cache.end())
	{
		return &it->second;
	}

	auto fallback = icon_cache.find("Folder");
	if (fallback != icon_cache.end())
	{
		return &fallback->second;
	}

	return nullptr;
}

