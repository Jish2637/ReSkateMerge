#include "Engine/Game/Multiplayer/msc_interface.h"
// Park mods on the dedicated server (server_parks.cpp): reading a park mod's file, and spawning,
// timing out and removing parks as the server's own objects, read the way a player's game reads
// them. The server runs over a transport that is not Steam's, with simulated players.
//
//   dingosdk_server_park_tests <Server/Test/fixtures/park_mods>
#include "Server/server_host.h"
#include "Extension/Multiplayer/Session/monotonic_clock.h"
#include "Server/moderator_help.h"
#include "Server/msc_presence.h"
#include "Engine/Game/Multiplayer/first_person_presence.h"
#include "Engine/Core/Text/word_filter.h"
#include "Engine/Game/World/world_names.h"
#include "Server/server_parks.h"
#include "Server/build_sets.h"
#include "Extension/Multiplayer/Net/pose_batch.h"
#include "Extension/Multiplayer/Net/sound_codec.h"
#include "Extension/Multiplayer/Net/wire_codec.h"
#include "Extension/Multiplayer/Session/object_state.h"
#include "Extension/Multiplayer/Session/room.h"
#include "Extension/Multiplayer/Steam/steam_transport.h"
#include "Engine/Game/Build/supported_build.h"

#include <algorithm>
#include <fstream>
#include <cstdio>
#include <ctime>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace dingosdk::multiplayer {
namespace {
// What the fake transport holds: what each player is sent, and what they send.
struct Wire {
    TransportStatus status;
    std::vector<TransportMessage> inbound;
    std::map<std::uint64_t, std::vector<std::vector<std::uint8_t>>> sent;
    std::map<std::uint64_t, std::string> closed;
};
Wire &wire() {
    static Wire value;
    return value;
}
} // namespace

// A game server's SteamID, as Steam gives the server one: never a player's.
constexpr std::uint64_t server_steam_id = (1ULL << 56) | (4ULL << 52) | 1;

struct SteamTransport::Impl {};
SteamTransport::SteamTransport() = default;
SteamTransport::~SteamTransport() = default;
bool SteamTransport::open() { return true; }
bool SteamTransport::open_game_server(void *) { return true; }
bool SteamTransport::host(unsigned) {
    wire().status.ready = wire().status.hosting = true;
    wire().status.local_id = server_steam_id;
    return true;
}
bool SteamTransport::join(std::uint64_t, std::uint32_t, std::uint16_t) { return false; }
bool SteamTransport::listen_direct(std::uint16_t) { return true; }
void SteamTransport::set_packing(unsigned) {}
std::vector<std::string> SteamTransport::take_direct_notes() { return {}; }
bool SteamTransport::set_steam_debug(bool) { return false; }
bool SteamTransport::connect_peer(std::uint64_t) { return false; }
void SteamTransport::allow_peers(std::span<const Member>) {}
bool SteamTransport::socket_test() { return true; }
void SteamTransport::stop() {}
void SteamTransport::disconnect(std::uint64_t id, const char *reason) {
    wire().closed[id] = reason ? reason : "";
    std::erase_if(wire().status.peers, [&](const TransportPeer &peer) { return peer.id == id; });
}
void SteamTransport::poll() {}
bool SteamTransport::send(std::uint64_t id, std::span<const std::uint8_t> bytes, bool, bool, TrafficLane) {
    if (std::none_of(wire().status.peers.begin(), wire().status.peers.end(), [&](const auto &peer) { return peer.id == id; })) return false;
    wire().sent[id].emplace_back(bytes.begin(), bytes.end());
    return true;
}
void SteamTransport::send_batch(std::span<TransportSend> messages) {
    for (auto &message : messages) message.sent = send(message.id, message.bytes, message.reliable, message.fresh, message.lane);
}
std::vector<TransportMessage> SteamTransport::receive() { return std::exchange(wire().inbound, {}); }
std::string SteamTransport::name(std::uint64_t) { return {}; }
const TransportStatus &SteamTransport::status() const { return wire().status; }
std::string SteamTransport::take_closed(std::uint64_t) { return {}; }
std::string SteamTransport::link_report(std::uint64_t) { return {}; }
std::int64_t SteamTransport::pending(std::uint64_t) const { return 0; }
std::string SteamTransport::relay_status() const { return "OK"; }
bool SteamTransport::set_send_rate(int) { return true; }
int SteamTransport::send_rate() const { return 900 * 1024; }
std::vector<TransportLink> SteamTransport::links() { return {}; }
bool SteamTransport::bind(void *, void *, void *) { return true; }
} // namespace dingosdk::multiplayer

namespace {
using namespace dingosdk;
using namespace dingosdk::multiplayer;
using namespace dingosdk::server;

int failures{};
void check(bool ok, const std::string &what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what.c_str());
        ++failures;
    }
}
bool has(const std::vector<std::string> &lines, std::string_view text) {
    return std::any_of(lines.begin(), lines.end(), [&](const auto &line) { return line.find(text) != std::string::npos; });
}

// One player's game, as far as the parks go: what the server sent it, read the way its session
// reads it (session_receive.cpp): an object update counts only from a source the roster names,
// with that source's epoch, through the server.
struct Game {
    std::uint64_t id{}, epoch{}, secret{}, map{}, world = 1;
    std::uint32_t sequence{};
    std::string name;
    DeltaReceiver receiver;
    std::optional<Member> server;
    ServerAnnouncement announcement;
    std::vector<std::string> timed_announcements;
    ObjectState park; // the server's objects as this game holds them
    std::map<std::uint64_t, ObjectState> player_objects;
    std::optional<std::uint32_t> object_clear_counter;
    unsigned local_clear_requests{};
    std::vector<std::string> chat;
    std::vector<Json> msc_messages;
    std::uint32_t server_chat_badge=default_server_chat_badge,server_chat_text=default_server_chat_text;
    std::vector<std::pair<std::uint32_t,std::uint32_t>> chat_colours;
    std::vector<std::uint64_t> heard_voice;
    std::vector<std::string> votes; // the roster's: the votes of the server's own that are on
    bool refused{}; // an object update the game would not have taken
    bool joined{};
    std::vector<Member> roster;
    unsigned roster_updates{};
    bool noclip{},no_bail{},boosts{},enforce_tuning{},sync_drones{};
    std::uint32_t role_permissions{};
    std::vector<std::uint64_t> typing_notices;
    std::vector<Packet> throwdowns;

    Packet header(PacketKind kind, std::uint64_t now) {
        Packet p;
        p.kind = kind;
        p.session = secret;
        p.map = map;
        p.world = world;
        p.epoch = epoch;
        p.source = id;
        p.time_us = now;
        p.sequence = ++sequence;
        p.build = supported_build::game_sha256_bytes;
        return p;
    }
    void send(const Packet &p) { wire().inbound.push_back({id, encode_wire(p), p.time_us}); }
    void hello(std::uint64_t now) {
        wire().status.peers.push_back({id, true});
        auto p = header(PacketKind::hello, now);
        p.text = name;
        send(p);
    }
    void say(std::string_view text, std::uint64_t now) {
        auto p = header(PacketKind::chat, now);
        p.text = std::string(text);
        send(p);
    }
    // Once a second: the map is loaded here (and the server hears from the player).
    void ready(std::uint64_t now) {
        auto p = header(PacketKind::world_ready, now);
        p.world_ready = true;
        send(p);
    }
    void read() {
        for (const auto &bytes : std::exchange(wire().sent[id], {})) {
            if (pose_batch::is_batch(bytes) || pose_batch::Ack::read(bytes) || sound_codec::is_sound(bytes)) continue;
            bool missing{};
            const auto p = receiver.receive(bytes, missing, 0);
            if (!p) continue;
            if (p->kind == PacketKind::msc) msc_messages.push_back(Json::parse(p->text));
            if (p->kind == PacketKind::welcome) joined = true;
            if (p->kind == PacketKind::world_state) {
                if (p->world > world) { park = {}; player_objects.clear(); } // stock clear_world
                world = p->world;
                if (p->map) map = p->map;
            }
            if (p->kind == PacketKind::roster) {
                ++roster_updates;
                noclip=p->guest_noclip;no_bail=p->guest_no_bail;boosts=p->guest_boosts;enforce_tuning=p->enforce_tuning;sync_drones=p->sync_drones;
                if (p->announcement.id && p->announcement.id != announcement.id)
                    timed_announcements.push_back(p->announcement.text);
                announcement = p->announcement;
                if (object_clear_counter && *object_clear_counter != p->object_clears) ++local_clear_requests;
                object_clear_counter = p->object_clears;
                roster=p->members;server_chat_badge=p->chat_badge;server_chat_text=p->chat_text;
                for (const auto &member : p->members)
                    if (member.id == p->source) server = member;
                votes.clear();
                for (const auto &v : p->server_custom_votes) votes.push_back(v.name);
            }
            if (p->kind == PacketKind::chat) {chat.push_back(p->text);chat_colours.push_back({server_chat_badge,server_chat_text});}
            if(p->kind==PacketKind::throwdown) {
                const auto member=std::ranges::find_if(roster,[&](const auto &m){return m.id==p->source;});
                if(!server || member==roster.end() || !routed_source(*p,*member,server->id,false,server->id))refused=true;
                throwdowns.push_back(*p);
            }
            if(p->kind==PacketKind::permissions)role_permissions=p->role_permissions;
            if(p->kind==PacketKind::typing)typing_notices.push_back(p->source);
            if (p->kind == PacketKind::voice) heard_voice.push_back(p->source);
            if (p->kind == PacketKind::objects) {
                const auto member = std::find_if(roster.begin(), roster.end(), [&](const auto &m) { return m.id == p->source; });
                if (!server || member == roster.end() || !routed_source(*p, *member, server->id, false, server->id)) refused = true;
                else if ((p->source == server->id ? park : player_objects[p->source]).receive(p->objects) == ObjectState::Result::invalid) refused = true;
            }
        }
    }
};

int run(const std::filesystem::path &fixtures) {
    // ---- Reading park mods -------------------------------------------------------------------
    {
        const auto street = load_park_mod(fixtures, {"street", "popular skate 2 street park", "San Vansterdam", "bam"});
        check(street.key == "bam" && street.objects.size() == 8 && street.skipped.empty(), "The street park did not load whole");
        const auto &first = street.objects.front();
        check(first.item == "own_bkpads_generic_padlow_00001" && first.position == std::array<float, 3>{-583.f, 1948.35f, 1651.f} &&
                  first.rotation == std::array<float, 4>{0.f, .3827f, 0.f, .9239f} && first.scale == 1.f,
              "The street park's first object is not the file's");
        // No key: the mod's only park.
        check(load_park_mod(fixtures, {"street", "popular skate 2 street park", "San Vansterdam", ""}).key == "bam",
              "A mod's only park was not found without a key");
        const auto mixed = load_park_mod(fixtures, {"plaza", "mixed park", "Isle of Grom", "grom"});
        check(mixed.objects.size() == 2 && mixed.skipped.size() == 3, "Objects players' games would not show were not skipped");
        check(mixed.skipped.size() == 3 && mixed.skipped[0] == "#2 vehicle_car_00001" && mixed.skipped[2].starts_with("#4 "),
              "Skipped objects are not named by their place and item");
        check(mixed.objects.size() == 2 && mixed.objects[1].scale == 2.5f, "A park object's size was lost");
        const auto refused = [&](ParkMod park, std::string_view why) {
            try {
                load_park_mod(fixtures, park);
            } catch (const std::exception &e) {
                if (std::string(e.what()).find(why) != std::string::npos) return true;
                std::fprintf(stderr, "  (said: %s)\n", e.what());
            }
            return false;
        };
        // Said without the server's own paths: the reason can reach chat (an admin, a vote).
        check(refused({"x", "not installed", "San Vansterdam", "bam"}, "The mod folder \"not installed\" is not in the Mods folder."),
              "A missing mod folder was not reported, or named the server's path");
        check(refused({"x", "popular skate 2 street park", "Isle of Grom", "grom"}, "has no park for grom"), "A missing park file was not reported");
        check(refused({"x", "two maps", "San Vansterdam", ""}, "set \"key\""), "A mod with parks for two maps did not ask for a key");
        check(load_park_mod(fixtures, {"x", "two maps", "Super Ultra Mega Resort", "mpr"}).objects.size() == 3, "A park chosen by its key did not load");
        check(refused({"x", "broken", "San Vansterdam", "bam"}, "cannot be read"), "A park file that is not JSON was not reported");
        check(refused({"x", "not a park", "San Vansterdam", "bam"}, "not a ReSkate park"), "A file without the park format was accepted");
        check(refused({"x", "../park_mods/broken", "San Vansterdam", "bam"}, "not a mod folder name"), "A path was taken for a folder");
    }

    // ---- The server --------------------------------------------------------------------------
    const auto empty = std::filesystem::temp_directory_path() / "reskate_server_park_tests_mods";
    std::filesystem::create_directories(empty);
    load_levels(empty); // the retail maps
    ServerConfig config;
    config.file = std::filesystem::temp_directory_path() / "reskate_server_park_tests" / "ReSkateServer.json";
    // A copy of the fixtures the test may change, as an owner updates a mod while the server runs.
    const auto mods = std::filesystem::temp_directory_path() / "reskate_server_park_tests_live";
    std::filesystem::remove_all(mods);
    std::filesystem::copy(fixtures, mods, std::filesystem::copy_options::recursive);
    config.mods = mods;
    config.auto_update = false;
    config.park_mods = {{"street", "popular skate 2 street park", "San Vansterdam", "bam"},
                        {"street2", "popular skate 2 street park", "San Vansterdam", "bam"},
                        {"street3", "popular skate 2 street park", "San Vansterdam", ""},
                        {"plaza", "mixed park", "Isle of Grom", "grom"},
                        {"gone", "not installed", "San Vansterdam", "bam"}};
    config.park_limits = {2, 20};
    config.votes.cooldown = 0;
    CustomVote park_vote{"park", "Spawn a park for 30 min", "park-mod add {arg} 30", {"street", "plaza"}};
    CustomVote unpark_vote{"unpark", "Remove a park", "park-mod remove {arg}", {"street", "plaza"}};
    config.votes.custom = {park_vote, unpark_vote};
    check(config_error(config).empty(), "The test's config was refused: " + config_error(config));
    std::vector<std::string> log;
    {
        ObjectAudit seed;
        const auto wall = static_cast<std::int64_t>(std::time(nullptr));
        seed.open(config.file.parent_path() / "data" / "object-history.jsonl", wall, {});
        seed.record({{wall - 750, 76561198000998881ULL, 1, "OlderHistory", "own_bk_old_history", "San Vansterdam", false},
                     {wall - 450, 76561198000998882ULL, 2, "RecentHistory", "own_bk_recent_history", "San Vansterdam", false}}, wall);
    }
    SteamTransport transport;
    Host host(config, transport, [&](const std::string &line) { log.push_back(line); });
    check(host.command("whoplaced").find("RecentHistory") != std::string::npos && host.command("whoplaced").find("OlderHistory") == std::string::npos,
          "Default history range is not ten minutes");
    check(host.command("whoplaced 5").starts_with("No recorded placements"), "Explicit history range is not in minutes");
    check(host.command("whoplaced 15").find("OlderHistory") != std::string::npos, "Explicit 15-minute history range omitted older record");
    std::string error;
    if (!host.start(error)) {
        std::fprintf(stderr, "The server did not start: %s\n", error.c_str());
        return 1;
    }
    const auto invite = parse_invite(host.invite());
    if (!invite) {
        std::fprintf(stderr, "No invite.\n");
        return 1;
    }
    std::map<std::uint64_t, Game> games;
    const auto add_game = [&](std::uint64_t id, std::string name) -> Game & {
        auto &game = games[id];
        game.id = id;
        game.epoch = 5000 + id % 1000;
        game.secret = invite->secret;
        game.map = map_hash(map_destination(config.map));
        game.name = std::move(name);
        return game;
    };
    std::uint64_t now = 1000000000ULL, next_ready = now;
    const auto run_for = [&](std::uint64_t microseconds) {
        for (const auto end = now + microseconds; now < end; now += 50000) {
            if (now >= next_ready) {
                next_ready = now + 1000000;
                for (auto &[id, game] : games)
                    if (game.joined) game.ready(now);
            }
            host.tick(now);
            for (auto &[id, game] : games) game.read();
        }
    };
    const auto command = [&](std::string_view line, std::uint64_t admin = 0) {
        const auto reply = host.command(line, admin);
        if (!admin) log.push_back(reply); // as the console writes its replies to the log
        return reply;
    };
    const auto logged = [&](std::string_view text) { return has(log, text); };

    // Roles (Server/roles.h): the player is an admin, whose "*" gives them park-mod.
    const std::uint64_t admin_id = 76561198000000001ULL;
    config.roles = default_roles();
    config.roles.players[admin_id] = "admin";
    auto &a = add_game(admin_id, "Robin");
    a.hello(now);
    run_for(2000000);
    check(a.joined && a.server && a.server->id == server_steam_id, "The player did not join, or the roster does not name the server");

    // Exercise the real announcement timer and delivery path, not just the shuffle bag.
    config.announcements = {{"Shuffle A", "Shuffle B", "Shuffle C"}, 1};
    command("announcements interval 1");
    std::string previous_announcement;
    for (unsigned cycle = 0; cycle < 3; ++cycle) {
        std::set<std::string> seen;
        for (unsigned i = 0; i < 3; ++i) {
            run_for(cycle == 0 && i == 0 ? 61000000 : 60000000);
            check(a.timed_announcements.size() == cycle * 3 + i + 1, "Timed announcement did not reach the player exactly once (received " + std::to_string(a.timed_announcements.size()) + ", expected " + std::to_string(cycle * 3 + i + 1) + ")");
            const auto line = a.timed_announcements.empty() ? std::string() : a.timed_announcements.back();
            check(line != previous_announcement && seen.insert(line).second, "Real timer repeated an announcement before exhaustion");
            previous_announcement = line;
        }
        check(seen == std::set<std::string>{"Shuffle A", "Shuffle B", "Shuffle C"}, "Real timer omitted a configured announcement");
    }
    config.announcements = {};

    // Spawned: the server's own objects, which the player's game takes from the server.
    auto reply = command("park-mod add street 2");
    check(reply == "[parks] street spawned (8 objects, 2 min) by console", "Spawn reply: " + reply);
    const auto spawned_at = now;
    run_for(1000000);
    check(!a.refused && a.park.objects().size() == 8, "The player's game did not receive the park as the server's objects");
    check(has(a.chat, "The street park is here for 2 min."), "Players were not told of the park");

    // What cannot be spawned, and nothing is spawned of it.
    check(command("park-mod add street").find("already spawned") != std::string::npos, "A park was spawned twice");
    reply = command("park-mod add plaza");
    check(reply.find("[parks] plaza could not be spawned: plaza belongs on Isle of Grom") == 0, "A park was spawned on another map: " + reply);
    check(command("park-mod add nothing").find("no park mod called") != std::string::npos, "An unknown park was not refused");
    reply = command("park-mod add gone");
    check(reply.find("[parks] gone could not be spawned: The mod folder \"not installed\"") == 0, "A missing mod was not an error line: " + reply);
    check(command("park-mod add street2 street3").find("at most 2 parks") != std::string::npos, "The parks limit was not kept");
    check(command("park-mod add street2 0").find("1 to 1440") != std::string::npos, "0 minutes was accepted");

    // Several at once, each with IDs of its own.
    reply = command("park-mod add street2");
    check(reply == "[parks] street2 spawned (8 objects, until removed) by console", "Second park: " + reply);
    run_for(1000000);
    check(!a.refused && a.park.objects().size() == 16, "Two parks did not reach the player as one layout");
    reply = command("park-mod list");
    check(reply.find("2 of 2 spawned, 16 of 20 objects") != std::string::npos && reply.find("street  \"popular skate 2 street park\" (bam)") != std::string::npos &&
              reply.find("2 min left") != std::string::npos && reply.find("until removed") != std::string::npos,
          "list: " + reply);

    // Roles: a moderator's role does not list park-mod; a role that lists it gives it alone.
    {
        const std::uint64_t mod_id = 76561198000000003ULL;
        config.roles.players[mod_id] = "moderator";
        auto &m = add_game(mod_id, "Moddy");
        m.hello(now);
        run_for(2000000);
        m.say("/park-mod list", now);
        run_for(500000);
        check(has(m.chat, "Your role (moderator) cannot use park-mod."), "A moderator could use park-mod");
        Role builder;
        builder.name = "builder";
        builder.rank = 20;
        builder.commands = {"park-mod"};
        config.roles.list.push_back(builder);
        config.roles.settle();
        config.roles.players[mod_id] = "builder";
        m.say("/park-mod", now); // not the same line again: chat turns repeats away
        run_for(500000);
        check(has(m.chat, "Park mods (2 of 2 spawned"), "A role listing park-mod could not use it");
        check(!has(m.chat, "Your role (builder) cannot use"), "A role listing park-mod was refused it");
        wire().status.peers.erase(std::remove_if(wire().status.peers.begin(), wire().status.peers.end(),
                                                 [&](const auto &peer) { return peer.id == mod_id; }),
                                  wire().status.peers.end());
        games.erase(mod_id);
        run_for(1000000);
    }

    // An admin in game: by their name.
    a.say("/park-mod remove street2", now);
    run_for(500000);
    check(logged("[parks] street2 removed (by Robin)"), "An admin's removal was not logged by name");
    check(has(a.chat, "[parks] street2 removed (by Robin)"), "The admin was not told");
    run_for(500000);
    check(a.park.objects().size() == 8, "A removed park stayed on the player's map");
    // The objects limit: all or none.
    config.park_limits.parks = 3;
    reply = command("park-mod add street2 street3");
    check(reply.find("that makes 24 park objects, and at most 20") != std::string::npos, "The objects limit was not kept: " + reply);
    check(command("park-mod list").find("1 of 3 spawned, 8 of 20 objects") != std::string::npos, "Half of a refused add was spawned");
    config.park_limits = {3, 512};

    // Expiry: a minute's warning, then gone.
    run_for(spawned_at + 61000000 - now);
    check(has(a.chat, "The street park goes in a minute."), "No warning a minute before a park went");
    check(!logged("removed (expired)"), "A park went early");
    run_for(spawned_at + 121000000 - now);
    check(logged("[parks] street removed (expired)") && has(a.chat, "The street park is gone."), "An expired park was not removed");
    check(a.park.revision() && a.park.objects().empty(), "An expired park stayed on the player's map");

    // Players vote for one (the owner's custom votes run park-mod as the console).
    a.say("/vote park street", now);
    run_for(1000000);
    check(logged("[parks] street spawned (8 objects, 30 min) by a vote"), "A vote did not spawn the park");
    a.say("/vote unpark street", now);
    run_for(1000000);
    check(logged("[parks] street removed (by a vote)"), "A vote did not remove the park");
    a.say("/vote park plaza", now);
    run_for(1000000);
    check(has(a.chat, "The plaza park could not be spawned: plaza belongs on Isle of Grom"), "A vote's failure did not reach chat");

    // clear, and a player who joins late is sent the park, and it stays when they leave.
    command("park-mod add street3");
    auto &b = add_game(76561198000000002ULL, "Late");
    b.hello(now);
    run_for(2000000);
    check(!b.refused && b.park.objects().size() == 8, "A player who joined later was not sent the park");
    wire().status.peers.erase(std::remove_if(wire().status.peers.begin(), wire().status.peers.end(),
                                             [&](const auto &peer) { return peer.id == b.id; }),
                              wire().status.peers.end());
    games.erase(b.id);
    run_for(2000000);
    check(command("park-mod list").find("1 of 3 spawned") != std::string::npos, "A park went with a player");
    reply = command("park-mod clear");
    check(reply == "[parks] street3 removed (by console)", "clear: " + reply);
    check(command("park-mod clear") == "No parks are spawned.", "clear with nothing spawned");

    // Live: a spawned park's file read again, in place, keeping its timer.
    const auto street_file = mods / "popular skate 2 street park" / "parks" / "bam.park.json";
    command("park-mod add street 10");
    run_for(1000000);
    std::ofstream(street_file, std::ios::binary | std::ios::trunc) << R"({"format": "reskate-park", "schema_version": 1, "revision": 1,
        "maps": {"bam": [{"id": 1, "item": "own_bkpads_generic_padlow_00001", "position": [0, 10, 0], "rotation": [0, 0, 0, 1]},
                         {"id": 2, "item": "own_bkpads_generic_padlow_00001", "position": [4, 10, 0], "rotation": [0, 0, 0, 1]},
                         {"id": 3, "item": "own_bkpads_generic_padlow_00001", "position": [8, 10, 0], "rotation": [0, 0, 0, 1]}]}})";
    reply = command("park-mod reload street");
    check(reply == "[parks] street reloaded (3 objects) by console", "reload: " + reply);
    run_for(1000000);
    check(!a.refused && a.park.objects().size() == 3, "A reloaded park did not reach the player");
    check(has(a.chat, "The street park was updated."), "Players were not told of the update");
    check(command("park-mod list").find("3 objects, 10 min left") != std::string::npos, "A reload lost the park's timer");
    check(command("park-mod reload street2").find("street2 is not spawned") != std::string::npos, "A park that is not up was reloaded");
    check(command("park-mod reload nothing").find("no park mod called") != std::string::npos, "An unknown park was reloaded");
    std::ofstream(street_file, std::ios::binary | std::ios::trunc) << "{ not json";
    check(command("park-mod reload street").find("[parks] street could not be reloaded: ") == 0, "A broken file was reloaded");
    run_for(1000000);
    check(a.park.objects().size() == 3, "A failed reload changed the park");
    std::filesystem::copy_file(fixtures / "popular skate 2 street park" / "parks" / "bam.park.json", street_file,
                               std::filesystem::copy_options::overwrite_existing);
    command("park-mod clear");

    // Live: parks set up from the console or an admin, saved to the config, no restart.
    reply = command("park-mod config add newpark | popular skate 2 street park | san vansterdam | bam");
    check(reply == "[parks] newpark set up (\"popular skate 2 street park\", bam on San Vansterdam) by console", "config add: " + reply);
    {
        const auto saved = load_config(config.file);
        check(!saved.park_mods.empty() && saved.park_mods.back().name == "newpark" && saved.park_mods.back().map == "San Vansterdam" &&
                  saved.park_mods.back().folder == "popular skate 2 street park" && saved.park_mods.back().key == "bam",
              "A park set up live was not saved to the config");
    }
    check(command("park-mod add newpark") == "[parks] newpark spawned (8 objects, until removed) by console", "A park set up live did not spawn");
    check(command("park-mod config add newpark | popular skate 2 street park | San Vansterdam").find("already set up") != std::string::npos,
          "A park was set up twice");
    check(command("park-mod config add New Park! | x | San Vansterdam").find("could not be set up: park_mods") != std::string::npos,
          "A bad park name was set up");
    check(command("park-mod config add elsewhere | x | Nowhere").find("is not a single known map") != std::string::npos, "A park on an unknown map was set up");
    check(command("park-mod config add later | ../x | San Vansterdam").find("folder must be") != std::string::npos, "A path was set up as a folder");
    check(command("park-mod config add half | x").find("park-mod config add <name>") == 0, "A park without a map was set up");
    reply = command("park-mod config add later | copied later | Isle of Grom");
    check(reply.find("its folder is not in the Mods folder yet") != std::string::npos, "A missing folder was not mentioned: " + reply);
    a.say("/park-mod config remove later", now);
    run_for(500000);
    check(logged("[parks] later no longer set up (by Robin)"), "An admin could not remove a park from the setup");
    reply = command("park-mod config remove newpark");
    check(reply == "[parks] newpark removed (by console)\n[parks] newpark no longer set up (by console)", "config remove: " + reply);
    run_for(1000000);
    check(a.park.objects().empty(), "A park removed from the setup stayed on the map");
    {
        const auto saved = load_config(config.file);
        check(std::none_of(saved.park_mods.begin(), saved.park_mods.end(),
                           [](const auto &park) { return park.name == "newpark" || park.name == "later"; }),
              "A park removed from the setup is still in the config");
    }
    check(command("park-mod config remove newpark").find("no park mod called") != std::string::npos, "A park was removed from the setup twice");

    // A map change takes the parks with it.
    command("park-mod add street");
    run_for(1000000);
    command("map Isle of Grom");
    check(logged("[parks] street removed (map change)"), "A map change did not remove the park");
    run_for(3000000);
    check(a.park.objects().empty(), "A park stayed after the map changed");
    reply = command("park-mod add plaza 5");
    check(reply.starts_with("[parks] plaza spawned (2 objects, 5 min) by console\n[parks] plaza: skipped 3 object(s)"), "plaza: " + reply);
    run_for(1000000);
    check(!a.refused && a.park.objects().size() == 2, "The park on the new map did not reach the player");
    const auto &objects = a.park.objects();
    check(std::any_of(objects.begin(), objects.end(), [](const auto &row) { return row.second.scale == 2.5f; }), "A resized object lost its size");
    check(command("park-mod add street").find("belongs on San Vansterdam") != std::string::npos, "A park for the old map was spawned");
    // A map mod copied in while the server runs is known after "maps reload".
    {
        check(command("maps").find("Brand New") == std::string::npos, "A map that is not installed is listed");
        std::filesystem::create_directories(mods / "someone-Brand_New");
        std::ofstream(mods / "someone-Brand_New" / "reskate-levels.json", std::ios::binary)
            << R"({"schema":1,"levels":[{"asset":"levels/game/dingolevel_reskate_brandnew/dingolevel_reskate_brandnew","displayName":"Brand New"}]})";
        reply = command("maps reload");
        check(reply.find("new: Brand New") != std::string::npos, "maps reload: " + reply);
        check(command("maps").find("Brand New") != std::string::npos && find_level("Brand New"), "A map copied in was not known after a reload");
        check(command("maps reload").find("none new") != std::string::npos, "A second reload found the same map new again");
        // A custom map goes by its own name: its level is dingolevel_reskate_<map>, and "reskate"
        // is no part of what is shown or typed (typed the old way, it still means the map).
        check(dingosdk::world_level_name("levels/game/dingolevel_reskate_brandnew/dingolevel_reskate_brandnew") == "brandnew" &&
                  dingosdk::world_level_name("Levels/Game/BAM_LevelRoot/BAM_LevelRoot") == "San Vansterdam",
              "A custom map's name still starts with reskate, or a game map's name changed");
        const auto *brand_new = find_level("Brand New");
        check(brand_new && find_level("brandnew") == brand_new && find_level("reskate brandnew") == brand_new &&
                  find_level("RESKATE brand") == brand_new && !find_level("reskate"),
              "A custom map was not found by its own name, or by the old one");
    }
    // Votes are decided by those who vote, at the end of their time: with three players on, one
    // yes and nobody else voting passes; a yes and a no fails (60% of the votes cast is needed);
    // and two yes passes at once, since the third player could not change it.
    {
        auto &c = add_game(76561198000000003ULL, "Cee");
        auto &d = add_game(76561198000000004ULL, "Dee");
        c.map = d.map = a.map; // the map and world the server is on now
        c.world = d.world = a.world;
        c.hello(now);
        d.hello(now);
        run_for(2000000);
        check(c.joined && d.joined, "The players for the votes did not join");
        const auto said = [&](std::string_view text) {
            return std::count_if(a.chat.begin(), a.chat.end(), [&](const std::string &line) { return line.find(text) != std::string::npos; });
        };
        const auto passed = said("passed ("), failed = said("failed ("), alone = said("passed (1 yes, 0 no, 1 needed of 1 vote)");
        a.say("/vote unpark street", now);
        run_for(5000000);
        check(said("passed (") == passed && said("failed (") == failed, "A vote two players had not voted in was decided before its time");
        run_for(27000000);
        check(said("passed (1 yes, 0 no, 1 needed of 1 vote)") == alone + 1, "A vote did not pass on the votes cast");
        a.say("/vote unpark street", now);
        run_for(1000000);
        c.say("/no", now);
        run_for(5000000);
        check(said("failed (") == failed && said("passed (") == passed + 1, "A vote a player could still pass was decided before its time");
        run_for(27000000);
        check(said("failed (1 yes, 1 no, 2 needed of 2 votes)") == 1, "A vote did not fail on the votes cast");
        a.say("/vote unpark street", now);
        run_for(1000000);
        d.say("/yes", now);
        run_for(1000000);
        check(said("passed (2 yes, 0 no, 2 needed of 2 votes)") == 1, "A vote nobody could change any more was not decided at once");
    }
    // The rotation: the minutes left are announced, the vote's maps are drawn a minute ahead,
    // and the vote decides: staying adds time, a map changes to it.
    {
        const auto said = [&](std::string_view text) {
            return std::count_if(a.chat.begin(), a.chat.end(), [&](const std::string &line) { return line.find(text) != std::string::npos; });
        };
        auto &c = games.at(76561198000000003ULL);
        auto &d = games.at(76561198000000004ULL);
        command("rotation vote on");
        command("rotation stay 2");
        check(command("rotation 3").find("A vote for the next map") != std::string::npos, "rotation with a vote: " + command("rotation"));
        run_for(61000000); // two minutes left: nothing of the vote yet
        check(!said("Vote for the next map"), "The vote for the next map started early");
        run_for(120000000); // the time is up: the vote runs, its answers three maps, any, and staying
        check(said("Vote for the next map") == 1 && said("/4 Random map  /5 Stay here") == 1, "The vote for the next map did not start as it should");
        a.say("/5", now);
        run_for(1000000);
        check(!said("The vote keeps"), "The vote ended before everyone had voted");
        run_for(31000000);
        check(said("The vote keeps") == 1 && said("for 2 more minutes") == 1, "Staying did not win the vote");
        check(command("rotation").find("in about 2 min") != std::string::npos, "Staying did not add its time: " + command("rotation"));
        // "/vote extend": the server's own vote for the same, at any time. It is in the votes games list.
        command("rotation stay 4");
        run_for(1000000);
        check(std::find(a.votes.begin(), a.votes.end(), "extend") != a.votes.end(), "The extend vote is not among the votes games are told of");
        a.say("/vote extend", now);
        run_for(1000000);
        c.say("/yes", now);
        d.say("/yes", now);
        run_for(1000000);
        check(said("more minutes passed (") == 1, "The vote to extend the map did not pass");
        reply = command("rotation");
        check(reply.find("in about 5 min") != std::string::npos, "/vote extend did not add its time: " + reply); // a minute was left, and four more
        command("votes extend off");
        check(command("vote extend") == "Extend votes are off on this server.", "An extend vote started while it was off: " + command("vote extend"));
        command("votes extend on");
        command("rotation stay 2");
        run_for(180000000);
        // The next time everyone votes for the first map: the vote ends at once and the map changes.
        run_for(121000000);
        check(said("Vote for the next map") == 2, "The vote did not come round again after staying");
        const auto before = said("Next map: ");
        a.say("/1", now);
        c.say("/1", now);
        d.say("/1", now);
        run_for(1000000);
        check(said("Next map: ") == before + 1 || logged("[rotation] Next map: "), "The vote did not change the map once everyone had voted");
        // "rotation extend" gives the map in play more time and says so.
        run_for(3000000);
        reply = command("rotation extend 5");
        check(reply.find("in about 7 min") != std::string::npos || reply.find("in about 8 min") != std::string::npos, "rotation extend: " + reply);
        command("rotation off");
        command("rotation vote off");
    }
    // Presence is optional: all unmodified-client tests above ran without it.
    config.name="-MSC-Midnight Skate Club-24/7-Smoke-Sesh-VC-Build-Trainer";
    auto& observer=add_game(76561198000999991ULL,"Ordinary");
    observer.secret=parse_invite(host.invite())->secret;observer.map=a.map;observer.world=a.world;
    observer.hello(now);run_for(2000000);
    check(observer.joined,"ordinary client failed to join MSC server: "+wire().closed[observer.id]);
    check(msc_presence::display_name(std::string(64,'x'),true,64).size()==64,"long presence name exceeded limit");
    check(valid_member_name(msc_presence::display_name(std::string(58,'x')+"\xE2\x82\xAC",true,64)),"presence split UTF-8");
    bool parsed{};
    check(msc_presence::parse_report("/jish-fp 1 4294967295",parsed) && parsed,"maximum report sequence rejected");
    check(!msc_presence::parse_report("/jish-fp 1 4294967296",parsed) &&
          !msc_presence::parse_report("/jish-fp 1 extra",parsed),"malformed report accepted");
    a.hello(now);run_for(2000000);
    const auto chat_before=a.chat.size();
    a.hello(now);run_for(2000000);
    check(a.chat.size()==chat_before,"presence added a chat notice");
    const auto shown=[&](){for(const auto& member:a.roster)if(member.id==a.id)return member.name;return std::string{};};
    a.say("/jish-fp 1 1",now);run_for(2000000);
    check(shown()=="[FP] Robin","presence not in ordinary roster");
    check(std::any_of(observer.roster.begin(),observer.roster.end(),[&](const auto& member){return member.id==a.id && member.name=="[FP] Robin";}),
          "ordinary observing client did not receive prefix");
    check(!has(a.chat,"/jish-fp 1 1"),"presence leaked into chat");
    a.say("/jish-fp 1 2",now);run_for(2000000);
    check(shown()=="[FP] Robin","heartbeat stacked prefix");
    for(std::uint32_t serial=10;serial<18;++serial) {
        a.say(first_person_report(true,serial),now);run_for(5000000);
        check(shown()=="[FP] Robin","client heartbeat failed to sustain presence beyond expiry");
        check(std::any_of(observer.roster.begin(),observer.roster.end(),[&](const auto& member){return member.id==a.id && member.name=="[FP] Robin";}),
            "observer lost FP during five-second client heartbeats");
    }
    a.say("/jish-fp 0 3",now);run_for(2000000);
    check(shown()=="Robin","off did not restore original");
    auto forged=observer.header(PacketKind::chat,now);forged.source=a.id;forged.text="/jish-fp 1 99";
    observer.send(forged);run_for(2000000);
    check(shown()=="Robin" && wire().closed.contains(observer.id),"forged presence bypassed sender validation");
    a.say("/jish-fp invalid",now);run_for(2000000);
    check(shown()=="Robin","malformed report changed state");
    a.say("/jish-fp 1 4",now);run_for(2000000);run_for(16000000);
    check(shown()=="Robin","stale status did not expire");
    config.name="Other server";a.say("/jish-fp 1 5",now);run_for(2000000);
    check(shown()=="Robin","other server accepted presence");
    check(a.chat.size()==chat_before,"presence feature posted chat output");
    // Staff object moderation over the same unmodified protocol/chat/object codecs.
    {
        const std::uint64_t mod_id = 76561198000990001ULL, builder_id = 76561198000990002ULL, watcher_id = 76561198000990003ULL;
        config.roles.players[mod_id] = "moderator";
        auto &mod = add_game(mod_id, "ObjectMod");
        auto &builder = add_game(builder_id, "Buildy");
        auto &watcher = add_game(watcher_id, "Watcher");
        for (auto *g : {&mod, &builder, &watcher}) { g->world = a.world; g->map = a.map; g->secret = a.secret; g->hello(now); }
        run_for(2000000);
        check(mod.joined && builder.joined && watcher.joined, "Object moderation test players did not join");
        const auto pose = [&](Game &g, float x = 0.f) {
            auto p = g.header(PacketKind::pose, now); p.pose.root.position = {x, 0, 0}; p.pose.skater.push_back(p.pose.root); g.send(p);
            run_for(250000);
        };
        const auto upload = [&](std::uint64_t revision, std::vector<NetworkObject> objects) {
            auto p = builder.header(PacketKind::objects, now); p.objects.revision = revision; p.objects.objects = std::move(objects); builder.send(p);
            run_for(500000);
        };
        std::vector<NetworkObject> layout;
        for (unsigned i = 1; i <= 14; ++i) layout.push_back({i, i <= 10 ? "own_bk_rail_01" : "own_bk_ramp_01", {i == 10 ? 6.f : float(i) / 10.f, 0, 0}});
        upload(1, layout);
        const auto staff_before = a.chat.size();
        mod.say("/whoplaced 2", now); run_for(2000000);
        check(has(mod.chat, "Placements:") && has(mod.chat, "Buildy - bk_"), "Moderator could not query history privately");
        check(!has(mod.chat, std::to_string(builder_id)), "SteamID leaked into staff chat");
        check(a.chat.size() == staff_before && !has(watcher.chat, "Placements:"), "History reply was not private");
        check(command("whoplaced 2", watcher_id).find("not an admin") != std::string::npos, "Ordinary player accessed history");
        check(command("delobj Buildy", watcher_id).find("not an admin") != std::string::npos, "Ordinary player accessed deletion");
        check(command("whoplaced 1441", mod_id).starts_with("Usage:"), "History accepted >1440 minutes");
        check(command("whoplaced nan", mod_id).starts_with("Usage:"), "History accepted NaN");
        check(command("whoplaced 0.5", mod_id).starts_with("Placements:"), "Fractional minutes refused");
        check(command("whoplaced", mod_id).starts_with("Placements:"), "No-argument history query failed");
        check(command("whoplaced", mod_id).find("Next: /whoplaced 10 2") != std::string::npos, "Default history pagination did not keep ten-minute range");
        pose(mod);
        check(command("delobj", mod_id).starts_with("Usage:"), "Empty deletion command did not show usage");
        const auto unfiltered = command("nearobj", mod_id);
        check(unfiltered.find("bk_rail_01") != std::string::npos && unfiltered.find("Buildy") != std::string::npos, "Bare nearobj omitted objects/owners");
        check(command("nearobj page 2", mod_id).find("bk_ramp_01") != std::string::npos, "Bare nearobj filtered out ramp objects");
        const auto near = command("nearobj RAIL", mod_id);
        check(near.find("#1 bk_rail_01 | Buildy | 0.1 m") != std::string::npos && near.find("page 1/2") != std::string::npos, "Partial matching, distance sorting or pagination failed: " + near);
        check(near.find("ramp") == std::string::npos && near.find(std::to_string(builder_id)) == std::string::npos, "Preview filter/identity display incorrect");
        const auto page2 = command("nearobj page 2", mod_id);
        check(page2.find("#9 bk_rail_01") != std::string::npos && page2.find("#10 ") == std::string::npos, "5m boundary or stable preview indices wrong");
        check(near.find("clears ALL their objects") != std::string::npos, "Preview did not warn about full owner clear");
        const std::uint64_t similar_id = 76561198000990004ULL;
        auto &similar = add_game(similar_id, "Buildy Friend");
        similar.world = mod.world; similar.map = mod.map; similar.secret = mod.secret; similar.hello(now); run_for(1000000); pose(mod);
        check(command("delobj Build", mod_id).starts_with("Multiple players match"), "Ambiguous partial name was not refused");
        check(command("delobj Buildy Friend", mod_id).find("has no placed objects") != std::string::npos, "Multiword exact name failed");
        check(command("delobj \"Buildy Friend\"", mod_id).find("has no placed objects") != std::string::npos, "Quoted multiword name failed");
        check(command("delobj NobodyHere", mod_id).starts_with("No connected player"), "Unknown name was not refused");
        pose(mod);
        const auto numbered = command("nearobj rail", mod_id);
        check(numbered.find("snapshot lasts 3 min") != std::string::npos && numbered.find("/delobj #") != std::string::npos, "Snapshot duration or numbered deletion instructions missing");
        const auto snapshot_started = now;
        const auto before_single_clears = builder.local_clear_requests;
        pose(mod, 40.f);
        check(command("delobj #1", mod_id).find("outside 5 m") != std::string::npos, "Numbered deletion ignored current distance");
        pose(mod);
        run_for(120000000); pose(mod);
        check(command("delobj #1", mod_id).starts_with("Removed"), "Numbered player-object deletion failed after two minutes");
        run_for(500000);
        check(a.player_objects[builder_id].objects().size() == 13 && !a.player_objects[builder_id].objects().contains(1) && a.player_objects[builder_id].objects().contains(2), "Numbered deletion removed extra objects or failed ordinary client delta delivery");
        check(builder.local_clear_requests == before_single_clears, "Single deletion used an owner-wide local clear");
        check(has(builder.chat, "Server staff have removed one of your objects") && !has(watcher.chat, "Server staff have removed one of your objects"), "Single object-removal notice missing or leaked to another player");
        check(command("delobj #1", mod_id).find("no longer available") != std::string::npos, "Deleted entry was silently renumbered or snapshot discarded");
        check(command("delobj #2", mod_id).starts_with("Removed"), "Second numbered deletion lost stable snapshot IDs");
        run_for(500000);
        upload(2, layout);
        check(a.player_objects[builder_id].objects().size() == 12 && !a.player_objects[builder_id].objects().contains(1) && !a.player_objects[builder_id].objects().contains(2), "Owner re-upload resurrected singly deleted objects");
        check(command("delobj #999", mod_id).find("not in your snapshot") != std::string::npos, "Invalid snapshot index accepted");
        run_for(179000000 - (now - snapshot_started)); pose(mod);
        check(command("nearobj page 2", mod_id).find("#9 bk_rail_01") != std::string::npos, "Snapshot expired before three minutes or indices shifted");
        run_for(1000000); pose(mod);
        check(command("delobj #3", mod_id).starts_with("Preview expired"), "Snapshot survived its three-minute deadline");
        const auto builder_clears = builder.local_clear_requests, watcher_clears = watcher.local_clear_requests, mod_clears = mod.local_clear_requests;
        check(command("delobj bUiLdY", mod_id).starts_with("Cleared all 12 shared objects owned by Buildy"), "Moderator could not clear selected object's owner");
        run_for(500000);
        check(a.player_objects[builder_id].objects().empty(), "Owner clear did not reach ordinary object decoder");
        check(has(builder.chat, "Server staff have removed your placed objects") && !has(watcher.chat, "Server staff have removed your placed objects"), "Full object-removal notice missing or leaked");
        check(builder.local_clear_requests == builder_clears + 1 && watcher.local_clear_requests == watcher_clears && mod.local_clear_requests == mod_clears, "Local clear was not restricted to selected owner");
        command("announce-to Buildy Test private card"); run_for(1000000);
        check(builder.local_clear_requests == builder_clears + 1 && watcher.local_clear_requests == watcher_clears, "Private announcement changed object-clear counters");
        check(logged("[object-remove] ObjectMod (" + std::to_string(mod_id) + ")") && logged("owned by Buildy (" + std::to_string(builder_id) + ")"), "Deletion log omitted identities");
        layout.push_back({99, "own_bk_rail_01", {1, 0, 0}});
        upload(3, layout);
        check(a.player_objects[builder_id].objects().size() == 1 && a.player_objects[builder_id].objects().contains(99), "Next owner update resurrected cleared objects or blocked fresh IDs");
        pose(mod); command("nearobj rail", mod_id);
        pose(mod, 40.f);
        check(command("delobj Buildy", mod_id).starts_with("Cleared all 1 shared objects"), "Named deletion was incorrectly limited by distance");
        layout.push_back({98, "own_bk_rail_01", {1, 0, 0}}); upload(4, layout);
        pose(mod); command("nearobj rail", mod_id);
        host.drop(similar_id, "Test complete"); games.erase(similar_id); run_for(500000); pose(mod);
        config.roles.players[builder_id] = "admin";
        check(command("delobj uild", mod_id).starts_with("Cleared all 1 shared objects"), "Moderator could not clear higher-ranked staff objects");
        run_for(500000);
        check(builder.local_clear_requests == builder_clears + 3, "Third targeted clear did not reach selected owner");
        command("clear-objects"); run_for(1000000);
        check(builder.local_clear_requests == builder_clears + 4 && watcher.local_clear_requests == watcher_clears + 1 && mod.local_clear_requests == mod_clears + 1, "Global clear broken after targeted clears");
        config.roles.players.erase(builder_id);
        layout.push_back({100, "own_bk_rail_01", {1, 0, 0}}); upload(5, layout); pose(mod);
        command("nearobj rail", mod_id);
        run_for(61000000);
        const auto direct_reply_start = mod.chat.size();
        mod.say("/delobj Buildy", now); run_for(1000000);
        check(std::any_of(mod.chat.begin() + direct_reply_start, mod.chat.end(), [](const auto &line) {
                  return line.starts_with("Cleared all 1 shared objects owned by Buildy");
              }), "In-game named deletion depended on a fresh preview or character position");
        const auto clears_before_travel = builder.local_clear_requests;
        command("map San Vansterdam"); run_for(3000000);
        check(builder.local_clear_requests == clears_before_travel, "Map travel changed a targeted owner's clear counter");
        upload(6, {{101, "own_bk_rail_01", {1, 0, 0}}});
        pose(mod); command("nearobj rail", mod_id);
        command("kick Buildy", admin_id); run_for(500000);
        check(command("delobj Buildy", mod_id).starts_with("No connected player"), "Disconnected player remained targetable");
        check(command("whoplaced 2", mod_id).find("Buildy") != std::string::npos, "Disconnect removed audit history");
        // Map travel invalidates previews; individual server-park objects use the same removal path.
        command("map San Vansterdam"); run_for(3000000);
        check(command("delobj Buildy", mod_id).starts_with("No connected player"), "Named deletion targeted disconnected player after map travel");
        command("park-mod add street"); run_for(1000000);
        check(!a.park.objects().empty(), "No server park for individual object deletion test");
        if (!a.park.objects().empty()) {
            const auto first = a.park.objects().begin()->second;
            const auto original_count = a.park.objects().size();
            auto p = mod.header(PacketKind::pose, now); p.pose.root.position = first.position; p.pose.skater.push_back(p.pose.root); mod.send(p); run_for(250000);
            const auto park_preview = command("nearobj " + first.item, mod_id);
            check(park_preview.find("Server park: street") != std::string::npos, "Server park object excluded from preview");
            check(command("delobj #1", mod_id).starts_with("Removed"), "Moderator could not delete server park object");
            run_for(1000000);
            check(a.park.objects().size() + 1 == original_count && !a.park.objects().contains(first.id), "Server park deletion did not reach client");
            command("park-mod add street2"); run_for(1000000);
            check(!a.park.objects().contains(first.id), "Publishing another park resurrected deleted park object");
        }
        // Object alerts use the native staff-alert permission and existing private chat route.
        const auto alerts = [](const Game &g) {
            return std::count_if(g.chat.begin(), g.chat.end(), [](const auto &s) { return s.starts_with("Possible object spam:"); });
        };
        const auto before_admin = alerts(a), before_mod = alerts(mod), before_watcher = alerts(watcher);
        const std::uint64_t spam_id = 76561198000990005ULL;
        auto &spammer = add_game(spam_id, "SpamBuilder");
        spammer.world = mod.world; spammer.map = mod.map; spammer.secret = mod.secret;
        spammer.hello(now); run_for(1000000);
        const std::string ramp = "own_bkramps_generic_kickercurvedlarge_00001";
        const std::string rail = "own_bkrails_generic_flatrailround_00001";
        std::vector<NetworkObject> spam_layout;
        std::uint64_t revision{}, next_id = 1000;
        const auto publish = [&](Game &g, std::uint64_t rev, const std::vector<NetworkObject> &objects) {
            auto p = g.header(PacketKind::objects, now); p.objects.revision = rev; p.objects.objects = objects; g.send(p); run_for(500000);
        };
        const auto add = [&](unsigned count) {
            for (unsigned i = 0; i < count; ++i) spam_layout.push_back({++next_id, i % 2 ? rail : ramp, {1, 0, 0}});
            publish(spammer, ++revision, spam_layout);
        };
        add(12);
        check(alerts(a) == before_admin && alerts(mod) == before_mod, "Joining bulk layout triggered an alert");
        add(6);
        check(alerts(a) == before_admin + 1 && alerts(mod) == before_mod + 1, "Default admin/moderator missed spam alert");
        check(alerts(spammer) == 0 && alerts(watcher) == before_watcher, "Object-spam warning leaked to ordinary players");
        check(has(mod.chat, "Possible object spam: SpamBuilder") && has(mod.chat, "6 objects in 10s:") && has(mod.chat, "kickercurvedlarge x3") && has(mod.chat, "flatrailround x3"), "Private warning lost player/count/type breakdown");
        check(!has(mod.chat, std::to_string(spam_id)), "Spam warning exposed a SteamID in chat");
        check(logged("[object-spam] Possible object spam: SpamBuilder (" + std::to_string(spam_id) + ")") && logged(ramp + " x3") && logged(rail + " x3"), "Spam log omitted full owner/asset details");
        add(6);
        check(alerts(mod) == before_mod + 1, "Cooldown allowed repeated warning");
        run_for(61000000); add(6);
        check(alerts(mod) == before_mod + 2, "Continued spam did not warn after cooldown");
        run_for(61000000);
        spam_layout.front().position = {3, 0, 0}; spam_layout.front().scale = 2.f;
        publish(spammer, ++revision, spam_layout);
        check(alerts(mod) == before_mod + 2, "Moving/resizing an existing ID triggered a warning");
        std::vector<NetworkObject> staff_layout{{1, ramp, {1, 0, 0}}};
        publish(mod, 1, staff_layout);
        for (unsigned i = 2; i <= 7; ++i) staff_layout.push_back({i, ramp, {1, 0, 0}});
        publish(mod, 2, staff_layout);
        check(has(a.chat, "Possible object spam: ObjectMod"), "Moderator placements were exempted");
        publish(a, 1, {{1, rail, {1, 0, 0}}});
        publish(a, 2, staff_layout);
        check(has(mod.chat, "Possible object spam: Robin"), "Admin placements were exempted");
        const auto before_travel_alerts = alerts(mod);
        command("map Isle of Grom"); run_for(3000000);
        spam_layout.clear(); revision = 0;
        add(6); add(5);
        check(alerts(mod) == before_travel_alerts, "Map travel retained old window or counted initial layout");
        add(1);
        check(alerts(mod) == before_travel_alerts + 1, "Post-travel new placements did not trigger");
        config.object_spam_alerts.enabled = false;
        add(6);
        check(alerts(mod) == before_travel_alerts + 1, "Disabled feature still warned staff");
        config.object_spam_alerts.enabled = true;
        add(6);
        check(alerts(mod) == before_travel_alerts + 2, "Re-enabled feature did not count new placements");
        const auto before_rejoin_alerts = alerts(mod);
        host.drop(spam_id, "Test reconnect"); run_for(500000);
        spammer.epoch += 10000; spammer.hello(now); run_for(1000000);
        spam_layout.clear(); revision = 0;
        add(5);
        check(alerts(mod) == before_rejoin_alerts, "Reconnect retained old window or counted baseline");
        add(6);
        check(alerts(mod) == before_rejoin_alerts + 1, "Fresh session failed to detect placements");
        // Staff presence follows the native kick permission, including moderator-only sessions.
        wire().closed.erase(spam_id);
        config.roles.players[admin_id] = "player";
        run_for(61000000); add(6);
        check(!wire().closed.contains(spam_id), "Alert-only detector kicked with moderator online");
        config.roles.players[admin_id] = "admin";
        config.roles.players[mod_id] = "player";
        run_for(61000000); add(6);
        check(!wire().closed.contains(spam_id), "Alert-only detector kicked with admin online");
        const std::string automod_notice = "No Admins Online - Auto Mod Is Active - Griefing With Objects Can Auto Kick Players - Play Nice!";
        const auto notices = [&] { return std::count(watcher.chat.begin(), watcher.chat.end(), automod_notice); };
        const auto notices_before = notices();
        config.roles.players[admin_id] = "player";
        run_for(61000000);add(6);
        check(!wire().closed.contains(spam_id), "Alert-only detector kicked without staff");
        check(logged("[object-spam] Possible object spam: SpamBuilder"), "No-staff placements lost their server audit");
        run_for(46ULL * 60000000);
        check(notices()==notices_before, "Removed Auto Mod announcement was emitted");
        host.drop(mod_id, "Test staff departure");run_for(1000000);
        check(notices()==notices_before, "Staff departure emitted removed Auto Mod announcement");
        config.object_spam_alerts.enabled=false;add(6);
        config.object_spam_alerts.enabled=true;
        command("map San Vansterdam");run_for(3000000);
        spam_layout.clear();revision=0;add(12);add(6);
        check(!wire().closed.contains(spam_id) && !wire().closed.contains(watcher_id), "Bulk placement caused an MSC auto-kick");
        check(command("delobj SpamBuilder").starts_with("Cleared all"), "Manual cleanup unavailable after alert-only spam");
        config.roles.players[admin_id] = "admin";
        config.roles.players[mod_id] = "moderator";
        wire().closed.erase(mod_id);
        mod.world = watcher.world; mod.map = watcher.map; mod.epoch += 10000;
        mod.hello(now); run_for(1000000);
        command("voice on");
        const auto speak = [&] {
            auto p = watcher.header(PacketKind::voice, now);
            p.voice.distance = 0.f; p.voice.bytes = {1, 2, 3}; watcher.send(p); run_for(250000);
        };
        auto heard = a.heard_voice.size(); speak();
        check(a.heard_voice.size() == heard + 1, "Unmuted voice did not reach listener");
        check(command("vmute Robin", watcher_id).find("not an admin") != std::string::npos, "Ordinary player could mute staff");
        check(command("vmute Robin", mod_id).find("rank") != std::string::npos, "Moderator muted higher-rank admin");
        check(command("vmute ObjectMod", mod_id).find("rank") != std::string::npos, "Moderator muted same rank");
        const auto staff_reply_start = mod.chat.size();
        mod.say("/mute Watch 5", now); run_for(500000);
        check(std::any_of(mod.chat.begin() + staff_reply_start, mod.chat.end(), [](const auto &line) { return line.find("voice muted for 5 min") != std::string::npos; }), "In-game moderator timed-mute command was not routed");
        check(config.voice_mutes.size() == 1 && config.voice_mutes.front().expires > std::time(nullptr) + 290, "Timed mute did not save its expiry");
        heard = a.heard_voice.size(); speak();
        check(a.heard_voice.size() == heard && has(watcher.chat, "Your voice has been muted"), "Muted voice leaked or private notice missing");
        watcher.say("Text still works while voice muted", now); run_for(500000);
        check(has(a.chat, "Text still works while voice muted"), "Voice mute suppressed text chat");
        const auto held_expiry = config.voice_mutes.front().expires;
        check(command("vmute Watcher 0", mod_id).starts_with("Usage:") && command("vmute Watcher -1", mod_id).starts_with("Usage:") &&
              command("vmute Watcher 1.5", mod_id).starts_with("Usage:") && config.voice_mutes.front().expires == held_expiry, "Invalid duration changed a mute");
        auto restored_mutes = load_config(config.file);
        check(restored_mutes.voice_mutes.size() == 1 && restored_mutes.voice_mutes.front().expires == held_expiry, "Restart lost timed mute or reset its deadline");
        host.drop(watcher_id, "Test muted reconnect"); run_for(500000);
        watcher.epoch += 10000; watcher.name = "Watcher Renamed"; wire().closed.erase(watcher_id);
        watcher.hello(now); run_for(1000000);
        heard = a.heard_voice.size(); speak();
        check(a.heard_voice.size() == heard && has(watcher.chat, "Your voice is muted"), "Reconnect/rename bypassed saved mute");
        command("map Isle of Grom"); run_for(3000000);
        heard = a.heard_voice.size(); speak();
        check(a.heard_voice.size() == heard, "Map change bypassed voice mute");
        host.drop(watcher_id, "Test offline unmute"); run_for(500000);
        check(command("unmute Watcher", mod_id).find("now voice unmuted") != std::string::npos, "Staff could not unmute saved offline name");
        watcher.epoch += 10000; watcher.hello(now); run_for(1000000);
        heard = a.heard_voice.size(); speak();
        check(a.heard_voice.size() == heard + 1, "Unmute failed to restore voice");
        check(command("vmute \"Watcher Renamed\"", mod_id).find("until manually unmuted") != std::string::npos && config.voice_mutes.front().expires == 0, "No-duration mute was not permanent");
        restored_mutes = load_config(config.file);
        check(restored_mutes.voice_mutes.size() == 1 && restored_mutes.voice_mutes.front().expires == 0, "Restart lost permanent mute");
        check(command("mutes", mod_id).find("Watcher Renamed - permanent") != std::string::npos, "Staff mute list omitted permanent mute");
        command("vmute Watcher Renamed 2", mod_id);
        check(config.voice_mutes.size() == 1 && config.voice_mutes.front().expires != 0, "Repeated mute did not update duration");
        config.voice_mutes.front().expires = static_cast<std::int64_t>(std::time(nullptr)) - 1;
        run_for(2000000); heard = a.heard_voice.size(); speak();
        check(config.voice_mutes.empty() && a.heard_voice.size() == heard + 1 && has(watcher.chat, "Your voice mute has expired"), "Timed expiry failed to restore voice");
        const auto temp_mute_file = config.file.parent_path() / "data" / "voice-mutes.json.tmp";
        std::filesystem::create_directory(temp_mute_file);
        check(command("vmute Watcher Renamed", mod_id).starts_with("Voice mute was not changed:") && config.voice_mutes.empty(), "Failed save applied an unpersisted mute");
        std::filesystem::remove(temp_mute_file);
        const auto help_start = mod.chat.size();
        const auto watcher_help_start = watcher.chat.size();
        mod.say("/modhelp", now); run_for(500000);
        check(std::any_of(mod.chat.begin() + help_start, mod.chat.end(), [](const auto &line) { return line.starts_with("Moderator commands - /modhelp <group>"); }), "In-game moderator help command was not routed");
        check(std::none_of(watcher.chat.begin() + watcher_help_start, watcher.chat.end(), [](const auto &line) { return line.find("Moderator commands") != std::string::npos || line.find("/mute <player>") != std::string::npos; }), "Moderator help leaked to ordinary players");
        std::string guide;
        const auto read_guide = [&](std::uint64_t staff_id) {
            std::string all;
            for (const auto &[group, title] : moderator_help_groups) {
                (void)title;
                for (unsigned page = 1; page <= 2; ++page) {
                    const auto text = command("modhelp " + std::string(group) + " " + std::to_string(page), staff_id);
                    if (!text.starts_with("Usage:")) {
                        check(std::count(text.begin(), text.end(), '\n') + 1 <= 12, "Grouped help exceeded private reply line budget");
                        all += text + "\n";
                    }
                }
            }
            return all;
        };
        guide = read_guide(mod_id);
        for (const std::string cmd : {"whoplaced", "nearobj", "delobj", "mute", "unmute", "mutes"})
            check(guide.find("/" + cmd) != std::string::npos, "Moderator help omitted added command " + cmd);
        for (const auto &cmd : config.roles.find("moderator")->granted_commands)
            check(guide.find("/" + cmd) != std::string::npos, "Moderator help omitted native moderator command " + cmd);
        check(guide.find("defaults to the last 10 minutes") != std::string::npos && guide.find("ALL that player's") != std::string::npos && guide.find("omit minutes for permanent") != std::string::npos, "Moderator help omitted key command behavior");
        check(guide.find("/poll end") != std::string::npos && guide.find("/vmute") != std::string::npos && guide.find("/delobj #") != std::string::npos, "Help still omitted poll control, aliases or numbered deletion");
        check(command("modhelp objects", mod_id).find("within 5m") != std::string::npos && command("modhelp voice", mod_id).find("/mute") != std::string::npos, "Named help groups missing content");
        check(command("modhelp 2", mod_id) == command("modhelp voice", mod_id), "Numeric group selection no longer works");
        check(command("modhelp maps", mod_id).starts_with("Usage:"), "Admin-only map help exposed to moderator");
        const auto admin_guide = read_guide(admin_id);
        check(read_guide(0).find("/say ") == std::string::npos, "Console modhelp exposed a console-only command");
        check(admin_guide.find("/lock ") != std::string::npos && admin_guide.find("/unlock ") != std::string::npos && admin_guide.find("/locks ") != std::string::npos && guide.find("/lock ") == std::string::npos, "2.1.0 lock help did not reflect actual permissions");
        for (const std::string cmd : {"/map ", "/clear-objects", "/voice ", "/announcements ", "/park-mod", "/score-check", "/update", "/quit", "/reserved"})
            check(admin_guide.find(cmd) == std::string::npos, "Moderator reference exposed admin-only command " + cmd);
        check(command("modhelp maps", admin_id).starts_with("Usage:") && command("modhelp settings", admin_id).starts_with("Usage:") && command("modhelp server", admin_id).starts_with("Usage:"), "Admin-only groups remained accessible through modhelp");
        for (const auto &entry : moderator_help_entries) check(entry.text.size() <= 200, "Help entry exceeds native chat byte limit");
        check(command("modhelp voice 99", mod_id).starts_with("Usage:") && command("modhelp voice nope", mod_id).starts_with("Usage:"), "Invalid category subpage accepted");
        check(command("modhelp", admin_id).starts_with("Moderator commands") && command("modhelp").starts_with("Moderator commands"), "Admin or console denied moderator help");
        check(command("modhelp", watcher_id).find("not an admin") != std::string::npos, "Ordinary player could access moderator help");
        check(command("modhelp 0", mod_id).starts_with("Usage:") && command("modhelp 999", mod_id).starts_with("Usage:") && command("modhelp nope", mod_id).starts_with("Usage:"), "Invalid help page accepted");
        auto limited_guide_role = *config.roles.find("moderator");
        limited_guide_role.name = "limited-guide"; limited_guide_role.commands = {"kick"};
        config.roles.list.push_back(limited_guide_role); config.roles.settle();
        config.roles.players[mod_id] = "limited-guide";
        const auto limited_guide = read_guide(mod_id);
        check(limited_guide.find("/ban ") == std::string::npos && limited_guide.find("/role ") == std::string::npos && limited_guide.find("/delobj ") != std::string::npos, "Help advertised commands absent from effective role permissions");
        config.roles.players[mod_id] = "moderator";
        check(command("mute Watcher Renamed 1", watcher_id).find("not an admin") != std::string::npos, "Ordinary player could use renamed mute");
        check(command("mute Robin", mod_id).find("rank") != std::string::npos, "Renamed mute bypassed rank guard");
        check(command("mute Watcher Renamed 1", mod_id).find("muted for 1 min") != std::string::npos, "Renamed mute failed");
        check(command("vmutes", mod_id).find("Watcher Renamed") != std::string::npos, "Old mute-list alias stopped working");
        check(command("vunmute Watcher Renamed", mod_id).find("now voice unmuted") != std::string::npos, "Old unmute alias stopped working");
        check(!custom_command_name_free("mute") && !custom_command_name_free("unmute") && !custom_command_name_free("mutes"), "Renamed mute commands were not reserved");
        check(!custom_command_name_free("modhelp"), "modhelp was not reserved as a native server command");
        check(!custom_command_name_free("vmute") && !custom_command_name_free("vunmute") && !custom_command_name_free("vmutes"), "Voice commands were not registered as native names");
        // Manual text timeouts: persistent UTC deadlines, no permanent form, private receipts.
        check(command("timeout Robin 5", mod_id).find("rank") != std::string::npos && command("timeout ObjectMod 5", mod_id).find("rank") != std::string::npos, "Text timeout bypassed rank guards");
        check(command("timeout Robin 5", watcher_id).find("not an admin") != std::string::npos, "Ordinary player used timeout");
        for (const std::string args : {"Watcher Renamed", "Watcher Renamed 0", "Watcher Renamed -1", "Watcher Renamed 1.5", "Watcher Renamed 525601"})
            check(command("timeout " + args, mod_id).starts_with("Usage:"), "Text timeout accepted missing/invalid duration: " + args);
        const auto private_notice_start = a.chat.size();
        mod.say("/timeout Watcher Renamed 5", now); run_for(500000);
        check(config.text_timeouts.size() == 1 && config.text_timeouts.front().expires > std::time(nullptr) + 290 && has(watcher.chat, "You are text chat muted for 5 min."), "In-game timed text restriction or target notification missing");
        check(std::none_of(a.chat.begin() + private_notice_start, a.chat.end(), [](const auto &line) { return line.find("text chat muted") != std::string::npos; }), "Text-timeout receipt was public");
        check(command("timeouts", mod_id).find("Watcher Renamed") != std::string::npos && command("modhelp chat", mod_id).find("/timeout <player> <minutes>") != std::string::npos, "Timeout list or grouped moderator help missing");
        const auto text_deadline = config.text_timeouts.front().expires;
        auto text_restart = load_config(config.file);
        check(text_restart.text_timeouts.size() == 1 && text_restart.text_timeouts.front().expires == text_deadline, "Restart lost text timeout or extended deadline");
        const auto assert_blocked = [&](const std::string &message) {
            run_for(1600000); // stay below native flood control, so this tests the timeout gate
            const auto before = a.chat.size();
            const auto sender_before = watcher.chat.size();
            watcher.say(message, now); run_for(500000);
            check(std::none_of(a.chat.begin() + before, a.chat.end(), [](const auto &line) { return line.find("TEXT_TIMEOUT_SENTINEL") != std::string::npos; }), "Timed-out text escaped via: " + message);
            check(std::any_of(watcher.chat.begin() + sender_before, watcher.chat.end(), [](const auto &line) { return line.find("You are text chat muted for") != std::string::npos; }), "Blocked sender lost timeout feedback: " + message);
        };
        for (const std::string message : {"TEXT_TIMEOUT_SENTINEL public", "/w Robin TEXT_TIMEOUT_SENTINEL whisper", "/p TEXT_TIMEOUT_SENTINEL party", "/party say TEXT_TIMEOUT_SENTINEL alias", "/party chat TEXT_TIMEOUT_SENTINEL alias2", "/poll TEXT_TIMEOUT_SENTINEL | yes | no"}) assert_blocked(message);
        const auto previous_custom_commands = config.commands;
        config.commands.push_back({"timeout-bypass", "", {"say {arg}"}, false});
        assert_blocked("/timeout-bypass TEXT_TIMEOUT_SENTINEL custom");
        config.commands = previous_custom_commands;
        auto before_timeout_voice = a.heard_voice.size(); speak();
        check(a.heard_voice.size() == before_timeout_voice + 1, "Text timeout incorrectly blocked voice");
        const auto vote_feedback = watcher.chat.size();
        watcher.say("/yes", now); run_for(500000);
        check(std::none_of(watcher.chat.begin() + vote_feedback, watcher.chat.end(), [](const auto &line) { return line.find("text chat muted") != std::string::npos; }), "Text timeout blocked vote answers");
        command("timeout ObjectMod 1", admin_id);
        check(command("msg Robin TEXT_TIMEOUT_SENTINEL admin-route", mod_id).find("text chat muted") != std::string::npos, "Direct admin command bypassed text timeout");
        command("untimeout ObjectMod", admin_id);
        const auto dual_notice_start = watcher.chat.size();
        command("mute Watcher Renamed 2", mod_id);
        check(std::none_of(watcher.chat.begin() + dual_notice_start, watcher.chat.end(), [](const auto &line) { return line.find("Text chat is still available") != std::string::npos; }), "Voice-mute receipt contradicted active text timeout");
        command("untimeout Watcher Renamed", mod_id);
        watcher.say("Text after timeout removal", now); run_for(500000);
        check(has(a.chat, "Text after timeout removal") && has(watcher.chat, "Server staff have removed your text chat timeout"), "Manual text restoration or receipt failed");
        before_timeout_voice = a.heard_voice.size(); speak();
        check(a.heard_voice.size() == before_timeout_voice, "Removing text timeout accidentally unmuted voice");
        command("unmute Watcher Renamed", mod_id);
        command("timeout Watcher Renamed 3", mod_id);
        const auto reconnect_deadline = config.text_timeouts.front().expires;
        host.drop(watcher_id, "Test text-timeout reconnect"); run_for(500000);
        watcher.epoch += 10000; watcher.name = "Watcher Timed"; wire().closed.erase(watcher_id);
        watcher.hello(now); run_for(1000000);
        check(config.text_timeouts.front().expires == reconnect_deadline && has(watcher.chat, "You are text chat muted for 3 min."), "Reconnect reset text deadline or lost private notice");
        assert_blocked("TEXT_TIMEOUT_SENTINEL after rename");
        command("map San Vansterdam"); run_for(3000000);
        assert_blocked("TEXT_TIMEOUT_SENTINEL after map travel");
        host.drop(watcher_id, "Test offline text restore"); run_for(500000);
        check(command("untimeout Watcher Renamed", mod_id).find("has no text chat timeout") != std::string::npos && load_config(config.file).text_timeouts.empty(), "Offline removal by saved name failed to persist");
        watcher.epoch += 10000; watcher.hello(now); run_for(1000000);
        command("timeout Watcher Timed 1", mod_id);
        config.text_timeouts.front().expires = static_cast<std::int64_t>(std::time(nullptr)) - 1;
        run_for(2000000);
        watcher.say("Text after expiry", now); run_for(500000);
        check(config.text_timeouts.empty() && has(a.chat, "Text after expiry") && has(watcher.chat, "Your text chat timeout has expired"), "Timed text expiry did not restore chat or notify player");
        const auto failed_text_tmp = config.file.parent_path() / "data" / "text-timeouts.json.tmp";
        std::filesystem::create_directory(failed_text_tmp);
        const auto failed_notice_start = watcher.chat.size();
        check(command("timeout Watcher Timed 5", mod_id).starts_with("Text timeout was not changed:") && config.text_timeouts.empty() && watcher.chat.size() == failed_notice_start, "Failed timeout save applied or announced a restriction");
        std::filesystem::remove(failed_text_tmp);
        check(!custom_command_name_free("timeout") && !custom_command_name_free("untimeout") && !custom_command_name_free("timeouts"), "Timeout commands not registered as native names");
        check(!a.refused && !mod.refused && !watcher.refused, "Moderated object updates failed stock codec validation");
    }
    check(!wire().closed.contains(admin_id), "The player was disconnected: " + (wire().closed.contains(admin_id) ? wire().closed[admin_id] : std::string{}));
    return failures;
}
std::vector<Json> read_points_records(const std::filesystem::path &file) {
    std::ifstream in(file,std::ios::binary);std::string line;std::vector<Json> rows;std::set<std::string> ids;
    while(std::getline(in,line)) {
        auto row=Json::parse(line);const auto utc=row.at("utc").string();
        check(utc.size()==20 && utc[10]=='T' && utc[19]=='Z',"Points audit timestamp is not full UTC");
        check(ids.insert(row.at("event_id").string()).second,"Duplicate points audit event ID");rows.push_back(std::move(row));
    }
    return rows;
}
#include "player_stats_host_cases.inc"
#include "vip_test_host_cases.inc"
#include "shop_host_cases.inc"
#include "msc_interface_host_cases.inc"
#include "msc_features_host_cases.inc"
#include "expansion_host_cases.inc"
#include "msc_alias_host_cases.inc"
#include "points_map_host_cases.inc"
#include "chat_log_host_cases.inc"
#include "build_sets_host_cases.inc"
#include "map_persistence_host_cases.inc"
#include "upgrade_210_host_cases.inc"
} // namespace

int main(int argc, char **argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: dingosdk_server_park_tests <park mod fixtures folder>\n");
        return 2;
    }
    try {
        const auto result = run_msc_features_host() | run_msc_interface_host() | run_msc_alias_host() | run_expansion_host() | run(argv[1]) | run_stats_host() | run_vip_test_host() | run_shop_host() | run_points_map_host() | run_chat_log_host() | run_build_sets_host(argv[1]) | run_map_persistence_host() | run_upgrade_210_host();
        if (!result) std::printf("server park tests passed\n");
        return result ? 1 : 0;
    } catch (const std::exception &e) {
        std::fprintf(stderr, "FAIL: %s\n", e.what());
        return 1;
    }
}
