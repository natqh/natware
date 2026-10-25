#include "meshparser.h"

#include <windows.h>
#include <shlobj.h>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <cstring>
#include <unordered_set>
#include <unordered_map>
#include <thread>
#include <queue>
#include <condition_variable>
#include <chrono>
#include <atomic>
#include <shared_mutex>
#include "../../../sdk/math/math.h"
#include "../../../memory/memory.h"
#include "../../../sdk/sdk.h"
#include "../../../offsets/offsets.hpp"

#define STBI_ONLY_ZLIB
#define STBI_SUPPORT_ZLIB
#include "../../../ext/imgui/stb_image.h"

#pragma comment(lib, "shell32.lib")

namespace meshparser
{
	constexpr size_t THREAD_POOL_SIZE = 6;
	constexpr uint32_t BINARY_CACHE_MAGIC = 0x4D455348;
	constexpr uint32_t BINARY_CACHE_VERSION = 2;

	static void compute_mesh_bounds_and_hull(parsed_mesh& mesh);

	static uint8_t get_character_rig_type(uint64_t character_address)
	{
		if (character_address == 0)
			return 1;

		rbx::model_instance_t character(character_address);
		auto humanoid = character.find_first_child("Humanoid");
		if (humanoid.address)
		{
			rbx::humanoid_t humanoid_obj(humanoid.address);
			return humanoid_obj.get_rig_type();
		}

		return 1;
	}

	static std::string get_mesh_part_name(const std::string& part_name, uint8_t rig_type)
	{
		if (rig_type == 0)
		{
			if (part_name == "Left Arm") return "LeftUpperArm";
			if (part_name == "Right Arm") return "RightUpperArm";
			if (part_name == "Left Leg") return "LeftUpperLeg";
			if (part_name == "Right Leg") return "RightUpperLeg";
			if (part_name == "Torso") return "UpperTorso";
		}
		return part_name;
	}

	struct mesh_download_task
	{
		uint64_t asset_id;
		uint64_t character_address;
		std::string part_name;
	};

	static std::unordered_map<uint64_t, parsed_mesh> g_mesh_cache;
	static std::shared_mutex g_mesh_cache_mutex;

	static std::unordered_map<uint64_t, character_mesh_cache> g_character_caches;
	static std::mutex g_character_cache_mutex;

	static std::queue<mesh_download_task> g_task_queue;
	static std::mutex g_queue_mutex;
	static std::condition_variable g_queue_cv;

	static std::unordered_set<uint64_t> g_pending_assets;
	static std::mutex g_pending_mutex;

	static std::vector<std::thread> g_worker_threads;
	static std::atomic<bool> g_running{ false };

	static float read_float_le(const uint8_t* data, size_t& offset)
	{
		float value;
		std::memcpy(&value, data + offset, sizeof(float));
		offset += sizeof(float);
		return value;
	}

	static uint32_t read_uint32_le(const uint8_t* data, size_t& offset)
	{
		uint32_t value;
		std::memcpy(&value, data + offset, sizeof(uint32_t));
		offset += sizeof(uint32_t);
		return value;
	}

	static uint16_t read_uint16_le(const uint8_t* data, size_t& offset)
	{
		uint16_t value;
		std::memcpy(&value, data + offset, sizeof(uint16_t));
		offset += sizeof(uint16_t);
		return value;
	}

	static uint8_t read_uint8(const uint8_t* data, size_t& offset)
	{
		return data[offset++];
	}

	static int8_t read_int8(const uint8_t* data, size_t& offset)
	{
		return static_cast<int8_t>(data[offset++]);
	}

	std::string get_storage_directory()
	{
		char appdata_path[MAX_PATH];
		if (SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, SHGFP_TYPE_CURRENT, appdata_path) == S_OK)
		{
			std::string path = std::string(appdata_path) + "\\Turtle.Club\\Storage";
			return path;
		}
		return "";
	}

	std::string get_cache_directory()
	{
		char appdata_path[MAX_PATH];
		if (SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, SHGFP_TYPE_CURRENT, appdata_path) == S_OK)
		{
			std::string path = std::string(appdata_path) + "\\Turtle.Club\\Storage\\Cache";
			return path;
		}
		return "";
	}

	std::string get_mesh_path(const std::string& name)
	{
		std::string dir = get_storage_directory();
		if (dir.empty())
			return "";

		std::string filename = name;
		if (filename.find(".mesh") == std::string::npos && filename.find(".json") == std::string::npos)
			filename += ".mesh";

		return dir + "\\" + filename;
	}

	bool ensure_storage_directory()
	{
		std::string dir = get_storage_directory();
		if (dir.empty())
			return false;

		try
		{
			(void)std::filesystem::create_directories(dir);
			return true;
		}
		catch (...)
		{
			return false;
		}
	}

	bool ensure_cache_directory()
	{
		std::string dir = get_cache_directory();
		if (dir.empty())
			return false;

		try
		{
			(void)std::filesystem::create_directories(dir);
			return true;
		}
		catch (...)
		{
			return false;
		}
	}

	static std::string get_mesh_version(const std::vector<uint8_t>& data)
	{
		if (data.size() < 12)
			return "";

		std::string version_str(reinterpret_cast<const char*>(data.data()), 12);
		if (version_str.substr(0, 8) != "version ")
			return "";

		return version_str.substr(8);
	}

	static bool parse_mesh_v1(const std::vector<uint8_t>& data, size_t offset, parsed_mesh& mesh, float scale = 0.5f, bool invert_uv = true)
	{
		std::string data_str(reinterpret_cast<const char*>(data.data() + offset), data.size() - offset);

		size_t newline_pos = data_str.find('\n');
		if (newline_pos == std::string::npos)
			return false;

		std::string num_faces_str = data_str.substr(0, newline_pos);
		uint32_t num_faces = static_cast<uint32_t>(std::stoi(num_faces_str));

		size_t start_pos = data_str.find('[');
		if (start_pos == std::string::npos)
			return false;

		std::vector<std::vector<float>> all_vectors;
		all_vectors.reserve(num_faces * 9);
		std::string remaining = data_str.substr(start_pos);

		size_t pos = 0;
		while (pos < remaining.length())
		{
			size_t bracket_start = remaining.find('[', pos);
			if (bracket_start == std::string::npos)
				break;

			size_t bracket_end = remaining.find(']', bracket_start);
			if (bracket_end == std::string::npos)
				break;

			std::string vec_str = remaining.substr(bracket_start + 1, bracket_end - bracket_start - 1);
			std::vector<float> vec;
			vec.reserve(3);
			std::istringstream iss(vec_str);
			std::string token;

			while (std::getline(iss, token, ','))
			{
				try
				{
					vec.push_back(std::stof(token));
				}
				catch (...)
				{
				}
			}

			if (vec.size() == 3)
				all_vectors.push_back(std::move(vec));

			pos = bracket_end + 1;
		}

		if (all_vectors.size() != num_faces * 9)
			return false;

		mesh.vertices.clear();
		mesh.vertices.reserve(all_vectors.size() / 3);
		mesh.faces.clear();
		mesh.faces.reserve(num_faces);

		for (size_t i = 0; i < all_vectors.size(); i += 3)
		{
			if (i + 2 >= all_vectors.size())
				break;

			mesh_vertex vertex;
			vertex.position = math::vector3(
				all_vectors[i][0] * scale,
				all_vectors[i][1] * scale,
				all_vectors[i][2] * scale
			);
			vertex.normal = math::vector3(
				all_vectors[i + 1][0],
				all_vectors[i + 1][1],
				all_vectors[i + 1][2]
			);
			float uv_y = all_vectors[i + 2][1];
			vertex.uv = math::vector2(
				all_vectors[i + 2][0],
				invert_uv ? (1.0f - uv_y) : uv_y
			);
			vertex.tangent = math::vector3(0, 0, 0);
			vertex.r = vertex.g = vertex.b = vertex.a = 255;

			mesh.vertices.push_back(vertex);
		}

		for (uint32_t i = 0; i < num_faces; i++)
		{
			mesh_face face;
			face.a = i * 3;
			face.b = i * 3 + 1;
			face.c = i * 3 + 2;
			mesh.faces.push_back(face);
		}

		mesh.num_vertices = static_cast<uint32_t>(mesh.vertices.size());
		mesh.num_faces = static_cast<uint32_t>(mesh.faces.size());
		return true;
	}

	static bool parse_mesh_v2(const std::vector<uint8_t>& data, size_t offset, parsed_mesh& mesh)
	{
		size_t pos = offset;

		if (pos + 12 > data.size())
			return false;

		uint16_t cb_size = read_uint16_le(data.data(), pos);
		if (cb_size != 12)
			return false;

		uint8_t cb_vertices_stride = read_uint8(data.data(), pos);
		uint8_t cb_face_stride = read_uint8(data.data(), pos);
		uint32_t num_vertices = read_uint32_le(data.data(), pos);
		uint32_t num_faces = read_uint32_le(data.data(), pos);

		if (num_vertices == 0 || num_faces == 0)
			return false;

		bool has_rgba = (cb_vertices_stride == 40);
		size_t vertex_size = cb_vertices_stride;

		mesh.vertices.clear();
		mesh.vertices.reserve(num_vertices);
		mesh.faces.clear();
		mesh.faces.reserve(num_faces);

		for (uint32_t i = 0; i < num_vertices; i++)
		{
			if (pos + vertex_size > data.size())
				return false;

			mesh_vertex vertex;
			vertex.position.x = read_float_le(data.data(), pos);
			vertex.position.y = read_float_le(data.data(), pos);
			vertex.position.z = read_float_le(data.data(), pos);
			vertex.normal.x = read_float_le(data.data(), pos);
			vertex.normal.y = read_float_le(data.data(), pos);
			vertex.normal.z = read_float_le(data.data(), pos);
			vertex.uv.x = read_float_le(data.data(), pos);
			vertex.uv.y = read_float_le(data.data(), pos);
			int8_t tx = read_int8(data.data(), pos);
			int8_t ty = read_int8(data.data(), pos);
			int8_t tz = read_int8(data.data(), pos);
			int8_t ts = read_int8(data.data(), pos);
			vertex.tangent.x = static_cast<float>(tx) / 127.0f;
			vertex.tangent.y = static_cast<float>(ty) / 127.0f;
			vertex.tangent.z = static_cast<float>(tz) / 127.0f;

			if (has_rgba)
			{
				vertex.r = read_uint8(data.data(), pos);
				vertex.g = read_uint8(data.data(), pos);
				vertex.b = read_uint8(data.data(), pos);
				vertex.a = read_uint8(data.data(), pos);
			}
			else
			{
				vertex.r = vertex.g = vertex.b = vertex.a = 255;
			}

			mesh.vertices.push_back(vertex);
		}

		for (uint32_t i = 0; i < num_faces; i++)
		{
			if (pos + 12 > data.size())
				return false;

			mesh_face face;
			face.a = read_uint32_le(data.data(), pos);
			face.b = read_uint32_le(data.data(), pos);
			face.c = read_uint32_le(data.data(), pos);
			mesh.faces.push_back(face);
		}

		mesh.num_vertices = num_vertices;
		mesh.num_faces = num_faces;
		return true;
	}

	static bool parse_mesh_v3(const std::vector<uint8_t>& data, size_t offset, parsed_mesh& mesh)
	{
		size_t pos = offset;

		if (pos + 16 > data.size())
			return false;

		uint16_t cb_size = read_uint16_le(data.data(), pos);
		if (cb_size != 16)
			return false;

		uint8_t cb_vertices_stride = read_uint8(data.data(), pos);
		uint8_t cb_face_stride = read_uint8(data.data(), pos);
		uint16_t sizeof_lod = read_uint16_le(data.data(), pos);
		uint16_t num_lods = read_uint16_le(data.data(), pos);
		uint32_t num_vertices = read_uint32_le(data.data(), pos);
		uint32_t num_faces = read_uint32_le(data.data(), pos);

		if (num_vertices == 0 || num_faces == 0)
			return false;

		mesh.vertices.clear();
		mesh.vertices.reserve(num_vertices);
		mesh.faces.clear();
		mesh.faces.reserve(num_faces);
		mesh.lods.clear();

		for (uint32_t i = 0; i < num_vertices; i++)
		{
			if (pos + 40 > data.size())
				return false;

			mesh_vertex vertex;
			vertex.position.x = read_float_le(data.data(), pos);
			vertex.position.y = read_float_le(data.data(), pos);
			vertex.position.z = read_float_le(data.data(), pos);
			vertex.normal.x = read_float_le(data.data(), pos);
			vertex.normal.y = read_float_le(data.data(), pos);
			vertex.normal.z = read_float_le(data.data(), pos);
			vertex.uv.x = read_float_le(data.data(), pos);
			vertex.uv.y = read_float_le(data.data(), pos);
			int8_t tx = read_int8(data.data(), pos);
			int8_t ty = read_int8(data.data(), pos);
			int8_t tz = read_int8(data.data(), pos);
			int8_t ts = read_int8(data.data(), pos);
			vertex.tangent.x = static_cast<float>(tx) / 127.0f;
			vertex.tangent.y = static_cast<float>(ty) / 127.0f;
			vertex.tangent.z = static_cast<float>(tz) / 127.0f;
			vertex.r = read_uint8(data.data(), pos);
			vertex.g = read_uint8(data.data(), pos);
			vertex.b = read_uint8(data.data(), pos);
			vertex.a = read_uint8(data.data(), pos);

			mesh.vertices.push_back(vertex);
		}

		for (uint32_t i = 0; i < num_faces; i++)
		{
			if (pos + 12 > data.size())
				return false;

			mesh_face face;
			face.a = read_uint32_le(data.data(), pos);
			face.b = read_uint32_le(data.data(), pos);
			face.c = read_uint32_le(data.data(), pos);
			mesh.faces.push_back(face);
		}

		for (uint16_t i = 0; i < num_lods; i++)
		{
			if (pos + 4 > data.size())
				break;

			mesh.lods.push_back(read_uint32_le(data.data(), pos));
		}

		if (mesh.lods.size() > 1 && mesh.lods[1] < mesh.faces.size())
		{
			mesh.faces.resize(mesh.lods[1]);
		}

		mesh.num_vertices = num_vertices;
		mesh.num_faces = static_cast<uint32_t>(mesh.faces.size());
		return true;
	}

	static bool parse_mesh_v4(const std::vector<uint8_t>& data, size_t offset, parsed_mesh& mesh)
	{
		size_t pos = offset;

		if (pos + 24 > data.size())
			return false;

		uint16_t sizeof_mesh_header = read_uint16_le(data.data(), pos);
		if (sizeof_mesh_header != 24)
			return false;

		uint16_t lod_type = read_uint16_le(data.data(), pos);
		uint32_t num_verts = read_uint32_le(data.data(), pos);
		uint32_t num_faces = read_uint32_le(data.data(), pos);
		uint16_t num_lods = read_uint16_le(data.data(), pos);
		uint16_t num_bones = read_uint16_le(data.data(), pos);
		uint32_t sizeof_bone_names_buffer = read_uint32_le(data.data(), pos);
		uint16_t num_subsets = read_uint16_le(data.data(), pos);
		uint8_t num_high_quality_lods = read_uint8(data.data(), pos);
		uint8_t unused = read_uint8(data.data(), pos);

		if (num_verts == 0 || num_faces == 0)
			return false;

		mesh.vertices.clear();
		mesh.vertices.reserve(num_verts);
		mesh.faces.clear();
		mesh.faces.reserve(num_faces);
		mesh.lods.clear();

		for (uint32_t i = 0; i < num_verts; i++)
		{
			if (pos + 40 > data.size())
				return false;

			mesh_vertex vertex;
			vertex.position.x = read_float_le(data.data(), pos);
			vertex.position.y = read_float_le(data.data(), pos);
			vertex.position.z = read_float_le(data.data(), pos);
			vertex.normal.x = read_float_le(data.data(), pos);
			vertex.normal.y = read_float_le(data.data(), pos);
			vertex.normal.z = read_float_le(data.data(), pos);
			vertex.uv.x = read_float_le(data.data(), pos);
			vertex.uv.y = read_float_le(data.data(), pos);
			int8_t tx = read_int8(data.data(), pos);
			int8_t ty = read_int8(data.data(), pos);
			int8_t tz = read_int8(data.data(), pos);
			int8_t ts = read_int8(data.data(), pos);
			vertex.tangent.x = static_cast<float>(tx) / 127.0f;
			vertex.tangent.y = static_cast<float>(ty) / 127.0f;
			vertex.tangent.z = static_cast<float>(tz) / 127.0f;
			vertex.r = read_uint8(data.data(), pos);
			vertex.g = read_uint8(data.data(), pos);
			vertex.b = read_uint8(data.data(), pos);
			vertex.a = read_uint8(data.data(), pos);

			mesh.vertices.push_back(vertex);
		}

		if (num_bones > 0)
		{
			pos += num_verts * 8;
		}

		for (uint32_t i = 0; i < num_faces; i++)
		{
			if (pos + 12 > data.size())
				return false;

			mesh_face face;
			face.a = read_uint32_le(data.data(), pos);
			face.b = read_uint32_le(data.data(), pos);
			face.c = read_uint32_le(data.data(), pos);
			mesh.faces.push_back(face);
		}

		for (uint16_t i = 0; i < num_lods; i++)
		{
			if (pos + 4 > data.size())
				break;

			mesh.lods.push_back(read_uint32_le(data.data(), pos));
		}

		if (mesh.lods.size() > 1 && mesh.lods[1] < mesh.faces.size())
		{
			mesh.faces.resize(mesh.lods[1]);
		}

		mesh.num_vertices = num_verts;
		mesh.num_faces = static_cast<uint32_t>(mesh.faces.size());
		return true;
	}

	static bool parse_mesh_v5(const std::vector<uint8_t>& data, size_t offset, parsed_mesh& mesh)
	{
		size_t pos = offset;

		if (pos + 32 > data.size())
			return false;

		uint16_t sizeof_mesh_header = read_uint16_le(data.data(), pos);
		if (sizeof_mesh_header != 32)
			return false;

		uint16_t lod_type = read_uint16_le(data.data(), pos);
		uint32_t num_verts = read_uint32_le(data.data(), pos);
		uint32_t num_faces = read_uint32_le(data.data(), pos);
		uint16_t num_lods = read_uint16_le(data.data(), pos);
		uint16_t num_bones = read_uint16_le(data.data(), pos);
		uint32_t sizeof_bone_names_buffer = read_uint32_le(data.data(), pos);
		uint16_t num_subsets = read_uint16_le(data.data(), pos);
		uint8_t num_high_quality_lods = read_uint8(data.data(), pos);
		uint8_t unused_padding = read_uint8(data.data(), pos);
		uint32_t facs_data_format = read_uint32_le(data.data(), pos);
		uint32_t facs_data_size = read_uint32_le(data.data(), pos);

		if (num_verts == 0 || num_faces == 0)
			return false;

		mesh.vertices.clear();
		mesh.vertices.reserve(num_verts);
		mesh.faces.clear();
		mesh.faces.reserve(num_faces);
		mesh.lods.clear();

		for (uint32_t i = 0; i < num_verts; i++)
		{
			if (pos + 40 > data.size())
				return false;

			mesh_vertex vertex;
			vertex.position.x = read_float_le(data.data(), pos);
			vertex.position.y = read_float_le(data.data(), pos);
			vertex.position.z = read_float_le(data.data(), pos);
			vertex.normal.x = read_float_le(data.data(), pos);
			vertex.normal.y = read_float_le(data.data(), pos);
			vertex.normal.z = read_float_le(data.data(), pos);
			vertex.uv.x = read_float_le(data.data(), pos);
			vertex.uv.y = read_float_le(data.data(), pos);
			int8_t tx = read_int8(data.data(), pos);
			int8_t ty = read_int8(data.data(), pos);
			int8_t tz = read_int8(data.data(), pos);
			int8_t ts = read_int8(data.data(), pos);
			vertex.tangent.x = static_cast<float>(tx) / 127.0f;
			vertex.tangent.y = static_cast<float>(ty) / 127.0f;
			vertex.tangent.z = static_cast<float>(tz) / 127.0f;
			vertex.r = read_uint8(data.data(), pos);
			vertex.g = read_uint8(data.data(), pos);
			vertex.b = read_uint8(data.data(), pos);
			vertex.a = read_uint8(data.data(), pos);

			mesh.vertices.push_back(vertex);
		}

		if (num_bones > 0)
		{
			pos += num_verts * 8;
		}

		for (uint32_t i = 0; i < num_faces; i++)
		{
			if (pos + 12 > data.size())
				return false;

			mesh_face face;
			face.a = read_uint32_le(data.data(), pos);
			face.b = read_uint32_le(data.data(), pos);
			face.c = read_uint32_le(data.data(), pos);
			mesh.faces.push_back(face);
		}

		for (uint16_t i = 0; i < num_lods; i++)
		{
			if (pos + 4 > data.size())
				break;

			mesh.lods.push_back(read_uint32_le(data.data(), pos));
		}

		if (mesh.lods.size() > 1 && mesh.lods[1] < mesh.faces.size())
		{
			mesh.faces.resize(mesh.lods[1]);
		}

		pos += num_bones * 60;
		pos += sizeof_bone_names_buffer;
		pos += num_subsets * 72;
		pos += facs_data_size;

		mesh.num_vertices = num_verts;
		mesh.num_faces = static_cast<uint32_t>(mesh.faces.size());
		return true;
	}

	bool parse_mesh(const std::string& file_path, parsed_mesh& mesh)
	{
		std::ifstream file(file_path, std::ios::binary);
		if (!file.is_open())
			return false;

		file.seekg(0, std::ios::end);
		size_t file_size = file.tellg();
		file.seekg(0, std::ios::beg);

		std::vector<uint8_t> data(file_size);
		file.read(reinterpret_cast<char*>(data.data()), file_size);
		file.close();

		return parse_mesh_from_data(data, mesh);
	}

	bool parse_mesh_from_data(const std::vector<uint8_t>& data, parsed_mesh& mesh)
	{
		if (data.size() < 12)
			return false;

		std::string version = get_mesh_version(data);
		if (version.empty())
			return false;

		mesh.version = version;

		size_t offset = 0;
		for (size_t i = 0; i < data.size(); i++)
		{
			if (data[i] == '\n')
			{
				offset = i + 1;
				break;
			}
		}

		bool success = false;
		if (version == "1.00")
			success = parse_mesh_v1(data, offset, mesh, 0.5f, true);
		else if (version == "1.01")
			success = parse_mesh_v1(data, offset, mesh, 1.0f, false);
		else if (version == "2.00")
			success = parse_mesh_v2(data, offset, mesh);
		else if (version == "3.00" || version == "3.01")
			success = parse_mesh_v3(data, offset, mesh);
		else if (version == "4.00" || version == "4.01")
			success = parse_mesh_v4(data, offset, mesh);
		else if (version == "5.00" || version == "5.01" || version == "6.00" || version == "7.00")
			success = parse_mesh_v5(data, offset, mesh);

		if (success)
			compute_mesh_bounds_and_hull(mesh);

		return success;
	}

	static void compute_mesh_bounds_and_hull(parsed_mesh& mesh)
	{
		if (mesh.vertices.empty())
		{
			mesh.bounds.valid = false;
			return;
		}

		mesh.bounds.min = math::vector3(FLT_MAX, FLT_MAX, FLT_MAX);
		mesh.bounds.max = math::vector3(-FLT_MAX, -FLT_MAX, -FLT_MAX);

		for (const auto& vertex : mesh.vertices)
		{
			mesh.bounds.min.x = (std::min)(mesh.bounds.min.x, vertex.position.x);
			mesh.bounds.min.y = (std::min)(mesh.bounds.min.y, vertex.position.y);
			mesh.bounds.min.z = (std::min)(mesh.bounds.min.z, vertex.position.z);
			mesh.bounds.max.x = (std::max)(mesh.bounds.max.x, vertex.position.x);
			mesh.bounds.max.y = (std::max)(mesh.bounds.max.y, vertex.position.y);
			mesh.bounds.max.z = (std::max)(mesh.bounds.max.z, vertex.position.z);
		}

		mesh.bounds.size = mesh.bounds.max - mesh.bounds.min;
		math::vector3 sum = mesh.bounds.min + mesh.bounds.max;
		mesh.bounds.center = { sum.x * 0.5f, sum.y * 0.5f, sum.z * 0.5f };
		mesh.bounds.valid = true;

		mesh.hull_vertices.clear();

		size_t vertex_count = mesh.vertices.size();
		size_t max_hull_vertices = 180;
		size_t sample_step = (vertex_count > max_hull_vertices) ? (vertex_count / max_hull_vertices) : 1;
		mesh.hull_vertices.reserve((std::min)(vertex_count, max_hull_vertices));

		for (size_t i = 0; i < vertex_count; i += sample_step)
		{
			math::vector3 normalized_pos = mesh.vertices[i].position;
			normalized_pos.x -= mesh.bounds.center.x;
			normalized_pos.y -= mesh.bounds.center.y;
			normalized_pos.z -= mesh.bounds.center.z;
			if (mesh.bounds.size.x > 0.001f) normalized_pos.x /= mesh.bounds.size.x;
			if (mesh.bounds.size.y > 0.001f) normalized_pos.y /= mesh.bounds.size.y;
			if (mesh.bounds.size.z > 0.001f) normalized_pos.z /= mesh.bounds.size.z;
			mesh.hull_vertices.push_back(normalized_pos);
		}
	}

	bool save_parsed_mesh_binary(const std::string& name, const parsed_mesh& mesh)
	{
		if (!ensure_storage_directory())
			return false;

		std::string path = get_storage_directory() + "\\" + name + ".mesh";

		std::ofstream file(path, std::ios::binary);
		if (!file.is_open())
			return false;

		file.write(reinterpret_cast<const char*>(&BINARY_CACHE_MAGIC), sizeof(uint32_t));
		file.write(reinterpret_cast<const char*>(&BINARY_CACHE_VERSION), sizeof(uint32_t));

		uint32_t version_len = static_cast<uint32_t>(mesh.version.size());
		file.write(reinterpret_cast<const char*>(&version_len), sizeof(uint32_t));
		file.write(mesh.version.data(), version_len);

		file.write(reinterpret_cast<const char*>(&mesh.num_vertices), sizeof(uint32_t));
		file.write(reinterpret_cast<const char*>(&mesh.num_faces), sizeof(uint32_t));

		uint32_t lod_count = static_cast<uint32_t>(mesh.lods.size());
		file.write(reinterpret_cast<const char*>(&lod_count), sizeof(uint32_t));

		if (!mesh.vertices.empty())
		{
			file.write(reinterpret_cast<const char*>(mesh.vertices.data()),
				mesh.vertices.size() * sizeof(mesh_vertex));
		}

		if (!mesh.faces.empty())
		{
			file.write(reinterpret_cast<const char*>(mesh.faces.data()),
				mesh.faces.size() * sizeof(mesh_face));
		}

		if (!mesh.lods.empty())
		{
			file.write(reinterpret_cast<const char*>(mesh.lods.data()),
				mesh.lods.size() * sizeof(uint32_t));
		}

		return true;
	}

	bool load_parsed_mesh_binary(const std::string& name, parsed_mesh& mesh)
	{
		std::string path = get_storage_directory() + "\\" + name + ".mesh";

		if (!std::filesystem::exists(path))
			return false;

		std::ifstream file(path, std::ios::binary);
		if (!file.is_open())
			return false;

		uint32_t magic, version;
		file.read(reinterpret_cast<char*>(&magic), sizeof(uint32_t));
		file.read(reinterpret_cast<char*>(&version), sizeof(uint32_t));

		if (magic != BINARY_CACHE_MAGIC || version != BINARY_CACHE_VERSION)
		{
			file.close();
			return false;
		}

		uint32_t version_len;
		file.read(reinterpret_cast<char*>(&version_len), sizeof(uint32_t));
		mesh.version.resize(version_len);
		file.read(mesh.version.data(), version_len);

		file.read(reinterpret_cast<char*>(&mesh.num_vertices), sizeof(uint32_t));
		file.read(reinterpret_cast<char*>(&mesh.num_faces), sizeof(uint32_t));

		uint32_t lod_count;
		file.read(reinterpret_cast<char*>(&lod_count), sizeof(uint32_t));

		mesh.vertices.resize(mesh.num_vertices);
		if (mesh.num_vertices > 0)
		{
			file.read(reinterpret_cast<char*>(mesh.vertices.data()),
				mesh.num_vertices * sizeof(mesh_vertex));
		}

		mesh.faces.resize(mesh.num_faces);
		if (mesh.num_faces > 0)
		{
			file.read(reinterpret_cast<char*>(mesh.faces.data()),
				mesh.num_faces * sizeof(mesh_face));
		}

		mesh.lods.resize(lod_count);
		if (lod_count > 0)
		{
			file.read(reinterpret_cast<char*>(mesh.lods.data()),
				lod_count * sizeof(uint32_t));
		}

		compute_mesh_bounds_and_hull(mesh);
		return true;
	}


	static bool download_from_url(const std::wstring& host, const std::wstring& path, std::vector<uint8_t>& data, bool accept_gzip = true)
	{
		return false;
	}

	bool download_mesh_from_asset_id(uint64_t asset_id, std::vector<uint8_t>& mesh_data)
	{
		if (asset_id == 0)
			return false;

		std::vector<uint8_t> api_response;
		std::wstring path = L"/v2/asset/?id=" + std::to_wstring(asset_id);
		if (!download_from_url(L"assetdelivery.roblox.com", path, api_response, false))
		{
			path = L"/v1/asset/?id=" + std::to_wstring(asset_id);
			if (!download_from_url(L"assetdelivery.roblox.com", path, api_response, false))
			{
				return false;
			}
		}

		if (api_response.size() >= 12)
		{
			std::string header(reinterpret_cast<const char*>(api_response.data()), 12);
			if (header.find("version ") == 0)
			{
				mesh_data = std::move(api_response);
				return true;
			}
		}

		std::string response_str(reinterpret_cast<const char*>(api_response.data()), api_response.size());

		size_t loc_start = response_str.find("\"location\"");
		if (loc_start == std::string::npos)
			loc_start = response_str.find("\"url\"");

		if (loc_start != std::string::npos)
		{
			size_t url_start = response_str.find("\"http", loc_start);
			if (url_start != std::string::npos)
			{
				url_start++;
				size_t url_end = response_str.find("\"", url_start);
				if (url_end != std::string::npos)
				{
					std::string location_url = response_str.substr(url_start, url_end - url_start);

					size_t protocol_pos = location_url.find("://");
					if (protocol_pos != std::string::npos)
					{
						std::string remaining = location_url.substr(protocol_pos + 3);
						size_t path_pos = remaining.find('/');
						if (path_pos != std::string::npos)
						{
							std::string host = remaining.substr(0, path_pos);
							std::string url_path = remaining.substr(path_pos);

							std::wstring w_host(host.begin(), host.end());
							std::wstring w_path(url_path.begin(), url_path.end());

							return download_from_url(w_host, w_path, mesh_data);
						}
					}
				}
			}
		}

		mesh_data = std::move(api_response);
		return true;
	}

	static std::string get_mesh_id_string(uint64_t part_address)
	{
		if (part_address == 0)
			return "";

		rbx::instance_t instance(part_address);
		std::string class_name = instance.get_class_name();
		if (class_name != "MeshPart")
			return "";

		uint64_t mesh_id_ptr = memory->read<uint64_t>(part_address + Offsets::MeshPart::MeshId);

		if (mesh_id_ptr == 0)
		{
			std::string mesh_id_direct = memory->read_string(part_address + Offsets::MeshPart::MeshId);
			if (mesh_id_direct != "str_error" && mesh_id_direct != "Unknown" && !mesh_id_direct.empty())
			{
				return mesh_id_direct;
			}
			return "";
		}

		std::string mesh_id = memory->read_string(mesh_id_ptr);

		if (mesh_id == "str_error" || mesh_id == "Unknown")
		{
			std::string mesh_id_direct = memory->read_string(part_address + Offsets::MeshPart::MeshId);
			if (mesh_id_direct != "str_error" && mesh_id_direct != "Unknown" && !mesh_id_direct.empty())
			{
				return mesh_id_direct;
			}
			return "";
		}

		return mesh_id;
	}

	static uint64_t extract_asset_id(const std::string& mesh_id_string)
	{
		if (mesh_id_string.find("rbxassetid://") == 0)
		{
			try { return std::stoull(mesh_id_string.substr(13)); }
			catch (...) { return 0; }
		}
		else if (mesh_id_string.find("rbxasset://") == 0)
		{
			try { return std::stoull(mesh_id_string.substr(11)); }
			catch (...) { return 0; }
		}
		else if (mesh_id_string.find("?id=") != std::string::npos)
		{
			size_t id_pos = mesh_id_string.find("?id=");
			std::string id_str = mesh_id_string.substr(id_pos + 4);
			size_t end_pos = id_str.find_first_of("& \n\r\t");
			if (end_pos != std::string::npos)
				id_str = id_str.substr(0, end_pos);
			try { return std::stoull(id_str); }
			catch (...) { return 0; }
		}

		return 0;
	}

	uint64_t get_mesh_asset_id_from_part(uint64_t part_address)
	{
		if (part_address == 0)
			return 0;

		rbx::instance_t instance(part_address);
		std::string class_name = instance.get_class_name();
		if (class_name != "MeshPart")
			return 0;

		std::string mesh_id_string = get_mesh_id_string(part_address);
		if (mesh_id_string.empty())
			return 0;

		return extract_asset_id(mesh_id_string);
	}

	static void worker_thread_func()
	{
		while (g_running.load())
		{
			mesh_download_task task;
			{
				std::unique_lock<std::mutex> lock(g_queue_mutex);
				g_queue_cv.wait_for(lock, std::chrono::milliseconds(100), [] {
					return !g_task_queue.empty() || !g_running.load();
					});

				if (!g_running.load())
					break;

				if (g_task_queue.empty())
					continue;

				task = std::move(g_task_queue.front());
				g_task_queue.pop();
			}

			{
				std::shared_lock<std::shared_mutex> lock(g_mesh_cache_mutex);
				if (g_mesh_cache.find(task.asset_id) != g_mesh_cache.end())
				{
					std::lock_guard<std::mutex> pending_lock(g_pending_mutex);
					g_pending_assets.erase(task.asset_id);
					continue;
				}
			}

			std::string cache_name = "mesh_" + std::to_string(task.asset_id);
			parsed_mesh mesh;

			if (load_parsed_mesh_binary(cache_name, mesh))
			{
				std::unique_lock<std::shared_mutex> lock(g_mesh_cache_mutex);
				g_mesh_cache[task.asset_id] = std::move(mesh);

				std::lock_guard<std::mutex> pending_lock(g_pending_mutex);
				g_pending_assets.erase(task.asset_id);
				continue;
			}

			std::vector<uint8_t> mesh_data;
			if (!download_mesh_from_asset_id(task.asset_id, mesh_data))
			{
				std::lock_guard<std::mutex> pending_lock(g_pending_mutex);
				g_pending_assets.erase(task.asset_id);
				continue;
			}

			if (mesh_data.empty() || mesh_data.size() < 12)
			{
				std::lock_guard<std::mutex> pending_lock(g_pending_mutex);
				g_pending_assets.erase(task.asset_id);
				continue;
			}

			if (!parse_mesh_from_data(mesh_data, mesh))
			{
				std::lock_guard<std::mutex> pending_lock(g_pending_mutex);
				g_pending_assets.erase(task.asset_id);
				continue;
			}

			if (mesh.vertices.empty() || mesh.faces.empty())
			{
				std::lock_guard<std::mutex> pending_lock(g_pending_mutex);
				g_pending_assets.erase(task.asset_id);
				continue;
			}

			save_parsed_mesh_binary(cache_name, mesh);

			{
				std::unique_lock<std::shared_mutex> lock(g_mesh_cache_mutex);
				g_mesh_cache[task.asset_id] = std::move(mesh);
			}

			{
				std::lock_guard<std::mutex> char_lock(g_character_cache_mutex);
				auto it = g_character_caches.find(task.character_address);
				if (it != g_character_caches.end())
				{
					it->second.part_asset_ids[task.part_name] = task.asset_id;
					it->second.is_valid = true;
				}
			}

			{
				std::lock_guard<std::mutex> pending_lock(g_pending_mutex);
				g_pending_assets.erase(task.asset_id);
			}
		}
	}

	void initialize()
	{
		if (g_running.load())
			return;

		g_running.store(true);
		g_worker_threads.reserve(THREAD_POOL_SIZE);

		for (size_t i = 0; i < THREAD_POOL_SIZE; i++)
		{
			g_worker_threads.emplace_back(worker_thread_func);
		}
	}

	bool parse_and_cache_part_mesh(const std::string& part_name, uint64_t part_address, uint64_t asset_id, parsed_mesh& out_mesh)
	{
		if (asset_id == 0)
			return false;

		{
			std::shared_lock<std::shared_mutex> lock(g_mesh_cache_mutex);
			auto it = g_mesh_cache.find(asset_id);
			if (it != g_mesh_cache.end())
			{
				out_mesh = it->second;
				return true;
			}
		}

		std::string cache_name = "mesh_" + std::to_string(asset_id);

		if (load_parsed_mesh_binary(cache_name, out_mesh))
		{
			std::unique_lock<std::shared_mutex> lock(g_mesh_cache_mutex);
			g_mesh_cache[asset_id] = out_mesh;
			return true;
		}

		std::vector<uint8_t> mesh_data;
		if (!download_mesh_from_asset_id(asset_id, mesh_data))
			return false;

		if (mesh_data.empty() || mesh_data.size() < 12)
			return false;

		if (!parse_mesh_from_data(mesh_data, out_mesh))
			return false;

		if (out_mesh.vertices.empty() || out_mesh.faces.empty())
			return false;

		save_parsed_mesh_binary(cache_name, out_mesh);

		{
			std::unique_lock<std::shared_mutex> lock(g_mesh_cache_mutex);
			g_mesh_cache[asset_id] = out_mesh;
		}

		return true;
	}

	character_mesh_cache* get_or_create_character_cache(uint64_t character_address)
	{
		if (character_address == 0)
			return nullptr;

		std::lock_guard<std::mutex> lock(g_character_cache_mutex);
		auto it = g_character_caches.find(character_address);
		if (it != g_character_caches.end())
		{
			return &it->second;
		}

		auto& cache = g_character_caches[character_address];
		cache.is_valid = false;
		return &cache;
	}

	void update_character_meshes(uint64_t character_address, const std::unordered_map<std::string, uint64_t>& parts)
	{
		if (character_address == 0)
			return;

		if (!g_running.load())
		{
			initialize();
		}

		uint8_t rig_type = get_character_rig_type(character_address);
		std::vector<mesh_download_task> new_tasks;

		{
			std::lock_guard<std::mutex> char_lock(g_character_cache_mutex);
			auto& cache = g_character_caches[character_address];

			for (const auto& [part_name, part_address] : parts)
			{
				if (part_address == 0 || part_name == "HumanoidRootPart")
					continue;

				uint64_t asset_id = get_mesh_asset_id_from_part(part_address);
				if (asset_id == 0)
					continue;

				std::string mesh_part_name = get_mesh_part_name(part_name, rig_type);

				auto cached_it = cache.part_asset_ids.find(mesh_part_name);
				if (cached_it != cache.part_asset_ids.end() && cached_it->second == asset_id)
					continue;

				{
					std::shared_lock<std::shared_mutex> mesh_lock(g_mesh_cache_mutex);
					if (g_mesh_cache.find(asset_id) != g_mesh_cache.end())
					{
						cache.part_asset_ids[mesh_part_name] = asset_id;
						cache.is_valid = true;
						continue;
					}
				}

				{
					std::lock_guard<std::mutex> pending_lock(g_pending_mutex);
					if (g_pending_assets.find(asset_id) != g_pending_assets.end())
						continue;

					g_pending_assets.insert(asset_id);
				}

				new_tasks.push_back({ asset_id, character_address, mesh_part_name });
			}
		}

		if (!new_tasks.empty())
		{
			std::lock_guard<std::mutex> queue_lock(g_queue_mutex);
			for (auto& task : new_tasks)
			{
				g_task_queue.push(std::move(task));
			}
			g_queue_cv.notify_all();
		}
	}

	const parsed_mesh* get_cached_mesh(uint64_t character_address, const std::string& part_name)
	{
		if (character_address == 0 || part_name.empty())
			return nullptr;

		uint8_t rig_type = get_character_rig_type(character_address);
		std::string mesh_part_name = get_mesh_part_name(part_name, rig_type);

		uint64_t asset_id = 0;
		{
			std::lock_guard<std::mutex> char_lock(g_character_cache_mutex);
			auto it = g_character_caches.find(character_address);
			if (it == g_character_caches.end())
				return nullptr;

			auto asset_it = it->second.part_asset_ids.find(mesh_part_name);
			if (asset_it == it->second.part_asset_ids.end())
				return nullptr;

			asset_id = asset_it->second;
		}

		std::shared_lock<std::shared_mutex> lock(g_mesh_cache_mutex);
		auto mesh_it = g_mesh_cache.find(asset_id);
		if (mesh_it == g_mesh_cache.end())
			return nullptr;

		return &mesh_it->second;
	}

}
