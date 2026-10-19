#include "MachineFilamentMatch.hpp"

#include <algorithm>
#include <map>
#include <set>

#include <boost/algorithm/string.hpp>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include "libslic3r/AppConfig.hpp"
#include "libslic3r/FilamentColorLibrary.hpp"
#include "libslic3r/Preset.hpp"
#include "libslic3r/PresetBundle.hpp"

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"

namespace Slic3r {
namespace GUI {

namespace {

constexpr const char* k_mapping_section = "machine_filament_presets";

std::string lower_trim(std::string s)
{
    boost::algorithm::trim(s);
    boost::algorithm::to_lower(s);
    return s;
}

// "FLASHFORGE PLA basic PLA @U1 0.4 nozzle" -> "FLASHFORGE PLA basic PLA"
std::string base_name(const std::string& name)
{
    return GetFilamentMatchName(Preset::remove_suffix_modified(name));
}

std::string filament_type_of(const Preset& preset)
{
    const auto* opt = preset.config.option<ConfigOptionStrings>("filament_type");
    return (opt && !opt->values.empty()) ? opt->values.front() : std::string();
}

} // namespace

std::string machine_filament_manual_key(const std::string& machine_name, const std::string& machine_type)
{
    // Different prefix from the vendor|type signature used by the "Create Filament" wizard link, so the two never collide.
    return "name:" + lower_trim(machine_name) + "|" + lower_trim(machine_type);
}

const Preset* find_manual_machine_filament_preset(const std::deque<Preset>& filaments,
                                                   const std::string&        machine_name,
                                                   const std::string&        machine_type)
{
    AppConfig* app_config = wxGetApp().app_config;
    if (app_config == nullptr)
        return nullptr;
    const std::string key = machine_filament_manual_key(machine_name, machine_type);
    if (!app_config->has(k_mapping_section, key))
        return nullptr;
    const std::string mapped = lower_trim(app_config->get(k_mapping_section, key));
    if (mapped.empty())
        return nullptr;
    for (const Preset& f : filaments)
        if (f.is_compatible && lower_trim(base_name(f.name)) == mapped)
            return &f;
    return nullptr;
}

void remember_manual_machine_filament_preset(const std::string& machine_name,
                                             const std::string& machine_type,
                                             const std::string& preset_name)
{
    AppConfig* app_config = wxGetApp().app_config;
    if (app_config == nullptr || machine_name.empty() || preset_name.empty())
        return;
    app_config->set(k_mapping_section, machine_filament_manual_key(machine_name, machine_type), base_name(preset_name));
    app_config->save();
}

const Preset* match_machine_filament_preset(const PresetBundle*      bundle,
                                            const std::string&        machine_name,
                                            const std::string&        machine_type,
                                            MachineFilamentMatchKind* kind)
{
    if (kind)
        *kind = MachineFilamentMatchKind::None;
    if (bundle == nullptr || machine_name.empty())
        return nullptr;

    const std::deque<Preset>& filaments = bundle->filaments.get_presets();

    if (const Preset* manual = find_manual_machine_filament_preset(filaments, machine_name, machine_type)) {
        if (kind)
            *kind = MachineFilamentMatchKind::Manual;
        return manual;
    }

    const std::string wanted = lower_trim(base_name(machine_name));
    if (wanted.empty())
        return nullptr;
    for (const Preset& f : filaments) {
        // User presets count as well as system ones: a profile the user created for this very spool is the best match.
        if (f.is_compatible && lower_trim(base_name(f.name)) == wanted) {
            if (kind)
                *kind = MachineFilamentMatchKind::Exact;
            return &f;
        }
    }
    return nullptr;
}

MachineFilamentMatchDialog::MachineFilamentMatchDialog(wxWindow*          parent,
                                                       const PresetBundle* bundle,
                                                       std::vector<Entry>  entries,
                                                       bool                allow_create)
    : wxDialog(parent, wxID_ANY, _L("Match filaments"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
    , m_entries(std::move(entries))
{
    auto* top = new wxBoxSizer(wxVERTICAL);

    auto* intro = new wxStaticText(this, wxID_ANY,
                                   _L("No filament profile matches the following filaments of the printer. "
                                      "Choose the profile to use for each one."));
    intro->Wrap(FromDIP(460));
    top->Add(intro, 0, wxALL, FromDIP(12));

    // Compatible presets, one per base name (nozzle variants collapse into the one compatible with the current printer).
    struct Candidate { const Preset* preset; std::string type; };
    std::vector<Candidate> candidates;
    if (bundle != nullptr) {
        std::set<std::string> seen;
        for (const Preset& f : bundle->filaments.get_presets()) {
            if (!f.is_compatible || f.name.empty())
                continue;
            if (!seen.insert(lower_trim(base_name(f.name))).second)
                continue;
            candidates.push_back({&f, filament_type_of(f)});
        }
    }

    auto* grid = new wxFlexGridSizer(2, wxSize(FromDIP(10), FromDIP(8)));
    grid->AddGrowableCol(1, 1);

    for (const Entry& entry : m_entries) {
        std::vector<const Candidate*> ordered;
        for (const Candidate& c : candidates)
            ordered.push_back(&c);
        const std::string type = lower_trim(entry.machine_type);
        // Same material type first, then alphabetical.
        std::stable_sort(ordered.begin(), ordered.end(), [&](const Candidate* a, const Candidate* b) {
            const bool sa = lower_trim(a->type) == type, sb = lower_trim(b->type) == type;
            if (sa != sb)
                return sa;
            return lower_trim(a->preset->name) < lower_trim(b->preset->name);
        });

        wxString label = wxString::FromUTF8(entry.machine_name);
        grid->Add(new wxStaticText(this, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);

        auto* choice = new wxChoice(this, wxID_ANY);
        std::vector<std::string> names;
        choice->Append(_L("Leave unchanged"));
        names.emplace_back();

        // Preselect "Generic <type>", which is what the sync used to pick on its own.
        const std::string generic = lower_trim("Generic " + entry.machine_type);
        int preselect = 0;
        for (const Candidate* c : ordered) {
            choice->Append(wxString::FromUTF8(c->preset->name));
            names.push_back(c->preset->name);
            if (preselect == 0 && !type.empty() && lower_trim(c->type) == type && lower_trim(base_name(c->preset->name)) == generic)
                preselect = int(names.size()) - 1;
        }
        choice->SetSelection(preselect);
        choice->SetMinSize(wxSize(FromDIP(280), -1));
        grid->Add(choice, 1, wxEXPAND);

        m_choices.push_back(choice);
        m_choice_names.push_back(std::move(names));
    }
    top->Add(grid, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(12));

    m_remember = new wxCheckBox(this, wxID_ANY, _L("Remember my choices"));
    m_remember->SetValue(true);
    top->Add(m_remember, 0, wxALL, FromDIP(12));

    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    if (allow_create) {
        auto* create = new wxButton(this, ID_CREATE_PRESET, _L("Create new profile..."));
        create->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(ID_CREATE_PRESET); });
        buttons->Add(create, 0, wxRIGHT, FromDIP(8));
    }
    buttons->AddStretchSpacer(1);
    buttons->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0);
    top->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));

    SetSizerAndFit(top);
    CentreOnParent();
    wxGetApp().UpdateDlgDarkUI(this);
}

std::string MachineFilamentMatchDialog::selected_preset(size_t i) const
{
    if (i >= m_choices.size())
        return {};
    const int sel = m_choices[i]->GetSelection();
    if (sel < 0 || size_t(sel) >= m_choice_names[i].size())
        return {};
    return m_choice_names[i][size_t(sel)];
}

bool MachineFilamentMatchDialog::remember_choices() const { return m_remember != nullptr && m_remember->GetValue(); }

} // namespace GUI
} // namespace Slic3r
