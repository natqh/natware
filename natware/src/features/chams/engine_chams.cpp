#include "engine_chams.h"
#include "chams_layout.h"
#include <memory/memory.h>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace engine_chams
{
    namespace
    {
        std::mutex              g_mutex;
        Status                  g_status       = Status::Disabled;
        std::size_t             g_entity_count = 0;
        std::size_t             g_player_count_val = 0;
        Settings                g_current_settings;

        bool mem_read_bytes(std::uint64_t address, void* buffer, std::size_t size)
        {
            auto status = Luck_ReadVirtualMemory(
                memory->get_process_handle(),
                reinterpret_cast<void*>(address),
                buffer,
                static_cast<ULONG>(size),
                nullptr
            );
            return status == 0;
        }

        template <typename T>
        bool mem_read(std::uint64_t address, T& out)
        {
            return mem_read_bytes(address, &out, sizeof(T));
        }

        bool mem_write_bytes(std::uint64_t address, const void* buffer, std::size_t size)
        {
            auto status = Luck_WriteVirtualMemory(
                memory->get_process_handle(),
                reinterpret_cast<void*>(address),
                const_cast<void*>(buffer),
                static_cast<ULONG>(size),
                nullptr
            );
            return status == 0;
        }

        template <typename T>
        bool mem_write(std::uint64_t address, const T& value)
        {
            return mem_write_bytes(address, &value, sizeof(T));
        }

        class Range final
        {
        public:
            std::uint64_t begin = 0;
            std::uint64_t end   = 0;

            bool valid(std::size_t stride, std::size_t limit) const
            {
                return end >= begin && (end - begin) % stride == 0 && (end - begin) / stride <= limit;
            }
            bool operator==(const Range&) const = default;
        };

        class Table final
        {
        public:
            std::uint64_t depth   = 0;
            std::uint64_t active  = 0;
            std::uint64_t fill    = 0;
            std::uint64_t outline = 0;
            std::uint64_t next    = 0;
            bool operator==(const Table&) const = default;
        };

        class Slot final
        {
        public:
            std::uint32_t           depth = 0;
            std::uint8_t            active = 0;
            std::array<float, 4>    fill{};
            std::array<float, 4>    outline{};
            bool operator==(const Slot&) const = default;
        };

        class Lease final
        {
        public:
            Table table;
            Slot  original;
            Slot  written;
        };

        class Entity final
        {
        public:
            std::uint64_t                player = 0;
            std::uint64_t                model = 0;
            std::uint64_t                humanoid = 0;
            std::uint64_t                part = 0;
            std::uint64_t                cluster = 0;
            std::uint64_t                address = 0;
            std::array<std::uint64_t, 2> geometry{};
            std::array<std::uint64_t, 2> techniques{};
            Range                        indices;
            bool operator==(const Entity&) const = default;
        };

        Slot make_appearance(const Settings& settings)
        {
            Slot result;
            result.depth  = settings.through_walls ? 0 : 1;
            result.active = 1;
            for (std::size_t i = 0; i < 4; ++i)
            {
                auto f = settings.fill[i];
                auto o = settings.outline[i];
                result.fill[i]    = std::isfinite(f) ? std::clamp(f, 0.0f, 1.0f) : 0.0f;
                result.outline[i] = std::isfinite(o) ? std::clamp(o, 0.0f, 1.0f) : 0.0f;
            }
            return result;
        }

        std::uint64_t                               s_base = 0;
        std::uint64_t                               s_engine = 0;
        std::uint64_t                               s_manager = 0;
        std::uint64_t                               s_datamodel = 0;
        std::uint8_t                                s_merged = 0;
        std::uint32_t                               s_id = 0;
        std::vector<Lease>                          s_leases;
        std::unordered_map<std::uint64_t, Entity>   s_entities;

        bool equals(std::uint64_t address, std::uint64_t value)
        {
            std::uint64_t current = 0;
            return mem_read(address, current) && current == value;
        }

        bool get_children(std::uint64_t instance, std::vector<std::uint64_t>& result)
        {
            result.clear();
            std::uint64_t container = 0;
            Range range;
            if (!mem_read(instance + layout::children(), container))
                return false;
            if (!container)
                return true;
            if (!mem_read(container, range) || !range.valid(16, 4096))
                return false;
            std::vector<std::array<std::uint64_t, 2>> entries((range.end - range.begin) / 16);
            if (!entries.empty() && !mem_read_bytes(range.begin, entries.data(), entries.size() * 16))
                return false;
            Range checked;
            if (!mem_read(container, checked) || checked != range || !equals(instance + layout::children(), container))
                return false;
            for (const auto& entry : entries)
            {
                if (entry[0])
                    result.push_back(entry[0]);
            }
            return true;
        }

        std::string get_class_name(std::uint64_t instance)
        {
            std::uint64_t descriptor = 0;
            std::uint64_t str = 0;
            std::uint64_t length = 0;
            std::uint64_t capacity = 0;
            if (!mem_read(instance + layout::class_descriptor(), descriptor) || !descriptor
                || !mem_read(descriptor + layout::class_name(), str) || !str
                || !mem_read(str + 16, length) || length > 96
                || !mem_read(str + 24, capacity) || capacity < length)
                return {};
            auto data = str;
            if (capacity >= 16 && !mem_read(str, data))
                return {};
            std::string result(static_cast<std::size_t>(length), char{});
            if (length && !mem_read_bytes(data, result.data(), result.size()))
                return {};
            return result;
        }

        bool session_live()
        {
            std::uint64_t fake = 0;
            std::uint8_t merged = 0;
            return s_engine && s_manager && s_datamodel
                && equals(s_base + layout::visual_engine_ptr(), s_engine)
                && equals(s_engine + layout::manager_offset, s_manager)
                && mem_read(s_base + layout::fake_dm_ptr(), fake) && fake
                && equals(fake + layout::fake_dm_to_real(), s_datamodel)
                && mem_read(s_base + layout::merge_flag, merged) && merged == s_merged;
        }

        bool get_tables(std::vector<Table>& tables)
        {
            tables.clear();
            if (!session_live())
                return false;
            if (s_merged)
            {
                tables.push_back({
                    s_manager + layout::global_depth,
                    s_manager + layout::global_active,
                    s_manager + layout::global_fill,
                    s_manager + layout::global_outline,
                    s_manager + layout::global_next
                });
                return true;
            }
            tables.push_back({
                s_manager + layout::fallback_depth,
                s_manager + layout::fallback_active,
                s_manager + layout::fallback_fill,
                s_manager + layout::fallback_outline,
                0
            });
            Range contexts;
            if (!mem_read(s_manager + layout::contexts, contexts) || !contexts.valid(layout::context_stride, 32))
                return false;
            for (auto ctx = contexts.begin; ctx < contexts.end; ctx += layout::context_stride)
            {
                tables.push_back({
                    ctx + layout::context_depth,
                    ctx + layout::context_active,
                    ctx + layout::context_fill,
                    ctx + layout::context_outline,
                    ctx
                });
            }
            Range checked;
            return mem_read(s_manager + layout::contexts, checked) && contexts == checked;
        }

        bool read_slot(const Table& table, std::uint32_t id, Slot& slot)
        {
            return mem_read(table.depth + id * 4, slot.depth)
                && mem_read(table.active + id, slot.active)
                && mem_read(table.fill + id * 16, slot.fill)
                && mem_read(table.outline + id * 16, slot.outline);
        }

        bool unallocated(const Table& table, std::uint32_t id)
        {
            std::uint32_t next = 0;
            return !table.next || (mem_read(table.next, next) && next <= id);
        }

        template <typename T>
        bool replace(std::uint64_t address, const T& expected, const T& desired)
        {
            T current{};
            return mem_read(address, current) && current == expected && mem_write(address, desired);
        }

        void restore_slot(const Lease& lease)
        {
            std::vector<Table> tables;
            if (!get_tables(tables) || std::find(tables.begin(), tables.end(), lease.table) == tables.end()
                || !unallocated(lease.table, s_id))
                return;
            const auto& t = lease.table;
            Slot current;
            if (read_slot(t, s_id, current) && current == lease.written)
            {
                replace(t.active + s_id, lease.written.active, lease.original.active);
                replace(t.depth + s_id * 4, lease.written.depth, lease.original.depth);
                replace(t.fill + s_id * 16, lease.written.fill, lease.original.fill);
                replace(t.outline + s_id * 16, lease.written.outline, lease.original.outline);
            }
        }

        bool get_registered(std::array<bool, 256>& used)
        {
            used.fill(false);
            if (s_merged)
                return true;
            std::uint64_t head = 0;
            std::uint64_t node = 0;
            if (!mem_read(s_manager + 24, head) || !head || !mem_read(head, node))
                return false;
            for (std::size_t count = 0; node != head; ++count)
            {
                std::uint64_t entry = 0;
                std::uint32_t id = 0;
                if (count >= 4096 || !node || !mem_read(node + 24, entry) || !entry
                    || !mem_read(entry + 112, id) || !mem_read(node, node))
                    return false;
                if (id < used.size())
                    used[id] = true;
            }
            return equals(s_manager + 24, head);
        }

        void stop_engine();

        bool acquire(const std::vector<Table>& tables, const Slot& appearance)
        {
            std::array<bool, 256> used{};
            if (!get_registered(used))
                return false;
            for (std::uint32_t candidate = 254; candidate >= 192; --candidate)
            {
                if (used[candidate])
                    continue;
                std::vector<Lease> leases;
                for (const auto& table : tables)
                {
                    Slot original;
                    if (!unallocated(table, candidate) || !read_slot(table, candidate, original) || original.active
                        || original.fill[3] != 0.0f || original.outline[3] != 0.0f)
                        break;
                    leases.push_back({ table, original, appearance });
                }
                if (leases.size() != tables.size())
                    continue;
                s_id = candidate;
                for (const auto& lease : leases)
                {
                    if (!session_live())
                    {
                        stop_engine();
                        return false;
                    }
                    const auto& t = lease.table;
                    auto pending = lease;
                    pending.written = lease.original;
                    s_leases.push_back(pending);
                    auto& written = s_leases.back().written;
                    if (!replace(t.depth + s_id * 4, written.depth, appearance.depth))
                    {
                        stop_engine();
                        return false;
                    }
                    written.depth = appearance.depth;
                    if (!replace(t.fill + s_id * 16, written.fill, appearance.fill))
                    {
                        stop_engine();
                        return false;
                    }
                    written.fill = appearance.fill;
                    if (!replace(t.outline + s_id * 16, written.outline, appearance.outline))
                    {
                        stop_engine();
                        return false;
                    }
                    written.outline = appearance.outline;
                    if (!replace(t.active + s_id, written.active, appearance.active))
                    {
                        stop_engine();
                        return false;
                    }
                    written.active = appearance.active;
                }
                return true;
            }
            return false;
        }

        bool leases_live(const std::vector<Table>& tables)
        {
            std::array<bool, 256> used{};
            if (tables.size() != s_leases.size() || !get_registered(used) || used[s_id])
                return false;
            for (std::size_t i = 0; i < tables.size(); ++i)
            {
                Slot current;
                if (tables[i] != s_leases[i].table || !unallocated(tables[i], s_id)
                    || !read_slot(tables[i], s_id, current) || current != s_leases[i].written)
                    return false;
            }
            return true;
        }

        bool set_appearance(const Slot& appearance)
        {
            for (auto& lease : s_leases)
            {
                const auto& t = lease.table;
                auto& written = lease.written;
                if (written == appearance)
                    continue;
                if (!session_live() || !unallocated(t, s_id))
                    return false;
                if (!replace(t.depth + s_id * 4, written.depth, appearance.depth))
                    return false;
                written.depth = appearance.depth;
                if (!replace(t.fill + s_id * 16, written.fill, appearance.fill))
                    return false;
                written.fill = appearance.fill;
                if (!replace(t.outline + s_id * 16, written.outline, appearance.outline))
                    return false;
                written.outline = appearance.outline;
            }
            return true;
        }

        bool entity_live(const Entity& entity)
        {
            if (!session_live() || !equals(entity.player + layout::model_instance(), entity.model)
                || !equals(entity.humanoid + layout::parent(), entity.model)
                || !equals(entity.part + layout::cluster_node, entity.cluster + layout::cluster_binding)
                || !equals(entity.cluster, s_base + layout::cluster_vtable)
                || !equals(entity.cluster + layout::cluster_engine, s_engine)
                || !equals(entity.cluster + layout::cluster_humanoid, entity.humanoid)
                || !equals(entity.address, s_base + layout::entity_vtable)
                || !equals(entity.address + layout::entity_cluster, entity.cluster))
                return false;
            std::array<std::uint64_t, 2> geometry{};
            std::array<std::uint64_t, 2> techniques{};
            Range indices;
            Range entities;
            if (!mem_read(entity.address + layout::entity_geometry, geometry) || geometry != entity.geometry
                || !mem_read(entity.address + layout::entity_techniques, techniques) || techniques != entity.techniques
                || !mem_read(entity.address + layout::entity_indices, indices) || indices != entity.indices
                || !mem_read(entity.cluster + layout::cluster_entities, entities) || !entities.valid(8, 512))
                return false;
            std::vector<std::uint64_t> addresses((entities.end - entities.begin) / 8);
            return !addresses.empty() && mem_read_bytes(entities.begin, addresses.data(), addresses.size() * 8)
                && std::find(addresses.begin(), addresses.end(), entity.address) != addresses.end();
        }

        bool has_technique(const Entity& entity)
        {
            Range techniques;
            if (!mem_read(entity.techniques[0], techniques) || !techniques.valid(136, 256))
                return false;
            for (auto addr = techniques.begin; addr < techniques.end; addr += 136)
            {
                std::uint32_t pass = 0;
                if (!mem_read(addr + 4, pass))
                    return false;
                if (pass >= 9 && pass <= 12)
                    return true;
            }
            return false;
        }

        bool collect(std::unordered_map<std::uint64_t, Entity>& result)
        {
            std::vector<std::uint64_t> children;
            if (!get_children(s_datamodel, children))
                return false;
            std::uint64_t players = 0;
            for (const auto child : children)
            {
                if (get_class_name(child) == "Players")
                {
                    players = child;
                    break;
                }
            }
            std::uint64_t local = 0;
            if (!players || !mem_read(players + layout::local_player(), local) || !local || !get_children(players, children))
                return false;
            std::unordered_set<std::uint64_t> clusters;
            for (const auto player : children)
            {
                if (player == local || get_class_name(player) != "Player")
                    continue;
                Entity entry;
                entry.player = player;
                std::vector<std::uint64_t> parts;
                if (!mem_read(player + layout::model_instance(), entry.model) || !entry.model || !get_children(entry.model, parts))
                    continue;
                for (const auto part : parts)
                {
                    if (get_class_name(part) == "Humanoid")
                    {
                        entry.humanoid = part;
                        break;
                    }
                }
                if (!entry.humanoid)
                    continue;
                for (const auto part : parts)
                {
                    const auto name = get_class_name(part);
                    if (name != "Part" && name != "MeshPart" && name != "UnionOperation" && name != "WedgePart" && name != "CornerWedgePart")
                        continue;
                    std::uint64_t node = 0;
                    if (!mem_read(part + layout::cluster_node, node) || node < 0x10000)
                        continue;
                    entry.part    = part;
                    entry.cluster = node - layout::cluster_binding;
                    if (!clusters.insert(entry.cluster).second || !equals(entry.cluster, s_base + layout::cluster_vtable)
                        || !equals(entry.cluster + layout::cluster_engine, s_engine)
                        || !equals(entry.cluster + layout::cluster_humanoid, entry.humanoid))
                        continue;
                    Range entities;
                    if (!mem_read(entry.cluster + layout::cluster_entities, entities) || !entities.valid(8, 512))
                        continue;
                    for (auto addr = entities.begin; addr < entities.end; addr += 8)
                    {
                        if (mem_read(addr, entry.address) && entry.address
                            && equals(entry.address, s_base + layout::entity_vtable)
                            && mem_read(entry.address + layout::entity_geometry, entry.geometry)
                            && mem_read(entry.address + layout::entity_techniques, entry.techniques)
                            && mem_read(entry.address + layout::entity_indices, entry.indices)
                            && has_technique(entry))
                        {
                            result.emplace(entry.address, entry);
                        }
                    }
                }
            }
            return session_live();
        }

        void stop_engine()
        {
            for (const auto& [addr, entity] : s_entities)
            {
                if (entity_live(entity))
                    replace(addr + layout::entity_highlight, s_id, std::uint32_t{ 0 });
            }
            s_entities.clear();
            for (const auto& lease : s_leases)
                restore_slot(lease);
            s_leases.clear();
            s_id = 0;
            g_player_count_val = 0;
            g_status = Status::Disabled;
        }

        void do_update(const Settings& settings)
        {
            if (!settings.enabled)
            {
                stop_engine();
                return;
            }

            s_base = memory->get_module_address();
            if (!s_base)
            {
                stop_engine();
                g_status = Status::WaitingForGame;
                return;
            }

            std::uint64_t engine = 0;
            std::uint64_t mgr = 0;
            std::uint64_t fake = 0;
            std::uint64_t dm = 0;
            std::uint8_t merged = 0;
            if (!mem_read(s_base + layout::visual_engine_ptr(), engine) || !engine
                || !mem_read(engine + layout::manager_offset, mgr) || !mgr
                || !mem_read(s_base + layout::fake_dm_ptr(), fake) || !fake
                || !mem_read(fake + layout::fake_dm_to_real(), dm) || !dm
                || !mem_read(s_base + layout::merge_flag, merged))
            {
                stop_engine();
                g_status = Status::WaitingForGame;
                return;
            }

            if (engine != s_engine || mgr != s_manager || dm != s_datamodel || merged != s_merged)
            {
                stop_engine();
                s_engine    = engine;
                s_manager   = mgr;
                s_datamodel = dm;
                s_merged    = merged;
            }

            std::vector<Table> tables;
            std::unordered_map<std::uint64_t, Entity> entities;
            if (!get_tables(tables) || !collect(entities))
            {
                stop_engine();
                g_status = Status::MemoryUnavailable;
                return;
            }
            if (entities.empty())
            {
                stop_engine();
                g_status = Status::WaitingForCharacters;
                return;
            }

            const auto appearance = make_appearance(settings);
            if (s_id && !leases_live(tables))
            {
                stop_engine();
                g_status = Status::WaitingForCharacters;
                return;
            }
            if (!s_id && !acquire(tables, appearance))
            {
                g_status = Status::NoAvailableIds;
                return;
            }
            if (!set_appearance(appearance))
            {
                stop_engine();
                g_status = Status::MemoryUnavailable;
                return;
            }

            for (auto it = s_entities.begin(); it != s_entities.end();)
            {
                const auto found = entities.find(it->first);
                if (found == entities.end() || found->second != it->second)
                {
                    if (entity_live(it->second))
                        replace(it->first + layout::entity_highlight, s_id, std::uint32_t{ 0 });
                    it = s_entities.erase(it);
                }
                else
                    ++it;
            }

            for (const auto& [addr, entity] : entities)
            {
                std::uint32_t current = 0;
                if (!mem_read(addr + layout::entity_highlight, current))
                    continue;
                const auto owned = s_entities.contains(addr);
                if (owned && current == s_id)
                    continue;
                if (owned)
                    s_entities.erase(addr);
                if (!current && entity_live(entity) && replace(addr + layout::entity_highlight, std::uint32_t{ 0 }, s_id))
                    s_entities.emplace(addr, entity);
            }

            std::unordered_set<std::uint64_t> players;
            for (const auto& [addr, entity] : s_entities)
                players.insert(entity.player);
            g_player_count_val = players.size();
            g_entity_count     = s_entities.size();
            g_status           = s_entities.empty() ? Status::WaitingForCharacters : Status::Active;
        }
    }

    void initialize()
    {
        running = true;
        std::thread(thread_func).detach();
    }

    void shutdown()
    {
        running = false;
        std::lock_guard lock(g_mutex);
        stop_engine();
    }

    void update(const Settings& settings)
    {
        std::lock_guard lock(g_mutex);
        g_current_settings = settings;
    }

    Status get_status()
    {
        return g_status;
    }

    const char* describe_status(Status s)
    {
        switch (s)
        {
        case Status::Disabled:              return "disabled";
        case Status::UnsupportedVersion:    return "unsupported client build";
        case Status::WaitingForGame:        return "waiting for game";
        case Status::WaitingForCharacters:  return "waiting for characters";
        case Status::NoAvailableIds:        return "no available highlight slot";
        case Status::MemoryUnavailable:     return "scene unavailable";
        case Status::Active:                return "active";
        }
        return "unknown";
    }

    std::size_t get_entity_count()
    {
        return g_entity_count;
    }

    std::size_t get_player_count()
    {
        return g_player_count_val;
    }

    void thread_func()
    {
        while (running)
        {
            // Take a copy and let go: a pass reads every character (tens of ms), and update()
            // shouldn't wait on it.
            Settings settings;
            {
                std::lock_guard lock(g_mutex);
                settings = g_current_settings;
            }
            do_update(settings);
            std::this_thread::sleep_for(std::chrono::milliseconds(settings.enabled ? 16 : 100));
        }
    }
}
