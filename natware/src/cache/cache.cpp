#include "cache.h"
#include <thread>
#include <algorithm>
#include <cmath>
#include <cfloat>
#include <game/game.h>
#include <memory/memory.h>
#include <Offsets/Offsets.hpp>
#include <gamesupport/gamesupport.h>
#include <settings.h>

// Roblox BrickColor ID -> RGB (0-1 float). Covers the most common team colors.
static bool brickcolor_to_rgb(int id, float out[3])
{
	switch (id)
	{
	case 1:   out[0]=0.949f; out[1]=0.953f; out[2]=0.953f; return true; // White
	case 5:   out[0]=0.843f; out[1]=0.773f; out[2]=0.604f; return true; // Brick yellow
	case 9:   out[0]=0.910f; out[1]=0.725f; out[2]=0.545f; return true; // Light orange
	case 11:  out[0]=0.498f; out[1]=0.557f; out[2]=0.392f; return true; // Pastel blue-green
	case 18:  out[0]=0.800f; out[1]=0.557f; out[2]=0.412f; return true; // Nougat
	case 21:  out[0]=0.769f; out[1]=0.157f; out[2]=0.110f; return true; // Bright red
	case 23:  out[0]=0.051f; out[1]=0.412f; out[2]=0.675f; return true; // Bright blue
	case 24:  out[0]=0.961f; out[1]=0.804f; out[2]=0.188f; return true; // Bright yellow
	case 26:  out[0]=0.106f; out[1]=0.165f; out[2]=0.208f; return true; // Black
	case 28:  out[0]=0.157f; out[1]=0.498f; out[2]=0.278f; return true; // Dark green
	case 29:  out[0]=0.631f; out[1]=0.769f; out[2]=0.549f; return true; // Medium green
	case 37:  out[0]=0.294f; out[1]=0.592f; out[2]=0.294f; return true; // Bright green
	case 38:  out[0]=0.627f; out[1]=0.373f; out[2]=0.208f; return true; // Dark orange
	case 45:  out[0]=0.678f; out[1]=0.737f; out[2]=0.835f; return true; // Light blue
	case 100: out[0]=0.933f; out[1]=0.573f; out[2]=0.573f; return true; // Light red
	case 101: out[0]=0.859f; out[1]=0.561f; out[2]=0.608f; return true; // Medium red
	case 102: out[0]=0.420f; out[1]=0.518f; out[2]=0.710f; return true; // Medium blue
	case 104: out[0]=0.420f; out[1]=0.294f; out[2]=0.502f; return true; // Bright violet
	case 105: out[0]=0.886f; out[1]=0.608f; out[2]=0.251f; return true; // Bright orange
	case 106: out[0]=0.851f; out[1]=0.522f; out[2]=0.255f; return true; // Bright orange (alt)
	case 107: out[0]=0.008f; out[1]=0.522f; out[2]=0.522f; return true; // Bright bluish green
	case 119: out[0]=0.643f; out[1]=0.741f; out[2]=0.278f; return true; // Br. yellowish green
	case 125: out[0]=0.914f; out[1]=0.722f; out[2]=0.573f; return true; // Light orange
	case 135: out[0]=0.455f; out[1]=0.525f; out[2]=0.616f; return true; // Sand blue
	case 141: out[0]=0.106f; out[1]=0.220f; out[2]=0.055f; return true; // Earth green
	case 151: out[0]=0.467f; out[1]=0.553f; out[2]=0.522f; return true; // Sand green
	case 153: out[0]=0.659f; out[1]=0.467f; out[2]=0.322f; return true; // Sand red
	case 192: out[0]=0.412f; out[1]=0.251f; out[2]=0.157f; return true; // Reddish brown
	case 194: out[0]=0.631f; out[1]=0.647f; out[2]=0.635f; return true; // Medium stone grey
	case 199: out[0]=0.388f; out[1]=0.373f; out[2]=0.384f; return true; // Dark stone grey
	case 208: out[0]=0.898f; out[1]=0.863f; out[2]=0.737f; return true; // Light stone grey
	case 226: out[0]=0.992f; out[1]=0.918f; out[2]=0.553f; return true; // Cool yellow
	case 1001: out[0]=0.992f; out[1]=0.992f; out[2]=0.992f; return true; // Institutional white
	case 1002: out[0]=0.016f; out[1]=0.016f; out[2]=0.016f; return true; // Really black
	case 1003: out[0]=1.000f; out[1]=0.000f; out[2]=0.000f; return true; // Really red
	case 1004: out[0]=1.000f; out[1]=0.690f; out[2]=0.000f; return true; // Deep orange
	case 1005: out[0]=0.000f; out[1]=0.400f; out[2]=0.800f; return true; // Really blue
	case 1006: out[0]=0.710f; out[1]=0.000f; out[2]=0.000f; return true; // Alder
	case 1007: out[0]=0.639f; out[1]=0.294f; out[2]=0.000f; return true; // Dusty Rose
	case 1008: out[0]=0.690f; out[1]=0.580f; out[2]=0.337f; return true; // Olive
	case 1009: out[0]=1.000f; out[1]=1.000f; out[2]=0.000f; return true; // New Yeller
	case 1010: out[0]=0.000f; out[1]=0.000f; out[2]=1.000f; return true; // Really blue
	case 1011: out[0]=0.004f; out[1]=0.004f; out[2]=0.004f; return true; // Really black (alt)
	case 1012: out[0]=0.067f; out[1]=0.067f; out[2]=1.000f; return true; // Deep blue
	case 1013: out[0]=0.016f; out[1]=0.686f; out[2]=0.925f; return true; // Toothpaste
	case 1014: out[0]=0.671f; out[1]=1.000f; out[2]=0.000f; return true; // Lime green
	case 1015: out[0]=0.863f; out[1]=0.086f; out[2]=0.235f; return true; // Crimson
	case 1016: out[0]=1.000f; out[1]=0.400f; out[2]=0.800f; return true; // Hot pink
	case 1017: out[0]=1.000f; out[1]=0.690f; out[2]=0.000f; return true; // Deep orange (alt)
	case 1018: out[0]=0.184f; out[1]=0.820f; out[2]=0.580f; return true; // Teal
	case 1019: out[0]=0.000f; out[1]=1.000f; out[2]=1.000f; return true; // Cyan
	case 1020: out[0]=0.518f; out[1]=0.000f; out[2]=0.678f; return true; // Royal purple
	case 1021: out[0]=0.678f; out[1]=0.000f; out[2]=1.000f; return true; // Neon orange (actually purple)
	case 1022: out[0]=0.459f; out[1]=0.000f; out[2]=0.459f; return true; // Magenta
	case 1023: out[0]=0.686f; out[1]=0.867f; out[2]=1.000f; return true; // Baby blue
	case 1024: out[0]=1.000f; out[1]=0.698f; out[2]=0.000f; return true; // Carnation pink (actually orange)
	case 1025: out[0]=1.000f; out[1]=0.600f; out[2]=0.000f; return true; // Gold
	case 1026: out[0]=0.702f; out[1]=0.533f; out[2]=1.000f; return true; // Lavender
	case 1027: out[0]=0.286f; out[1]=0.592f; out[2]=0.812f; return true; // Pastel blue
	case 1028: out[0]=0.671f; out[1]=0.953f; out[2]=0.400f; return true; // Pastel green
	case 1029: out[0]=1.000f; out[1]=1.000f; out[2]=0.596f; return true; // Pastel yellow
	case 1030: out[0]=1.000f; out[1]=0.596f; out[2]=0.863f; return true; // Pink
	case 1031: out[0]=0.749f; out[1]=0.000f; out[2]=0.000f; return true; // Maroon
	case 1032: out[0]=0.471f; out[1]=0.235f; out[2]=0.000f; return true; // Brown
	default:  return false;
	}
}

void cache::run()
{
	while (true)
	{
		std::vector<cache::entity_t> temp_cache;

		rbx::instance_t workspace = game::datamodel.find_first_child_by_class("Workspace");
		if (workspace.address != 0)
		{
			game::workspace = workspace;
			game::camera = memory->read<std::uint64_t>(workspace.address + Offsets::Workspace::CurrentCamera);
		}

		cache::entity_t local_entity{};

		const std::uint64_t game_id = game::datamodel.address ? memory->read<std::uint64_t>(game::datamodel.address + Offsets::DataModel::GameId) : 0ULL;
		const std::uint64_t place_id = game::datamodel.address ? memory->read<std::uint64_t>(game::datamodel.address + Offsets::DataModel::PlaceId) : 0ULL;
		const auto detection = gamesupport::detect(game_id, place_id);

		bool used_custom_cache = false;
		// The aim mode is the user's choice and is never changed here: the aimbot picks what
		// actually runs in each game (aimbot::effective_mode).
		game::is_phantom_forces = (detection.key == gamesupport::GameKey::PhantomForces);
		game::is_murder_mystery_2 = (detection.key == gamesupport::GameKey::MurderMystery2);
		game::is_lumber_tycoon_2 = (detection.key == gamesupport::GameKey::LumberTycoon2);

		if (game::is_phantom_forces)
		{
			if (!game::players.address || !game::workspace.address)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
				continue;
			}

			rbx::player_t local_player_obj{ memory->read<std::uint64_t>(game::players.address + Offsets::Player::LocalPlayer) };
			std::string local_player_name;
			if (local_player_obj.address != 0)
				local_player_name = local_player_obj.get_name();

			// PF doesn't use Roblox's Team system (Player.Team is always null).
			// Use Player.TeamColor (BrickColor int) to identify teams instead.
			std::unordered_map<std::string, int> player_teamcolor_map;
			int local_team_color = -1;
			{
				std::vector<rbx::player_t> service_players = game::players.get_children<rbx::player_t>();
				for (rbx::player_t& sp : service_players)
				{
					std::string pname = sp.get_name();
					int tc = memory->read<int>(sp.address + Offsets::Player::TeamColor);
					if (!pname.empty())
						player_teamcolor_map[pname] = tc;
				}
				if (local_player_obj.address != 0)
					local_team_color = memory->read<int>(local_player_obj.address + Offsets::Player::TeamColor);
			}

			// PF: Workspace.Players -> 2 team folders -> player models
			rbx::instance_t pf_players_folder = game::workspace.find_first_child("Players");

			if (pf_players_folder.address != 0)
			{
				std::vector<rbx::instance_t> team_folders = pf_players_folder.get_children();

				for (rbx::instance_t& team_folder : team_folders)
				{
					std::vector<rbx::instance_t> player_models = team_folder.get_children();

					for (rbx::instance_t& model : player_models)
					{
						cache::entity_t entity{};
						entity.instance = { model.address };
						entity.health = 100.f;
						entity.max_health = 100.f;

						std::vector<rbx::instance_t> parts = model.get_children();
						int limb_index = 0;

						for (rbx::instance_t& part : parts)
						{
							std::string part_class = part.get_class_name();
							if (part_class.find("Part") == std::string::npos)
								continue;

							rbx::instance_t billboard = part.find_first_child_by_class("BillboardGui");
							if (billboard.address != 0)
							{
								rbx::instance_t text_label = billboard.find_first_child_by_class("TextLabel");
								if (text_label.address != 0)
								{
									entity.name = memory->read_string(text_label.address + Offsets::GuiObject::Text);
									entity.display_name = entity.name;
								}
								entity.parts["Head"] = rbx::part_t(part.address);
								continue;
							}

							rbx::instance_t spotlight = part.find_first_child_by_class("SpotLight");
							if (spotlight.address != 0)
							{
								entity.parts["HumanoidRootPart"] = rbx::part_t(part.address);
								entity.parts["UpperTorso"] = rbx::part_t(part.address);
								continue;
							}

							{
								static const char* limb_names[] = {
									"LeftUpperArm", "RightUpperArm",
									"LeftUpperLeg", "RightUpperLeg",
									"LowerTorso"
								};
								if (limb_index < 5)
									entity.parts[limb_names[limb_index]] = rbx::part_t(part.address);
								limb_index++;
							}
						}

						if (entity.parts.find("Head") == entity.parts.end() ||
							entity.parts.find("HumanoidRootPart") == entity.parts.end())
							continue;

						if (entity.name.empty())
							continue;

						// Look up TeamColor from Players service by name
						auto tc_it = player_teamcolor_map.find(entity.name);
						if (tc_it != player_teamcolor_map.end())
						{
							entity.team = static_cast<std::uint64_t>(tc_it->second);
							entity.has_team_color = brickcolor_to_rgb(tc_it->second, entity.team_color);
						}
						else
						{
							entity.team = team_folder.address; // fallback to folder address
						}

						entity.rig_type = 1;

						// PF has no real Humanoid object - don't set a fake one
						// (setting torso address as humanoid breaks is_jumping/state checks)
						entity.humanoid = { 0 };

						temp_cache.push_back(entity);
					}
				}
			}

			// Set local player team from TeamColor
			if (local_player_obj.address != 0)
			{
				local_entity.name = local_player_name;
				local_entity.instance = { local_player_obj.address };
				local_entity.team = (local_team_color >= 0) ? static_cast<std::uint64_t>(local_team_color) : 0;
			}

			used_custom_cache = true;
		}

		if (!used_custom_cache)
		{
			// Re-resolve players if address is stale (teleport/rejoin)
			if (!game::players.address && game::datamodel.address)
			{
				game::players = game::datamodel.find_first_child_by_class("Players");
				if (game::players.address)
				{
					game::local_player = memory->read<rbx::instance_t>(game::players.address + Offsets::Player::LocalPlayer);
					if (game::local_player.address)
					{
						rbx::player_t lp_obj = { game::local_player.address };
						game::local_character = { lp_obj.get_model_instance().address };
					}
				}
			}

			if (!game::players.address)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
				continue;
			}

			std::vector<rbx::player_t> players = game::players.get_children<rbx::player_t>();

			// If players list is empty, datamodel might have changed — re-resolve
			if (players.empty() && game::datamodel.address)
			{
				game::players = game::datamodel.find_first_child_by_class("Players");
				if (game::players.address)
					players = game::players.get_children<rbx::player_t>();
			}

			rbx::player_t local_player_obj{ memory->read<std::uint64_t>(game::players.address + Offsets::Player::LocalPlayer) };

			for (rbx::player_t& player : players)
			{
				cache::entity_t entity{};

				entity.instance = { player.address };
				entity.name = player.get_name();
				entity.display_name = player.get_display_name();
				entity.team = memory->read<std::uint64_t>(player.address + Offsets::Player::Team);

				// Read team color from Team instance's BrickColor
				if (entity.team != 0 && entity.team != 0xFFFFFFFFFFFFFFFF)
				{
					try {
						int brick_color_id = memory->read<int>(entity.team + Offsets::Team::BrickColor);
						entity.has_team_color = brickcolor_to_rgb(brick_color_id, entity.team_color);
					} catch (...) {}
				}

				rbx::model_instance_t model_instance = player.get_model_instance();

				if (model_instance.address != 0)
				{
					std::vector<rbx::instance_t> children = model_instance.get_children();
					for (rbx::instance_t& child : children)
					{
						std::string child_name = child.get_name();
						std::string child_class = child.get_class_name();

						if (child_class.find("Part") != std::string::npos)
						{
							entity.parts[child_name] = rbx::part_t(child.address);
						}
						else if (child_class == "Humanoid")
						{
							entity.humanoid = { child.address };
						}
						else if (child_class == "Tool")
						{
							entity.tool_name = child_name;
						}
						else if (child_name == "BodyEffects")
						{
							if (rbx::instance_t ko = child.find_first_child("K.O"); ko.address != 0)
								entity.knocked = memory->read<bool>(ko.address + Offsets::Misc::Value);

							static std::vector<const char*> armor_candidates = { "Armor", "Armour", "Defense", "Defence" };
							for (const char* nm : armor_candidates)
							{
								rbx::instance_t armor_node = child.find_first_child(nm);
								if (armor_node.address == 0)
									continue;

								try {
									entity.armor_value = memory->read<int>(armor_node.address + Offsets::Misc::Value);
								} catch (...) {
									try {
										entity.armor_value = static_cast<int>(memory->read<double>(armor_node.address + Offsets::Misc::Value));
									} catch (...) {}
								}
								if (entity.armor_value >= 0) {
									entity.armor_percent = std::clamp(entity.armor_value / 100.0f, 0.0f, 1.0f);
									break;
								}
							}
						}
					}
				}

				if (entity.humanoid.address != 0)
				{
					entity.rig_type = entity.humanoid.get_rig_type();
					entity.health = memory->read<float>(entity.humanoid.address + Offsets::Humanoid::Health);
					entity.max_health = memory->read<float>(entity.humanoid.address + Offsets::Humanoid::MaxHealth);
				}

				// MM2 role detection: Knife=Murderer, Gun/Revolver=Sheriff, else=Innocent
				if (game::is_murder_mystery_2 && settings::visuals::mm2_esp)
				{
					entity.mm2_role = 1; // default Innocent

					// Check equipped tool (in character model)
					auto detect_role_from_tool = [](const std::string& tool) -> int {
						if (tool.empty()) return 0;
						if (tool == "Knife" || tool == "knife") return 3;
						if (tool == "Gun" || tool == "gun" || tool == "Revolver" || tool == "revolver") return 2;
						return 0;
					};

					int role = detect_role_from_tool(entity.tool_name);

					// Also check Backpack for unequipped tools
					if (role == 0)
					{
						rbx::instance_t backpack = player.find_first_child("Backpack");
						if (backpack.address != 0)
						{
							std::vector<rbx::instance_t> items = backpack.get_children();
							for (rbx::instance_t& item : items)
							{
								std::string item_name = item.get_name();
								role = detect_role_from_tool(item_name);
								if (role != 0) break;
							}
						}
					}

					if (role != 0)
						entity.mm2_role = role;

					// Set team color based on role
					if (entity.mm2_role == 3) // Murderer = Red
					{
						entity.team_color[0] = 1.0f; entity.team_color[1] = 0.15f; entity.team_color[2] = 0.15f;
						entity.has_team_color = true;
					}
					else if (entity.mm2_role == 2) // Sheriff = Blue
					{
						entity.team_color[0] = 0.15f; entity.team_color[1] = 0.4f; entity.team_color[2] = 1.0f;
						entity.has_team_color = true;
					}
					else // Innocent = Gray
					{
						entity.team_color[0] = 0.6f; entity.team_color[1] = 0.6f; entity.team_color[2] = 0.6f;
						entity.has_team_color = true;
					}
				}

				if (local_player_obj.address != 0 && player.address == local_player_obj.address)
				{
					local_entity = entity;
				}

				temp_cache.push_back(entity);
			}
		}

		{
			std::lock_guard<std::mutex> lock(mtx);
			cached_players = std::move(temp_cache);
			// For PF: only persist old local player when cache is empty (dead/loading).
			// If cache has entities but local wasn't found, reset — teams may have changed.
			if (local_entity.instance.address != 0)
				cached_local_player = local_entity;
			else if (!used_custom_cache || !cached_players.empty())
				cached_local_player = local_entity;
		}

		// New and respawned players show up within a few frames (it was 100 ms plus the scan).
		std::this_thread::sleep_for(std::chrono::milliseconds(33));
	}
}
