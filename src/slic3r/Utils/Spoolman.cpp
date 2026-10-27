#include "Spoolman.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>

#include <boost/algorithm/string.hpp>
#include <boost/log/trivial.hpp>

#include "nlohmann/json.hpp"

#include "libslic3r/AppConfig.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "Http.hpp"

using json = nlohmann::json;

namespace Slic3r {

// ---------------------------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------------------------

SpoolmanSettings SpoolmanSettings::load(const AppConfig &c)
{
    SpoolmanSettings s;
    s.host      = c.get("spoolman_host");
    s.port      = c.get("spoolman_port");
    s.use_https = c.get("spoolman_https") == "true";
    s.username  = c.get("spoolman_username");
    s.password  = c.get("spoolman_password");
    // Defaults to true: a fresh configuration has no key at all.
    s.hide_empty = c.get("spoolman_hide_empty") != "false";
    return s;
}

void SpoolmanSettings::save(AppConfig &c) const
{
    c.set("spoolman_host", host);
    c.set("spoolman_port", port);
    c.set("spoolman_https", use_https ? "true" : "false");
    c.set("spoolman_username", username);
    c.set("spoolman_password", password);
    c.set("spoolman_hide_empty", hide_empty ? "true" : "false");
}

bool SpoolmanSettings::valid() const { return !boost::trim_copy(host).empty(); }

std::string SpoolmanSettings::base_url() const
{
    std::string h      = boost::trim_copy(host);
    std::string scheme = use_https ? "https" : "http";
    if (boost::istarts_with(h, "https://")) {
        scheme = "https";
        h      = h.substr(8);
    } else if (boost::istarts_with(h, "http://")) {
        scheme = "http";
        h      = h.substr(7);
    }
    while (!h.empty() && h.back() == '/') h.pop_back();
    std::string       url = scheme + "://" + h;
    const std::string p   = boost::trim_copy(port);
    // Do not append the port if the user already typed one in the host field.
    if (!p.empty() && h.find(':') == std::string::npos) url += ":" + p;
    return url;
}

// ---------------------------------------------------------------------------------------------
// Spool list
// ---------------------------------------------------------------------------------------------

std::string SpoolmanSpool::display_name() const
{
    std::string name = boost::trim_copy(vendor);
    const std::string filament = boost::trim_copy(filament_name);
    if (!filament.empty())
        name = name.empty() ? filament : name + " " + filament;
    if (name.empty())
        name = "Spool #" + std::to_string(id);
    return name;
}

std::string SpoolmanSpool::primary_color() const { return colors.empty() ? std::string() : colors.front(); }

namespace {

// nlohmann's value() throws when the key is present but null, which Spoolman does use for the
// fields it does not know (a spool with no initial weight has no remaining weight either). Every
// field is read through these, so one odd record cannot cost the whole list.
std::string str_at(const json &j, const char *key)
{
    if (!j.is_object()) return {};
    const auto it = j.find(key);
    if (it == j.end() || !it->is_string()) return {};
    return it->get<std::string>();
}

double num_at(const json &j, const char *key, double fallback)
{
    if (!j.is_object()) return fallback;
    const auto it = j.find(key);
    if (it == j.end() || !it->is_number()) return fallback;
    return it->get<double>();
}

bool bool_at(const json &j, const char *key, bool fallback)
{
    if (!j.is_object()) return fallback;
    const auto it = j.find(key);
    if (it == j.end() || !it->is_boolean()) return fallback;
    return it->get<bool>();
}

const json &obj_at(const json &j, const char *key)
{
    static const json empty = json::object();
    if (!j.is_object()) return empty;
    const auto it = j.find(key);
    if (it == j.end() || !it->is_object()) return empty;
    return *it;
}

wxString format_http_error(const std::string &body, const std::string &error, unsigned status)
{
    if (status != 0) {
        // Spoolman answers errors as {"message": "..."}.
        std::string message;
        try {
            message = str_at(json::parse(body), "message");
        } catch (const std::exception &) {}
        if (!message.empty())
            return wxString::Format("HTTP %u: %s", status, wxString::FromUTF8(message.c_str()));
        return wxString::Format("HTTP %u: %s", status, wxString::FromUTF8(body.c_str()));
    }
    if (error.find("Couldn't resolve host name") != std::string::npos)
        return _L("The host name could not be resolved. Please check the Spoolman host and try again.");
    if (error.find("Timeout was reached") != std::string::npos)
        return _L("Connection timed out. Please check that the Spoolman server is running and reachable.");
    return wxString::FromUTF8(error.c_str());
}

} // namespace

std::string Spoolman::normalize_color(const std::string &hex)
{
    std::string digits;
    for (const char c : hex) {
        if (std::isxdigit(static_cast<unsigned char>(c)))
            digits += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        else if (c != '#' && !std::isspace(static_cast<unsigned char>(c)))
            return {}; // not a color
    }
    if (digits.size() == 3) // "F00" -> "FF0000"
        digits = {digits[0], digits[0], digits[1], digits[1], digits[2], digits[2]};
    if (digits.size() < 6)
        return {};
    return "#" + digits.substr(0, 6); // an alpha channel, if any, is dropped
}

std::vector<SpoolmanSpool> Spoolman::parse_spools(const std::string &body)
{
    const json doc = json::parse(body);
    if (!doc.is_array())
        throw std::runtime_error("the Spoolman server did not answer with a list of spools");

    std::vector<SpoolmanSpool> spools;
    spools.reserve(doc.size());
    for (const json &entry : doc) {
        if (!entry.is_object())
            continue;

        SpoolmanSpool s;
        s.id               = static_cast<int>(num_at(entry, "id", 0.));
        s.remaining_weight = num_at(entry, "remaining_weight", -1.);
        s.initial_weight   = num_at(entry, "initial_weight", -1.);
        s.location         = str_at(entry, "location");
        s.lot_nr           = str_at(entry, "lot_nr");
        s.comment          = str_at(entry, "comment");
        s.archived         = bool_at(entry, "archived", false);

        const json &filament = obj_at(entry, "filament");
        s.filament_id        = static_cast<int>(num_at(filament, "id", 0.));
        s.filament_name      = str_at(filament, "name");
        s.material           = str_at(filament, "material");
        s.diameter           = num_at(filament, "diameter", 0.);
        s.vendor             = str_at(obj_at(filament, "vendor"), "name");

        // A multi-color filament lists its colors in multi_color_hexes; a plain one has color_hex.
        const std::string multi = str_at(filament, "multi_color_hexes");
        if (!multi.empty()) {
            std::vector<std::string> parts;
            boost::split(parts, multi, boost::is_any_of(",;"));
            for (const std::string &part : parts) {
                const std::string color = normalize_color(part);
                if (!color.empty())
                    s.colors.push_back(color);
            }
        }
        if (s.colors.empty()) {
            const std::string color = normalize_color(str_at(filament, "color_hex"));
            if (!color.empty())
                s.colors.push_back(color);
        }

        spools.push_back(std::move(s));
    }
    return spools;
}

// ---------------------------------------------------------------------------------------------
// Client
// ---------------------------------------------------------------------------------------------

bool Spoolman::test(wxString &message) const
{
    const std::string url = m_settings.base_url() + "/api/v1/info";
    bool              ok  = false;

    BOOST_LOG_TRIVIAL(info) << "Spoolman: querying " << url;
    auto http = Http::get(url);
    if (!m_settings.username.empty())
        http.auth_basic(m_settings.username, m_settings.password);
    http.header("Accept", "application/json")
        .timeout_connect(10)
        .timeout_max(30)
        .on_complete([&](std::string body, unsigned) {
            std::string version;
            bool        parsed = false;
            try {
                const json doc = json::parse(body);
                version        = str_at(doc, "version");
                parsed         = doc.is_object();
            } catch (const std::exception &) {}
            if (!parsed) {
                message = _L("The host answered, but not like a Spoolman server. Please check the host and the port.");
                return;
            }
            ok = true;
            message = version.empty() ? _L("Connected to the Spoolman server.")
                                      : wxString::Format(_L("Connected to Spoolman %s."), wxString::FromUTF8(version.c_str()));
        })
        .on_error([&](std::string body, std::string error, unsigned status) {
            BOOST_LOG_TRIVIAL(error) << "Spoolman: " << url << " failed: " << error << ", HTTP " << status << ", body: " << body;
            message = format_http_error(body, error, status);
        })
        .perform_sync();
    return ok;
}

bool Spoolman::get_spools(std::vector<SpoolmanSpool> &spools, wxString &error) const
{
    const std::string url = m_settings.base_url() + "/api/v1/spool?allow_archived=false";
    bool              ok  = false;

    BOOST_LOG_TRIVIAL(info) << "Spoolman: fetching spools from " << url;
    auto http = Http::get(url);
    if (!m_settings.username.empty())
        http.auth_basic(m_settings.username, m_settings.password);
    http.header("Accept", "application/json")
        .timeout_connect(10)
        .timeout_max(60)
        .size_limit(16 * 1024 * 1024)
        .on_complete([&](std::string body, unsigned) {
            try {
                spools = parse_spools(body);
                ok     = true;
            } catch (const std::exception &e) {
                BOOST_LOG_TRIVIAL(error) << "Spoolman: could not parse the spool list: " << e.what();
                error = _L("The answer of the Spoolman server could not be read. Please check the host and the port.");
            }
        })
        .on_error([&](std::string body, std::string err, unsigned status) {
            BOOST_LOG_TRIVIAL(error) << "Spoolman: " << url << " failed: " << err << ", HTTP " << status << ", body: " << body;
            error = format_http_error(body, err, status);
        })
        .perform_sync();
    return ok;
}

} // namespace Slic3r
