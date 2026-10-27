#pragma once

#include <deque>
#include <string>
#include <vector>

#include <wx/dialog.h>

class wxCheckBox;
class wxChoice;

namespace Slic3r {

class Preset;
class PresetBundle;

namespace GUI {

// Matching of the filaments reported by the printer ("<vendor> <type>[ <sub_type>]", e.g.
// "FLASHFORGE PLA basic PLA") to the local filament presets.
//
// Resolution order:
//   1. a match chosen manually by the user and remembered in AppConfig;
//   2. a compatible preset (system OR user) whose name, ignoring the "@printer nozzle" suffix and
//      the case, is the machine filament name.
// Anything else is "not matched": callers are expected to let the user choose with
// MachineFilamentMatchDialog instead of silently falling back to a generic preset.

enum class MachineFilamentMatchKind
{
    None,   // no preset found
    Manual, // remembered manual match
    Exact   // same name
};

// Key of a remembered manual match: exact machine string + type.
std::string machine_filament_manual_key(const std::string& machine_name, const std::string& machine_type);

// Preset remembered for this machine filament, if it still exists and is compatible with the current printer.
const Preset* find_manual_machine_filament_preset(const std::deque<Preset>& filaments,
                                                   const std::string&        machine_name,
                                                   const std::string&        machine_type);

// Remembers that this machine filament uses the given preset ("preset_name" may carry the "@printer" suffix).
void remember_manual_machine_filament_preset(const std::string& machine_name,
                                             const std::string& machine_type,
                                             const std::string& preset_name);

// Full resolution (manual, then exact name). Returns nullptr if the filament is not matched.
const Preset* match_machine_filament_preset(const PresetBundle*       bundle,
                                            const std::string&         machine_name,
                                            const std::string&         machine_type,
                                            MachineFilamentMatchKind*  kind = nullptr);

// Lets the user pick, for each machine filament that could not be matched, the local preset to use.
class MachineFilamentMatchDialog : public wxDialog
{
public:
    struct Entry
    {
        std::string machine_name;
        std::string machine_type;
    };

    enum : int { ID_CREATE_PRESET = wxID_HIGHEST + 100 };

    // allow_create adds a "Create new profile..." button that ends the dialog with ID_CREATE_PRESET.
    // intro replaces the introduction text, for callers whose filaments do not come from the printer
    // (the Spoolman inventory, for one); empty keeps the default one.
    MachineFilamentMatchDialog(wxWindow*           parent,
                               const PresetBundle* bundle,
                               std::vector<Entry>  entries,
                               bool                allow_create,
                               const wxString&     intro = wxString());

    // Full name of the preset chosen for entry i; empty when the user chose to leave it unmatched.
    std::string selected_preset(size_t i) const;
    // "Remember my choices" checkbox.
    bool remember_choices() const;

private:
    std::vector<Entry>                 m_entries;
    std::vector<std::vector<std::string>> m_choice_names; // per entry: preset full names, index 0 = none
    std::vector<wxChoice*>             m_choices;
    wxCheckBox*                        m_remember{nullptr};
};

} // namespace GUI
} // namespace Slic3r
