#ifndef slic3r_Spoolman_hpp_
#define slic3r_Spoolman_hpp_

#include <string>
#include <vector>

#include <wx/string.h>

namespace Slic3r {

class AppConfig;

// User settings of the Spoolman integration, stored in the (untracked) application config.
struct SpoolmanSettings
{
    std::string host;      // bare host name / IP, or a full URL ("https://spoolman.example.com")
    std::string port;      // optional; Spoolman listens on 7912 by default
    bool        use_https{false};
    // Spoolman has no authentication of its own, but it is often published behind a reverse proxy
    // that asks for HTTP basic auth. Both fields are optional.
    std::string username;
    std::string password;
    bool        hide_empty{true}; // filter of the spool list, remembered between runs

    static SpoolmanSettings load(const AppConfig &config);
    void                    save(AppConfig &config) const;

    // Only the host is mandatory.
    bool        valid() const;
    std::string base_url() const;
};

// One spool of the Spoolman inventory, flattened from GET /api/v1/spool (the spool, its filament and
// the filament's vendor). Only the fields the slicer needs are kept.
struct SpoolmanSpool
{
    int                      id{0};            // Spoolman spool id
    int                      filament_id{0};
    std::string              filament_name;    // "Rapid PETG Gray"
    std::string              vendor;           // "Deeplee"
    std::string              material;         // "PETG"; used as the filament type
    std::vector<std::string> colors;           // "#969DA2"; more than one for a multi-color filament
    double                   diameter{0.};     // mm, 0 when unknown
    double                   remaining_weight{-1.}; // grams; negative when Spoolman does not know it
    double                   initial_weight{-1.};
    std::string              location;
    std::string              lot_nr;
    std::string              comment;
    bool                     archived{false};

    // "Deeplee Rapid PETG Gray": the name the filament preset matching works on, so that a match
    // remembered once keeps being used for every later selection of the same spool.
    std::string display_name() const;
    // Primary color as "#RRGGBB", empty when the spool has none.
    std::string primary_color() const;
    bool        has_remaining_weight() const { return remaining_weight >= 0.; }
};

// Read-only client of a Spoolman server (https://github.com/Donkie/Spoolman).
class Spoolman
{
public:
    explicit Spoolman(SpoolmanSettings settings) : m_settings(std::move(settings)) {}

    // GET /api/v1/info: checks that the host answers like a Spoolman instance and reports its version.
    bool test(wxString &message) const;
    // GET /api/v1/spool: the whole inventory, archived spools excluded.
    bool get_spools(std::vector<SpoolmanSpool> &spools, wxString &error) const;

    // Parses the body of GET /api/v1/spool. Missing and unknown fields are ignored, so a spool without
    // a filament, a color or a weight still shows up in the list.
    // Throws std::runtime_error when the body is not a JSON array.
    static std::vector<SpoolmanSpool> parse_spools(const std::string &body);
    // "969DA2", "#969da2" and "969DA2FF" all become "#969DA2". Empty when the value is not a color.
    static std::string normalize_color(const std::string &hex);

private:
    SpoolmanSettings m_settings;
};

} // namespace Slic3r

#endif
