#include "hud.h"
#include "cl_util.h"
#include "cl_entity.h"
#include "const.h"
#include "entity_state.h"
#include "spawns.h"
#include "triangleapi.h"

#include <vector>
#include <string>
#include <math.h>
#include <stdio.h>

namespace spawns
{
	struct SpawnPoint
	{
		Vector origin;
		float last_active_time;
	};

	static std::vector<SpawnPoint> g_SpawnPoints;
	static std::string g_LastMap;

	static cvar_t* cl_show_spawns = nullptr;
	static cvar_t* cl_show_spawns_dist = nullptr;
	static cvar_t* cl_show_spawns_only_active = nullptr;

	struct bsp_lump_t
	{
		int fileofs;
		int filelen;
	};

	struct bsp_header_t
	{
		int version;
		bsp_lump_t lumps[15]; // Lump 0 is entities
	};

	static std::vector<SpawnPoint> load_spawn_origins()
	{
		std::vector<SpawnPoint> spawns_list;
		const char* level_name = gEngfuncs.pfnGetLevelName();
		if (!level_name || !level_name[0])
			return spawns_list;

		// Extract base map filename (e.g., crossfire.bsp)
		const char* slash = strrchr(level_name, '/');
		if (!slash)
			slash = strrchr(level_name, '\\');
		std::string map_filename = slash ? (slash + 1) : level_name;

		FILE* f = nullptr;

		// Array of search paths to try
		std::vector<std::string> paths_to_try;
		paths_to_try.push_back(level_name); // 1. Direct path
		paths_to_try.push_back(std::string(gEngfuncs.pfnGetGameDirectory()) + "/" + level_name); // 2. Mod path
		paths_to_try.push_back(std::string(gEngfuncs.pfnGetGameDirectory()) + "/maps/" + map_filename); // 3. Mod maps path
		
		// 4. Mod-specific downloads path (e.g. ag_downloads/maps/crossfire.bsp)
		paths_to_try.push_back(std::string(gEngfuncs.pfnGetGameDirectory()) + "_downloads/maps/" + map_filename);
		paths_to_try.push_back(std::string(gEngfuncs.pfnGetGameDirectory()) + "_downloads/" + std::string(level_name));
		
		paths_to_try.push_back("valve_downloads/maps/" + map_filename); // 5. Valve downloads path
		paths_to_try.push_back("valve_downloads/" + std::string(level_name)); // 6. Valve downloads level path
		paths_to_try.push_back("valve/maps/" + map_filename); // 7. Fallback valve path
		paths_to_try.push_back("valve/" + std::string(level_name)); // 8. Fallback level path

		for (const auto& path : paths_to_try)
		{
			f = fopen(path.c_str(), "rb");
			if (f)
			{
				gEngfuncs.Con_Printf("[Spawns] Loaded map BSP file from path: %s\n", path.c_str());
				break;
			}
		}

		if (!f)
		{
			gEngfuncs.Con_Printf("[Spawns] ERROR: Could not find or open map file %s in any search path!\n", level_name);
			return spawns_list;
		}

		bsp_header_t header;
		if (fread(&header, sizeof(bsp_header_t), 1, f) == 1)
		{
			int entity_offset = header.lumps[0].fileofs;
			int entity_len = header.lumps[0].filelen;
			if (entity_len > 0)
			{
				char* buffer = new char[entity_len + 1];
				fseek(f, entity_offset, SEEK_SET);
				if (fread(buffer, entity_len, 1, f) == 1)
				{
					buffer[entity_len] = '\0';
					std::string ent_str(buffer);
					delete[] buffer;

					size_t pos = 0;
					while ((pos = ent_str.find('{', pos)) != std::string::npos)
					{
						size_t end_pos = ent_str.find('}', pos);
						if (end_pos == std::string::npos)
							break;

						std::string entity_body = ent_str.substr(pos + 1, end_pos - pos - 1);
						pos = end_pos + 1;

						// Extract all tokens in quotes
						std::vector<std::string> tokens;
						size_t t_pos = 0;
						while (true)
						{
							size_t q1 = entity_body.find('"', t_pos);
							if (q1 == std::string::npos)
								break;
							size_t q2 = entity_body.find('"', q1 + 1);
							if (q2 == std::string::npos)
								break;
							tokens.push_back(entity_body.substr(q1 + 1, q2 - q1 - 1));
							t_pos = q2 + 1;
						}

						std::string classname;
						std::string origin_val;

						for (size_t k = 0; k + 1 < tokens.size(); k += 2)
						{
							const std::string& key = tokens[k];
							const std::string& val = tokens[k + 1];

							// Case insensitive key comparison
							bool is_classname = true;
							if (key.size() == 9)
							{
								for (int char_idx = 0; char_idx < 9; char_idx++)
								{
									if (tolower(key[char_idx]) != "classname"[char_idx])
										{ is_classname = false; break; }
								}
							}
							else { is_classname = false; }

							bool is_origin = true;
							if (key.size() == 6)
							{
								for (int char_idx = 0; char_idx < 6; char_idx++)
								{
									if (tolower(key[char_idx]) != "origin"[char_idx])
										{ is_origin = false; break; }
								}
							}
							else { is_origin = false; }

							if (is_classname)
								classname = val;
							else if (is_origin)
								origin_val = val;
						}

						// Convert classname to lowercase for safe comparison
						for (auto& c : classname) c = tolower(c);

						if (classname == "info_player_deathmatch" ||
							classname == "info_player_start" ||
							classname == "info_player_coop")
						{
							if (!origin_val.empty())
							{
								float x, y, z;
								if (sscanf(origin_val.c_str(), "%f %f %f", &x, &y, &z) == 3)
								{
									SpawnPoint sp;
									sp.origin = Vector(x, y, z);
									sp.last_active_time = -999.0f;
									spawns_list.push_back(sp);
								}
							}
						}
					}
				}
				else
				{
					delete[] buffer;
				}
			}
		}
		fclose(f);

		gEngfuncs.Con_Printf("[Spawns] Successfully parsed %d spawn points.\n", (int)spawns_list.size());
		return spawns_list;
	}

	void init()
	{
		cl_show_spawns = CVAR_CREATE("cl_show_spawns", "1", FCVAR_CLIENTDLL | FCVAR_ARCHIVE);
		cl_show_spawns_dist = CVAR_CREATE("cl_show_spawns_dist", "2000", FCVAR_CLIENTDLL | FCVAR_ARCHIVE);
		cl_show_spawns_only_active = CVAR_CREATE("cl_show_spawns_only_active", "0", FCVAR_CLIENTDLL | FCVAR_ARCHIVE);
	}

	void update_and_draw()
	{
		if (!cl_show_spawns || cl_show_spawns->value == 0.0f)
			return;

		std::string current_map = gEngfuncs.pfnGetLevelName();
		if (current_map != g_LastMap)
		{
			g_LastMap = current_map;
			g_SpawnPoints = load_spawn_origins();
		}

		if (g_SpawnPoints.empty())
			return;

		float current_time = gEngfuncs.GetClientTime();
		cl_entity_t* local_player = gEngfuncs.GetLocalPlayer();
		if (!local_player)
			return;

		int max_clients = gEngfuncs.GetMaxClients();

		// 1. Detect if any active player is near a spawn point
		for (int i = 1; i <= max_clients; i++)
		{
			if (i == local_player->index)
				continue; // Skip local player

			cl_entity_t* ent = gEngfuncs.GetEntityByIndex(i);
			if (!ent || !ent->player)
				continue;

			// Check if they are currently updated/visible in PVS and alive
			if (ent->curstate.messagenum < local_player->curstate.messagenum)
				continue;
			if (ent->curstate.effects & EF_NODRAW)
				continue;

			// Check both predicted origin and raw packet state origin
			Vector player_pos_predicted = ent->origin;
			Vector player_pos_packet = ent->curstate.origin;

			for (auto& sp : g_SpawnPoints)
			{
				float dist_predicted = (player_pos_predicted - sp.origin).Length();
				float dist_packet = (player_pos_packet - sp.origin).Length();

				if (dist_predicted < 128.0f || dist_packet < 128.0f)
				{
					sp.last_active_time = current_time;
				}
			}
		}

		// 2. Draw spawn points on the screen
		float max_dist = cl_show_spawns_dist ? cl_show_spawns_dist->value : 2000.0f;

		for (const auto& sp : g_SpawnPoints)
		{
			float time_since_active = current_time - sp.last_active_time;
			bool is_active = (time_since_active >= 0.0f && time_since_active < 5.0f);

			if (cl_show_spawns_only_active && cl_show_spawns_only_active->value != 0.0f && !is_active)
				continue;

			float dist_to_local = (local_player->origin - sp.origin).Length();
			if (max_dist > 0.0f && dist_to_local > max_dist)
				continue;

			float screen[3];
			if (gEngfuncs.pTriAPI->WorldToScreen(const_cast<float*>(static_cast<const float*>(sp.origin)), screen))
				continue; // Behind camera

			int x = XPROJECT(screen[0]);
			int y = YPROJECT(screen[1]);

			char text_buf[64];
			int r, g, b;

			if (is_active)
			{
				// Flash between bright red and darker red
				float pulse = sinf(current_time * 10.0f) * 0.5f + 0.5f;
				r = 200 + (int)(55 * pulse);
				g = (int)(50 * pulse);
				b = (int)(50 * pulse);

				std::snprintf(text_buf, sizeof(text_buf), "* SPAWN (ACTIVE %0.1fs) *", time_since_active);
			}
			else
			{
				r = 0;
				g = 180;
				b = 0;
				std::snprintf(text_buf, sizeof(text_buf), "[ Spawn (%d) ]", (int)dist_to_local);
			}

			int w, h;
			gEngfuncs.pfnDrawConsoleStringLen(text_buf, &w, &h);
			gHUD.DrawConsoleStringWithColorTags(x - w / 2, y - h / 2, const_cast<char*>(text_buf), false, (float)r / 255.0f, (float)g / 255.0f, (float)b / 255.0f);
		}
	}
}
