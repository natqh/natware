#include "custom_entities.h"
#include "../cache.h"
#include <algorithm>
#include <imgui/imgui.h>
#include <cmath>
#include <memory/memory.h>
#include <Offsets/Offsets.hpp>
#include "../../settings.h"

namespace custom_entities
{
    inline void parse_path(const std::string& path, std::vector<std::string>& parts)
    {
        std::string current = path;
        size_t pos = 0;
        while ((pos = current.find('.')) != std::string::npos) {
            parts.push_back(current.substr(0, pos));
            current = current.substr(pos + 1);
        }
        parts.push_back(current);
    }

    inline rbx::instance_t navigate_path(const std::vector<std::string>& parts)
    {
        rbx::instance_t current = game::datamodel;
        for (const auto& part : parts) {
            current = current.find_first_child(part.c_str());
            if (!current.address) return {};
        }
        return current;
    }

    inline settings::custom_entities::custom_container_t* find_or_create_container(const std::string& path)
    {
        std::lock_guard<std::mutex> lock(containers_mtx);
        for (auto& cont : settings::custom_entities::containers) {
            if (cont.path == path) return &cont;
        }
        settings::custom_entities::containers.push_back({ path, path, true, {} });
        return &settings::custom_entities::containers.back();
    }

    inline void extract_entity_parts(settings::custom_entities::custom_entity_t& entity, rbx::instance_t& instance)
    {
        try
        {
            std::vector<rbx::part_t> parts = instance.get_children<rbx::part_t>();
            for (rbx::part_t& part : parts) {
                if (!part.address) continue;

                try
                {
                    std::string class_name = part.get_class_name();
                    if (class_name.find("Part") == std::string::npos) continue;

                    cache::part_data_t part_data;
                    part_data.part = { part.address };
                    part_data.name = part.get_name();

                    rbx::primitive_t primitive = part.get_primitive();
                    if (primitive.address) {
                        try
                        {
                            part_data.position = primitive.get_position();
                            part_data.size = primitive.get_size();
                            part_data.rotation = primitive.get_rotation();
                        }
                        catch (...)
                        {
                        }
                    }

                    entity.parts[part_data.name] = part_data;

                    if (part_data.name == "HumanoidRootPart") entity.root_part = part_data;
                    else if (part_data.name == "Torso") entity.root_part = part_data;
                    else if (part_data.name == "Head") entity.head = part_data;
                }
                catch (...)
                {
                    continue;
                }
            }
        }
        catch (...)
        {
        }
    }

    inline void calculate_distance(settings::custom_entities::custom_entity_t& entity)
    {
        try
        {
            if (!cache::cached_local_player.instance.address || entity.root_part.position.x == 0) return;

            std::lock_guard<std::mutex> lock(cache::mtx);
            auto hrp_it = cache::cached_local_player.parts.find("HumanoidRootPart");
            if (hrp_it == cache::cached_local_player.parts.end() || !hrp_it->second.address) return;

            rbx::part_t local_part_copy = hrp_it->second;
            rbx::primitive_t local_primitive = local_part_copy.get_primitive();
            if (!local_primitive.address) return;

            math::vector3 local_pos = local_primitive.get_position();
            math::vector3 diff = {
                entity.root_part.position.x - local_pos.x,
                entity.root_part.position.y - local_pos.y,
                entity.root_part.position.z - local_pos.z
            };
            entity.distance = std::sqrtf(diff.x * diff.x + diff.y * diff.y + diff.z * diff.z);
        }
        catch (...)
        {
            entity.distance = 0.0f;
        }
    }

    void scan_container(const std::string& path)
    {
        try
        {
            std::vector<std::string> path_parts;
            parse_path(path, path_parts);

            rbx::instance_t container = navigate_path(path_parts);
            if (!container.address) return;

            settings::custom_entities::custom_container_t* cont = nullptr;
            {
                std::lock_guard<std::mutex> lock(containers_mtx);
                for (auto& c : settings::custom_entities::containers) {
                    if (c.path == path) {
                        cont = &c;
                        break;
                    }
                }
                if (!cont) {
                    settings::custom_entities::containers.push_back({ path, path, true, {} });
                    cont = &settings::custom_entities::containers.back();
                }
            }

            if (!cont) return;

            std::vector<settings::custom_entities::custom_entity_t> new_entities;

            std::vector<rbx::instance_t> children = container.get_children<rbx::instance_t>();
            for (rbx::instance_t& child : children) {
                if (!child.address) continue;

                try
                {
                    settings::custom_entities::custom_entity_t entity;
                    entity.instance = child;
                    entity.name = child.get_name();
                    entity.container_path = path;
                    entity.enabled = true;

                    extract_entity_parts(entity, child);
                    calculate_distance(entity);

                    new_entities.push_back(entity);
                }
                catch (...)
                {
                    continue;
                }
            }

            // Look the container up again: loading a config can rebuild the list while we scan.
            {
                std::lock_guard<std::mutex> lock(containers_mtx);
                for (auto& c : settings::custom_entities::containers) {
                    if (c.path == path) {
                        c.entities = std::move(new_entities);
                        break;
                    }
                }
            }
        }
        catch (...)
        {
        }
    }

    void set_container(const std::string& path)
    {
        {
            std::lock_guard<std::mutex> lock(containers_mtx);
            settings::custom_entities::containers.clear();
            settings::custom_entities::containers.push_back({ path, path, true, {} });
        }
        scan_container(path);
    }

    void refresh_all_containers()
    {
        std::vector<std::string> paths_to_scan;
        {
            std::lock_guard<std::mutex> lock(containers_mtx);
            for (const auto& container : settings::custom_entities::containers) {
                if (container.enabled) {
                    paths_to_scan.push_back(container.path);
                }
            }
        }
        
        for (const auto& path : paths_to_scan) {
            scan_container(path);
        }
    }

    void update_containers()
    {
        static float last_refresh = 0.f;
        float current_time = static_cast<float>(ImGui::GetTime());

        // At most four times a second: each refresh walks the containers on the render thread.
        if (settings::custom_entities::auto_refresh &&
            (current_time - last_refresh) >= (std::max)(settings::custom_entities::refresh_rate, 0.25f)) {
            refresh_all_containers();
            last_refresh = current_time;
        }
    }
}
