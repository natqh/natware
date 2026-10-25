#include "avatarmanager.h"
#include <imgui/imgui.h>
#include "../../../ext/imgui/stb_image.h"
#include <iostream>
#include <chrono>
#include <algorithm>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
AvatarManager::AvatarManager(ID3D11Device* device, ID3D11DeviceContext* context)
    : d3d_device(device), d3d_context(context) {
    if (d3d_device) {
        d3d_device->AddRef();
    }
    if (d3d_context) {
        d3d_context->AddRef();
    }

    last_rate_limit_time = std::chrono::steady_clock::time_point{};
}

AvatarManager::~AvatarManager() {
    {
        std::lock_guard<std::mutex> lock(download_mutex);
        for (auto& pair : pending_downloads) {
            if (pair.second.valid()) {
                try {
                    pair.second.wait_for(std::chrono::milliseconds(100));
                }
                catch (...) {
                }
            }
        }
        pending_downloads.clear();
    }

    for (auto& pair : texture_cache) {
        if (pair.second.srv) {
            pair.second.srv->Release();
        }
    }

    if (d3d_device) {
        d3d_device->Release();
    }
    if (d3d_context) {
        d3d_context->Release();
    }
}

void AvatarManager::requestAvatar(uint64_t userId) {
    std::string userIdStr = std::to_string(userId);
    requestAvatar(userIdStr);
}

void AvatarManager::requestAvatar(const std::string& userId) {
    if (userId.empty()) return;

    std::lock_guard<std::mutex> lock(cache_mutex);

    if (image_cache.find(userId) != image_cache.end()) {
        return;
    }

    if (avatar_states.find(userId) != avatar_states.end()) {
        AvatarState state = avatar_states[userId];
        if (state == AvatarState::Ready || state == AvatarState::Downloading || state == AvatarState::Blocked || state == AvatarState::Failed) {
            return;
        }
    }

    auto now = std::chrono::steady_clock::now();
    auto time_since_rate_limit = now - last_rate_limit_time;
    auto seconds_since = std::chrono::duration_cast<std::chrono::seconds>(time_since_rate_limit).count();

    if (last_rate_limit_time != std::chrono::steady_clock::time_point{} &&
        time_since_rate_limit < std::chrono::seconds(15)) {
        avatar_states[userId] = AvatarState::Failed;
        return;
    }

    avatar_states[userId] = AvatarState::Downloading;

    std::lock_guard<std::mutex> download_lock(download_mutex);
    if (pending_downloads.find(userId) == pending_downloads.end()) {
        pending_downloads[userId] = std::async(std::launch::async, [this, userId]() {
            return downloadAvatarSync(userId);
            });
    }
}

std::vector<uint8_t> AvatarManager::downloadImageFromUrl(const std::string& host, const std::string& path) {
    return {};
}

std::vector<uint8_t> AvatarManager::downloadAvatarSync(const std::string& userId) {
    return {};
}

ID3D11ShaderResourceView* AvatarManager::createTextureFromBytes(const std::vector<uint8_t>& imageData, int& width, int& height) {
    if (imageData.empty() || !d3d_device) {
        return nullptr;
    }

    int channels;
    unsigned char* pixels = stbi_load_from_memory(imageData.data(), static_cast<int>(imageData.size()),
        &width, &height, &channels, 4);

    if (!pixels) {
        return nullptr;
    }

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;

    D3D11_SUBRESOURCE_DATA subResource = {};
    subResource.pSysMem = pixels;
    subResource.SysMemPitch = width * 4;

    ID3D11Texture2D* texture = nullptr;
    HRESULT hr = d3d_device->CreateTexture2D(&desc, &subResource, &texture);

    stbi_image_free(pixels);

    if (FAILED(hr)) {
        return nullptr;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = desc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    ID3D11ShaderResourceView* srv = nullptr;
    hr = d3d_device->CreateShaderResourceView(texture, &srvDesc, &srv);

    texture->Release();

    if (FAILED(hr)) {
        return nullptr;
    }

    return srv;
}

void AvatarManager::processCompletedDownloads() {
    std::lock_guard<std::mutex> download_lock(download_mutex);

    int processed = 0;
    for (auto it = pending_downloads.begin(); it != pending_downloads.end() && processed < 2;) {
        if (it->second.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            std::string userId = it->first;

            try {
                auto imageData = it->second.get();

                std::lock_guard<std::mutex> cache_lock(cache_mutex);

                if (avatar_states[userId] == AvatarState::Blocked) {
                    it = pending_downloads.erase(it);
                    processed++;
                    continue;
                }

                if (!imageData.empty()) {
                    image_cache[userId] = imageData;

                    int width, height;
                    ID3D11ShaderResourceView* srv = createTextureFromBytes(imageData, width, height);

                    if (srv) {
                        AvatarTexture texture;
                        texture.srv = srv;
                        texture.width = width;
                        texture.height = height;
                        texture.loaded = true;
                        texture.last_accessed = std::chrono::steady_clock::now();

                        texture_cache[userId] = texture;
                        avatar_states[userId] = AvatarState::Ready;

                    }
                    else {
                        avatar_states[userId] = AvatarState::Failed;
                    }
                }
                else {
                    avatar_states[userId] = AvatarState::Failed;
                }

            }
            catch (const std::exception& e) {
                (void)e;
                std::lock_guard<std::mutex> cache_lock(cache_mutex);
                avatar_states[userId] = AvatarState::Failed;
            }

            it = pending_downloads.erase(it);
            processed++;
        }
        else {
            ++it;
        }
    }
}

void AvatarManager::cleanupOldTextures() {
    if (texture_cache.size() <= MAX_CACHE_SIZE) {
        return;
    }

    std::vector<std::pair<std::string, std::chrono::steady_clock::time_point>> access_times;
    for (const auto& pair : texture_cache) {
        access_times.emplace_back(pair.first, pair.second.last_accessed);
    }

    std::sort(access_times.begin(), access_times.end(),
        [](const auto& a, const auto& b) {
            return a.second < b.second;
        });

    size_t to_remove = texture_cache.size() - MAX_CACHE_SIZE + 10;
    for (size_t i = 0; i < to_remove && i < access_times.size(); ++i) {
        const std::string& userId = access_times[i].first;

        auto it = texture_cache.find(userId);
        if (it != texture_cache.end()) {
            if (it->second.srv) {
                it->second.srv->Release();
            }
            texture_cache.erase(it);
        }

        image_cache.erase(userId);
        avatar_states.erase(userId);
    }
}

void AvatarManager::update() {
    processCompletedDownloads();

    static int cleanup_counter = 0;
    if (++cleanup_counter >= 100) {
        cleanup_counter = 0;
        std::lock_guard<std::mutex> lock(cache_mutex);
        cleanupOldTextures();
    }
}

ImTextureID AvatarManager::getAvatarTexture(uint64_t userId) {
    std::string userIdStr = std::to_string(userId);
    return getAvatarTexture(userIdStr);
}

ImTextureID AvatarManager::getAvatarTexture(const std::string& userId) {
    if (userId.empty()) return ImTextureID{};

    std::lock_guard<std::mutex> lock(cache_mutex);

    auto it = texture_cache.find(userId);
    if (it != texture_cache.end() && it->second.loaded) {
        it->second.last_accessed = std::chrono::steady_clock::now();
        return reinterpret_cast<ImTextureID>(it->second.srv);
    }

    return ImTextureID{};
}


