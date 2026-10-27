#include "MachineFilamentMatch.hpp"

#include <algorithm>
#include <map>
#include <set>

#include <boost/algorithm/string.hpp>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/menu.h>
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

// Profiles default this to "(Undefined)", which is no vendor at all.
std::string filament_vendor_of(const Preset& preset)
{
    const auto* opt = preset.config.option<ConfigOptionStrings>("filament_vendor");
    if (opt == nullptr || opt->values.empty())
        return {};
    std::string vendor = opt->values.front();
    boost::algorithm::trim(vendor);
    if (lower_trim(vendor) == "(undefined)")
        return {};
    return vendor;
}

// A filament is reported as "<vendor> <name>", so a profile vendor occurring in that string is
// very probably the one in hand.
bool vendor_occurs_in(const std::string& vendor, const std::string& machine_lower)
{
    const std::string v = lower_trim(vendor);
    return !v.empty() && !machine_lower.empty() && machine_lower.find(v) != std::string::npos;
}

bool is_generic_preset(const std::string& name) { return boost::algorithm::starts_with(lower_trim(name), "generic"); }

// How many likely profiles the shortlist shows, and when a material gets vendor submenus of its
// own instead of one long list.
constexpr size_t k_suggested_max = 8;
constexpr size_t k_group_split   = 12;

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
                                                       bool                allow_create,
                                                       const wxString&     intro_text)
    : wxDialog(parent, wxID_ANY, _L("Match filaments"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
    , m_entries(std::move(entries))
{
    auto* top = new wxBoxSizer(wxVERTICAL);

    auto* intro = new wxStaticText(this, wxID_ANY,
                                   intro_text.IsEmpty() ?
                                       _L("No filament profile matches the following filaments of the printer. "
                                          "Choose the profile to use for each one.") :
                                       intro_text);
    intro->Wrap(FromDIP(460));
    top->Add(intro, 0, wxALL, FromDIP(12));

    // Compatible presets, one per base name (nozzle variants collapse into the one compatible with the current printer).
    struct Candidate { const Preset* preset; std::string type; std::string vendor; };
    std::vector<Candidate> candidates;
    if (bundle != nullptr) {
        std::set<std::string> seen;
        for (const Preset& f : bundle->filaments.get_presets()) {
            if (!f.is_compatible || f.name.empty())
                continue;
            if (!seen.insert(lower_trim(base_name(f.name))).second)
                continue;
            candidates.push_back({&f, filament_type_of(f), filament_vendor_of(f)});
        }
    }

    auto* grid = new wxFlexGridSizer(2, wxSize(FromDIP(10), FromDIP(8)));
    grid->AddGrowableCol(1, 1);

    for (const Entry& entry : m_entries) {
        const std::string type    = lower_trim(entry.machine_type);
        const std::string machine = lower_trim(entry.machine_name);

        // Every candidate gets its index once; the menu only ever refers to these.
        std::vector<std::string> names;
        names.emplace_back(); // index 0: leave unchanged
        std::vector<int> index_of(candidates.size(), 0);
        for (size_t c = 0; c < candidates.size(); ++c) {
            names.push_back(candidates[c].preset->name);
            index_of[c] = int(names.size()) - 1;
        }

        PresetMenu menu;

        // The shortlist: same material, this filament's own vendor first, then the generic
        // profile.  A list one does not have to read is the whole point of having it.
        {
            std::vector<size_t> same_type;
            for (size_t c = 0; c < candidates.size(); ++c)
                if (!type.empty() && lower_trim(candidates[c].type) == type)
                    same_type.push_back(c);
            std::stable_sort(same_type.begin(), same_type.end(), [&](size_t a, size_t b) {
                const bool va = vendor_occurs_in(candidates[a].vendor, machine);
                const bool vb = vendor_occurs_in(candidates[b].vendor, machine);
                if (va != vb)
                    return va;
                const bool ga = is_generic_preset(base_name(candidates[a].preset->name));
                const bool gb = is_generic_preset(base_name(candidates[b].preset->name));
                if (ga != gb)
                    return ga;
                return lower_trim(candidates[a].preset->name) < lower_trim(candidates[b].preset->name);
            });
            if (same_type.size() > k_suggested_max)
                same_type.resize(k_suggested_max);
            for (const size_t c : same_type)
                menu.suggested.push_back({wxString::FromUTF8(candidates[c].preset->name), index_of[c]});
        }

        // Then everything, by material first: the material is the thing one actually knows about
        // the filament in hand, so it is the level to pick from.
        {
            std::map<std::string, std::vector<size_t>> by_type;
            std::map<std::string, std::string>         type_label;
            for (size_t c = 0; c < candidates.size(); ++c) {
                const std::string key = lower_trim(candidates[c].type);
                by_type[key].push_back(c);
                type_label.emplace(key, candidates[c].type);
            }

            std::vector<std::string> keys;
            keys.reserve(by_type.size());
            for (const auto& it : by_type)
                keys.push_back(it.first);
            std::sort(keys.begin(), keys.end(), [&type](const std::string& a, const std::string& b) {
                if ((a == type) != (b == type))
                    return a == type;             // its own material at the top
                if (a.empty() != b.empty())
                    return !a.empty();            // profiles with no material last
                return a < b;
            });

            for (const std::string& key : keys) {
                MenuGroup group;
                group.label = key.empty() ? _L("Other") : wxString::FromUTF8(type_label[key].c_str());

                std::vector<size_t>& list = by_type[key];
                std::stable_sort(list.begin(), list.end(), [&candidates](size_t a, size_t b) {
                    const std::string va = lower_trim(candidates[a].vendor);
                    const std::string vb = lower_trim(candidates[b].vendor);
                    if (va != vb)
                        return va < vb;
                    return lower_trim(candidates[a].preset->name) < lower_trim(candidates[b].preset->name);
                });

                if (list.size() > k_group_split) {
                    std::map<std::string, std::vector<size_t>> by_vendor;
                    std::map<std::string, std::string>         vendor_label;
                    for (const size_t c : list) {
                        const std::string vkey = lower_trim(candidates[c].vendor);
                        by_vendor[vkey].push_back(c);
                        vendor_label.emplace(vkey, candidates[c].vendor);
                    }
                    for (const auto& it : by_vendor) {
                        MenuGroup sub;
                        sub.label = it.first.empty() ? _L("Other") : wxString::FromUTF8(vendor_label[it.first].c_str());
                        for (const size_t c : it.second)
                            sub.leaves.push_back({wxString::FromUTF8(candidates[c].preset->name), index_of[c]});
                        group.subgroups.push_back(std::move(sub));
                    }
                } else {
                    for (const size_t c : list)
                        group.leaves.push_back({wxString::FromUTF8(candidates[c].preset->name), index_of[c]});
                }
                menu.groups.push_back(std::move(group));
            }
        }

        // Preselect "Generic <type>", which is what the sync used to pick on its own.
        const std::string generic   = lower_trim("Generic " + entry.machine_type);
        int               preselect = 0;
        for (size_t c = 0; c < candidates.size(); ++c) {
            if (!type.empty() && lower_trim(candidates[c].type) == type &&
                lower_trim(base_name(candidates[c].preset->name)) == generic) {
                preselect = index_of[c];
                break;
            }
        }

        grid->Add(new wxStaticText(this, wxID_ANY, wxString::FromUTF8(entry.machine_name)), 0, wxALIGN_CENTER_VERTICAL);

        const size_t row    = m_buttons.size();
        auto*        button = new wxButton(this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                           wxSize(FromDIP(300), -1), wxBU_LEFT);
        button->Bind(wxEVT_BUTTON, [this, row](wxCommandEvent&) { show_preset_menu(row); });
        grid->Add(button, 1, wxEXPAND);

        m_buttons.push_back(button);
        m_menus.push_back(std::move(menu));
        m_choice_names.push_back(std::move(names));
        m_selected.push_back(preselect);
        update_button_label(row);
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

void MachineFilamentMatchDialog::update_button_label(size_t row)
{
    if (row >= m_buttons.size() || row >= m_selected.size() || row >= m_choice_names.size())
        return;
    const int  index = m_selected[row];
    const bool none  = index <= 0 || size_t(index) >= m_choice_names[row].size();
    const wxString name = none ? _L("Leave unchanged") :
                                 wxString::FromUTF8(m_choice_names[row][size_t(index)].c_str());
    // The triangle says the button opens a list; the name alone would read as a label.
    m_buttons[row]->SetLabel(name + wxString::FromUTF8("   \xE2\x96\xBE"));
    m_buttons[row]->SetToolTip(none ? _L("No profile is assigned to this filament.") : name);
}

void MachineFilamentMatchDialog::show_preset_menu(size_t row)
{
    if (row >= m_menus.size() || row >= m_choice_names.size() || row >= m_buttons.size())
        return;

    // Item ids are the index into m_choice_names shifted by a base, so the menu needs no event
    // plumbing at all: GetPopupMenuSelectionFromUser hands the chosen id straight back.
    constexpr int base_id = wxID_HIGHEST + 1000;
    const int     current = m_selected[row];

    const auto item_label = [current](const MenuLeaf& leaf) {
        return leaf.name_index == current ? wxString::Format(_L("%s (selected)"), leaf.label) : leaf.label;
    };
    const auto fill = [&item_label](wxMenu* target, const std::vector<MenuLeaf>& leaves) {
        for (const MenuLeaf& leaf : leaves)
            target->Append(base_id + leaf.name_index, item_label(leaf));
    };

    wxMenu menu;
    menu.Append(base_id, current == 0 ? wxString::Format(_L("%s (selected)"), _L("Leave unchanged")) :
                                        _L("Leave unchanged"));

    const PresetMenu& entry_menu = m_menus[row];
    if (!entry_menu.suggested.empty()) {
        menu.AppendSeparator();
        fill(&menu, entry_menu.suggested);
    }
    if (!entry_menu.groups.empty()) {
        menu.AppendSeparator();
        for (const MenuGroup& group : entry_menu.groups) {
            auto* sub = new wxMenu();
            fill(sub, group.leaves);
            for (const MenuGroup& nested : group.subgroups) {
                auto* nested_menu = new wxMenu();
                fill(nested_menu, nested.leaves);
                sub->AppendSubMenu(nested_menu, nested.label);
            }
            menu.AppendSubMenu(sub, group.label);
        }
    }

    const wxPoint at = m_buttons[row]->GetPosition() + wxPoint(0, m_buttons[row]->GetSize().y);
    const int     chosen = GetPopupMenuSelectionFromUser(menu, at);
    if (chosen == wxID_NONE)
        return;
    const int index = chosen - base_id;
    if (index < 0 || size_t(index) >= m_choice_names[row].size())
        return;
    m_selected[row] = index;
    update_button_label(row);
}

std::string MachineFilamentMatchDialog::selected_preset(size_t i) const
{
    if (i >= m_selected.size() || i >= m_choice_names.size())
        return {};
    const int sel = m_selected[i];
    if (sel < 0 || size_t(sel) >= m_choice_names[i].size())
        return {};
    return m_choice_names[i][size_t(sel)];
}

bool MachineFilamentMatchDialog::remember_choices() const { return m_remember != nullptr && m_remember->GetValue(); }

} // namespace GUI
} // namespace Slic3r
