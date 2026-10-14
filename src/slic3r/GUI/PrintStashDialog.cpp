#include "PrintStashDialog.hpp"

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/gbsizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/utils.h>

#include "GUI.hpp"
#include "I18N.hpp"
#include "MsgDialog.hpp"

namespace Slic3r { namespace GUI {

PrintStashDialog::PrintStashDialog(wxWindow *parent, const PrintStashSettings &s)
    : wxDialog(parent, wxID_ANY, _L("PrintStash settings"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
{
    auto *grid = new wxFlexGridSizer(2, wxSize(8, 8));
    grid->AddGrowableCol(1, 1);

    auto add_row = [&](const wxString &label, wxWindow *ctrl) {
        grid->Add(new wxStaticText(this, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
        grid->Add(ctrl, 1, wxEXPAND);
    };

    m_host = new wxTextCtrl(this, wxID_ANY, wxString::FromUTF8(s.host), wxDefaultPosition, wxSize(FromDIP(280), -1));
    m_host->SetToolTip(_L("Host name or IP address of the PrintStash server, e.g. printstash.example.com"));
    m_port = new wxTextCtrl(this, wxID_ANY, wxString::FromUTF8(s.port));
    m_port->SetToolTip(_L("Optional. Leave empty to use the default port of the protocol."));
    m_https = new wxCheckBox(this, wxID_ANY, _L("Use HTTPS"));
    m_https->SetValue(s.use_https);
    m_username   = new wxTextCtrl(this, wxID_ANY, wxString::FromUTF8(s.username));
    m_api_key    = new wxTextCtrl(this, wxID_ANY, wxString::FromUTF8(s.api_key), wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD);
    m_collection = new wxTextCtrl(this, wxID_ANY, wxString::FromUTF8(s.collection));
    m_collection->SetToolTip(_L("Optional. Target collection, e.g. Functional/Brackets"));
    m_tags = new wxTextCtrl(this, wxID_ANY, wxString::FromUTF8(s.tags));
    m_tags->SetToolTip(_L("Optional. Comma separated tags."));

    add_row(_L("Host"), m_host);
    add_row(_L("Port"), m_port);
    add_row(wxEmptyString, m_https);
    add_row(_L("Username"), m_username);
    add_row(_L("API key"), m_api_key);
    add_row(_L("Collection"), m_collection);
    add_row(_L("Tags"), m_tags);

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
    CentreOnParent();
}

PrintStashSettings PrintStashDialog::get_settings() const
{
    // Trim: a stray space/newline pasted with the API key or username causes "invalid_credentials".
    auto val = [](const wxTextCtrl *c) { return std::string(c->GetValue().Trim(true).Trim(false).ToUTF8().data()); };
    PrintStashSettings s;
    s.host       = val(m_host);
    s.port       = val(m_port);
    s.use_https  = m_https->GetValue();
    s.username   = val(m_username);
    s.api_key    = val(m_api_key);
    s.collection = val(m_collection);
    s.tags       = val(m_tags);
    return s;
}

void PrintStashDialog::on_test()
{
    const PrintStashSettings s = get_settings();
    if (!s.valid()) {
        show_error(this, _L("Please fill in host, username and API key."));
        return;
    }
    PrintStash host(s);
    wxString   msg;
    bool       ok;
    {
        wxBusyCursor wait;
        ok = host.test(msg);
    }
    if (ok)
        show_info(this, host.get_test_ok_msg(), _L("PrintStash"));
    else
        show_error(this, host.get_test_failed_msg(msg));
}

}} // namespace Slic3r::GUI
