#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <shared_mutex>
#include <atomic>
#include <functional>
#include "../../../sdk/math/math.h"

namespace meshparser
{
	struct mesh_vertex
	{
		math::vector3 position;
		math::vector3 normal;
		math::vector2 uv;
		math::vector3 tangent;
		uint8_t r, g, b, a;
	};

	struct mesh_face
	{
		uint32_t a, b, c;
	};

	struct mesh_bounds
	{
		math::vector3 min;
		math::vector3 max;
		math::vector3 center;
		math::vector3 size;
		bool valid = false;
	};

	struct parsed_mesh
	{
		std::string version;
		uint32_t num_vertices;
		uint32_t num_faces;
		std::vector<mesh_vertex> vertices;
		std::vector<mesh_face> faces;
		std::vector<uint32_t> lods;

		mesh_bounds bounds;
		std::vector<math::vector3> hull_vertices;
	};

	struct character_mesh_cache
	{
		std::unordered_map<std::string, uint64_t> part_asset_ids;
		bool is_valid = false;
	};

	void initialize();

	std::string get_storage_directory();
	std::string get_cache_directory();
	bool ensure_storage_directory();
	bool parse_mesh_from_data(const std::vector<uint8_t>& data, parsed_mesh& mesh);
	bool download_mesh_from_asset_id(uint64_t asset_id, std::vector<uint8_t>& mesh_data);
	bool save_parsed_mesh_binary(const std::string& name, const parsed_mesh& mesh);
	bool load_parsed_mesh_binary(const std::string& name, parsed_mesh& mesh);

	uint64_t get_mesh_asset_id_from_part(uint64_t part_address);
	bool parse_and_cache_part_mesh(const std::string& part_name, uint64_t part_address, uint64_t asset_id, parsed_mesh& out_mesh);
	character_mesh_cache* get_or_create_character_cache(uint64_t character_address);
	void update_character_meshes(uint64_t character_address, const std::unordered_map<std::string, uint64_t>& parts);
	const parsed_mesh* get_cached_mesh(uint64_t character_address, const std::string& part_name);
}
