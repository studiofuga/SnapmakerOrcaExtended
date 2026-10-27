#include "SpoolmanDialog.hpp"

#include <algorithm>
#include <tuple>

#include <boost/algorithm/string.hpp>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/panel.h>
#include <wx/scrolwin.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/utils.h>

#include "GUI.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "MsgDialog.hpp"

namespace Slic3r { namespace GUI {

// ---------------------------------------------------------------------------------------------
// Connection settings
// ---------------------------------------------------------------------------------------------

SpoolmanDialog::SpoolmanDialog(wxWindow *parent, const SpoolmanSettings &s)
    : wxDialog(parent, wxID_ANY, _L("Spoolman settings"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
    , m_settings(s)
{
    auto *grid = new wxFlexGridSizer(2, wxSize(8, 8));
    grid->AddGrowableCol(1, 1);

    auto add_row = [&](const wxString &label, wxWindow *ctrl) {
        grid->Add(new wxStaticText(this, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
        grid->Add(ctrl, 1, wxEXPAND);
    };

    m_host = new wxTextCtrl(this, wxID_ANY, wxString::FromUTF8(s.host.c_str()), wxDefaultPosition, wxSize(FromDIP(280), -1));
    m_host->SetToolTip(_L("Host name or IP address of the Spoolman server, e.g. spoolman.example.com"));
    // A fresh configuration starts from the port Spoolman listens on by default.
    const std::string port = (s.host.empty() && s.port.empty()) ? std::string("7912") : s.port;
    m_port                 = new wxTextCtrl(this, wxID_ANY, wxString::FromUTF8(port.c_str()));
    m_port->SetToolTip(_L("Spoolman listens on port 7912 by default. Leave empty to use the default port of the protocol."));
    m_https = new wxCheckBox(this, wxID_ANY, _L("Use HTTPS"));
    m_https->SetValue(s.use_https);
    m_username = new wxTextCtrl(this, wxID_ANY, wxString::FromUTF8(s.username.c_str()));
    m_username->SetToolTip(_L("Optional. Only needed when the Spoolman server sits behind a proxy asking for HTTP basic authentication."));
    m_password = new wxTextCtrl(this, wxID_ANY, wxString::FromUTF8(s.password.c_str()), wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD);
    m_password->SetToolTip(_L("Optional. Only needed when the Spoolman server sits behind a proxy asking for HTTP basic authentication."));

    add_row(_L("Host"), m_host);
    add_row(_L("Port"), m_port);
    add_row(wxEmptyString, m_https);
    add_row(_L("User name"), m_username);
    add_row(_L("Password"), m_password);

    auto *test_btn = new wxButton(this, wxID_ANY, _L("Test connection"));
    test_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { on_test(); });

    auto *buttons = new wxBoxSizer(wxHORIZONTAL);
    buttons->Add(test_btn, 0, wxRIGHT, FromDIP(8));
    buttons->AddStretchSpacer(1);
    buttons->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0);

    auto *top = new wxBoxSizer(wxVERTICAL);
    top->Add(grid, 1, wxEXPAND | wxALL, FromDIP(12));
    top->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));
    SetSizerAndFit(top);
    wxGetApp().UpdateDlgDarkUI(this);
    CentreOnParent();
}

SpoolmanSettings SpoolmanDialog::get_settings() const
{
    // Trim: a stray space or newline pasted with the host name turns into an unresolvable host.
    auto val = [](const wxTextCtrl *c) { return std::string(c->GetValue().Trim(true).Trim(false).ToUTF8().data()); };
    SpoolmanSettings s = m_settings; // keeps hide_empty, which this dialog does not show
    s.host      = val(m_host);
    s.port      = val(m_port);
    s.use_https = m_https->GetValue();
    s.username  = val(m_username);
    s.password  = val(m_password);
    return s;
}

void SpoolmanDialog::on_test()
{
    const SpoolmanSettings s = get_settings();
    if (!s.valid()) {
        show_error(this, _L("Please fill in the host of the Spoolman server."));
        return;
    }
    Spoolman client(s);
    wxString msg;
    bool     ok;
    {
        wxBusyCursor wait;
        ok = client.test(msg);
    }
    if (ok)
        show_info(this, msg, _L("Spoolman"));
    else
        show_error(this, msg);
}

// ---------------------------------------------------------------------------------------------
// Spool selection
// ---------------------------------------------------------------------------------------------

namespace {

std::string lower(const std::string &s) { return boost::algorithm::to_lower_copy(s); }

// Grouped by material, then vendor, then name: the spools of one material sit together, which is
// how they get picked for a project.
bool spool_order(const SpoolmanSpool &a, const SpoolmanSpool &b)
{
    return std::make_tuple(lower(a.material), lower(a.vendor), lower(a.filament_name), a.id) <
           std::make_tuple(lower(b.material), lower(b.vendor), lower(b.filament_name), b.id);
}

wxString remaining_label(const SpoolmanSpool &s)
{
    if (!s.has_remaining_weight())
        return wxString::FromUTF8("\xE2\x80\x94"); // em dash
    return wxString::Format("%.0f g", s.remaining_weight);
}

} // namespace

SpoolmanSpoolDialog::SpoolmanSpoolDialog(wxWindow *parent, std::vector<SpoolmanSpool> spools, size_t max_slots, bool hide_empty)
    : wxDialog(parent, wxID_ANY, _L("Spoolman spools"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
    , m_spools(std::move(spools))
    , m_max_slots(std::max<size_t>(max_slots, 1))
{
    std::sort(m_spools.begin(), m_spools.end(), spool_order);

    auto *top = new wxBoxSizer(wxVERTICAL);

    auto *intro = new wxStaticText(this, wxID_ANY,
                                   _L("Tick the spools to use in this project. They replace the filament list of the "
                                      "project: one filament slot per spool, in the order they are ticked."));
    intro->Wrap(FromDIP(520));
    top->Add(intro, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(12));

    // Filter row
    auto *filter_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_filter           = new wxTextCtrl(this, wxID_ANY, wxEmptyString);
    m_filter->SetHint(_L("Filter by name, material or location"));
    m_filter->Bind(wxEVT_TEXT, [this](wxCommandEvent &) { apply_filter(); });
    m_hide_empty = new wxCheckBox(this, wxID_ANY, _L("Hide empty spools"));
    m_hide_empty->SetValue(hide_empty);
    m_hide_empty->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent &) { apply_filter(); });
    filter_sizer->Add(m_filter, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
    filter_sizer->Add(m_hide_empty, 0, wxALIGN_CENTER_VERTICAL);
    top->Add(filter_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(12));

    // Spool list
    m_list = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(520), FromDIP(360)), wxVSCROLL | wxBORDER_SIMPLE);
    m_list->SetScrollRate(0, FromDIP(10));
    auto *list_sizer = new wxBoxSizer(wxVERTICAL);

    m_rows.reserve(m_spools.size());
    for (size_t i = 0; i < m_spools.size(); ++i) {
        const SpoolmanSpool &spool = m_spools[i];

        Row row;
        row.spool_index = i;
        row.panel       = new wxPanel(m_list, wxID_ANY);
        auto *row_sizer = new wxBoxSizer(wxHORIZONTAL);

        row.slot = new wxStaticText(row.panel, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(26), -1), wxALIGN_RIGHT);
        row_sizer->Add(row.slot, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));

        auto *swatch = new wxPanel(row.panel, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(14), FromDIP(14)), wxBORDER_SIMPLE);
        const std::string color = spool.primary_color();
        if (!color.empty()) {
            const wxColour c(wxString::FromUTF8(color.c_str()));
            if (c.IsOk())
                swatch->SetBackgroundColour(c);
        }
        swatch->SetToolTip(color.empty() ? _L("This spool has no color in Spoolman.") : wxString::FromUTF8(color.c_str()));
        row_sizer->Add(swatch, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));

        row.check = new wxCheckBox(row.panel, wxID_ANY, wxString::FromUTF8(spool.display_name().c_str()));
        wxString tip = wxString::Format(_L("Spool #%d in Spoolman"), spool.id);
        if (!spool.location.empty())
            tip += "\n" + wxString::Format(_L("Location: %s"), wxString::FromUTF8(spool.location.c_str()));
        if (!spool.lot_nr.empty())
            tip += "\n" + wxString::Format(_L("Lot: %s"), wxString::FromUTF8(spool.lot_nr.c_str()));
        row.check->SetToolTip(tip);
        const size_t row_index = m_rows.size();
        row.check->Bind(wxEVT_CHECKBOX, [this, row_index](wxCommandEvent &) { on_toggle(row_index); });
        row_sizer->Add(row.check, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));

        auto *material = new wxStaticText(row.panel, wxID_ANY, wxString::FromUTF8(spool.material.c_str()), wxDefaultPosition,
                                          wxSize(FromDIP(70), -1));
        row_sizer->Add(material, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));

        auto *remaining = new wxStaticText(row.panel, wxID_ANY, remaining_label(spool), wxDefaultPosition, wxSize(FromDIP(64), -1),
                                           wxALIGN_RIGHT);
        remaining->SetToolTip(_L("Filament left on the spool, as Spoolman knows it."));
        row_sizer->Add(remaining, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));

        row.panel->SetSizer(row_sizer);
        list_sizer->Add(row.panel, 0, wxEXPAND | wxALL, FromDIP(3));
        m_rows.push_back(row);
    }

    m_list->SetSizer(list_sizer);
    top->Add(m_list, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(12));

    m_summary = new wxStaticText(this, wxID_ANY, wxEmptyString);
    top->Add(m_summary, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(12));

    top->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, FromDIP(12));
    if (auto *ok_btn = FindWindow(wxID_OK))
        ok_btn->SetLabel(_L("Assign to project"));

    SetSizerAndFit(top);
    SetMinSize(GetSize());
    wxGetApp().UpdateDlgDarkUI(this);
    apply_filter();
    update_slots();
    CentreOnParent();
}

bool SpoolmanSpoolDialog::matches_filter(const SpoolmanSpool &spool) const
{
    if (m_hide_empty != nullptr && m_hide_empty->GetValue() && spool.has_remaining_weight() && spool.remaining_weight <= 0.)
        return false;

    const std::string needle = lower(std::string(m_filter->GetValue().ToUTF8().data()));
    if (needle.empty())
        return true;
    const std::string haystack = lower(spool.display_name() + " " + spool.material + " " + spool.location + " " + spool.lot_nr);
    return haystack.find(needle) != std::string::npos;
}

void SpoolmanSpoolDialog::apply_filter()
{
    for (const Row &row : m_rows)
        row.panel->Show(matches_filter(m_spools[row.spool_index]));
    m_list->Layout();
    m_list->FitInside();
}

void SpoolmanSpoolDialog::on_toggle(size_t row_index)
{
    if (row_index >= m_rows.size())
        return;
    const Row   &row         = m_rows[row_index];
    const size_t spool_index = row.spool_index;

    if (row.check->GetValue()) {
        if (m_selection.size() >= m_max_slots) {
            // The project cannot hold more filaments than the printer has slots.
            row.check->SetValue(false);
            m_summary->SetLabel(wxString::Format(_L("At most %d spools can be assigned to the project."), int(m_max_slots)));
            m_summary->SetForegroundColour(wxColour(0xC0, 0x39, 0x2B));
            Layout();
            return;
        }
        if (std::find(m_selection.begin(), m_selection.end(), spool_index) == m_selection.end())
            m_selection.push_back(spool_index);
    } else {
        m_selection.erase(std::remove(m_selection.begin(), m_selection.end(), spool_index), m_selection.end());
    }
    update_slots();
}

void SpoolmanSpoolDialog::update_slots()
{
    for (const Row &row : m_rows) {
        const auto it = std::find(m_selection.begin(), m_selection.end(), row.spool_index);
        row.slot->SetLabel(it == m_selection.end() ?
                               wxString() :
                               wxString::Format("%d.", int(std::distance(m_selection.begin(), it)) + 1));
    }
    m_summary->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
    m_summary->SetLabel(m_selection.empty()
                            ? _L("No spool selected.")
                            : wxString::Format(_L("%d of at most %d spools selected."), int(m_selection.size()), int(m_max_slots)));
    if (auto *ok_btn = FindWindow(wxID_OK))
        ok_btn->Enable(!m_selection.empty());
    Layout();
}

std::vector<SpoolmanSpool> SpoolmanSpoolDialog::selected_spools() const
{
    std::vector<SpoolmanSpool> out;
    out.reserve(m_selection.size());
    for (const size_t index : m_selection)
        if (index < m_spools.size())
            out.push_back(m_spools[index]);
    return out;
}

bool SpoolmanSpoolDialog::hide_empty() const { return m_hide_empty != nullptr && m_hide_empty->GetValue(); }

}} // namespace Slic3r::GUI
