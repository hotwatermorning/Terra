#include "./GUI.hpp"
#include "../App.hpp"

#include <set>
#include <fmt/ostream.h>

#include <pluginterfaces/vst/ivstaudioprocessor.h>

#include "../misc/StrCnv.hpp"

namespace fmt {
    template <>
    struct formatter<Steinberg::ViewRect> {
        template <typename ParseContext>
        constexpr auto parse(ParseContext &ctx) { return ctx.begin(); }
        
        template <typename FormatContext>
        auto format(Steinberg::ViewRect const &rc, FormatContext &ctx) {
            return format_to(ctx.begin(), "({}, {}, {}, {})", rc.left, rc.top, rc.right, rc.bottom);
        }
    };
}

NS_HWM_BEGIN

class PluginEditorControl
:   public wxPanel
{
public:
    PluginEditorControl() {}
    ~PluginEditorControl() {}
    
private:
    wxButton *btn_next_program_;
    wxButton *btn_prev_program_;
};

class PluginEditorContents
:   public wxWindow
,   public Vst3Plugin::EditorCloseListener
,   public Vst3Plugin::PlugFrameListener
{
public:
    PluginEditorContents(wxWindow *parent,
                         Vst3Plugin *target_plugin)
    :   wxWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize)
    {
        plugin_ = target_plugin;
        
        plugin_->AddEditorCloseListener(this);
        
        // いくつかのプラグイン (Arturia SEM V2やOszillos Mega Scopeなど) では、
        // IPlugViewに実際のウィンドウハンドルをattachするまえに、IPlugViewのサイズでウィンドウのサイズを設定しておかなければ、
        // 正しくプラグインウィンドウが表示されなかった。
        auto rc = plugin_->GetPreferredRect();
        SetSize(rc.getWidth(), rc.getHeight());
        
        plugin_->OpenEditor(GetHandle(), this);
        
        Show(true);
    }
    
    ~PluginEditorContents() {
        int x = 0;
        x = 1;
    }
    
    void OnEditorClosed(Vst3Plugin *) override
    {
        plugin_->RemoveEditorCloseListener(this);
        if(auto p = GetParent()) { p->Destroy(); }
    }
    
    void CloseEditor()
    {
        plugin_->RemoveEditorCloseListener(this);
        plugin_->CloseEditor();
    }
    
private:
    Vst3Plugin *plugin_ = nullptr;
    
    void OnResizePlugView(Steinberg::ViewRect const &rc) override
    {
        hwm::dout << "New view size: {}"_format(rc) << std::endl;
        
        SetSize(rc.getWidth(), rc.getHeight());
        GetParent()->Layout();
    }
};

class PluginEditorFrame
:   public wxFrame
{
public:
    PluginEditorFrame(wxWindow *parent,
                      Vst3Plugin *target_plugin,
                      std::function<void()> on_destroy)
    :   wxFrame(parent,
                wxID_ANY,
                target_plugin->GetEffectName(),
                wxDefaultPosition,
                wxDefaultSize,
                wxDEFAULT_FRAME_STYLE & ~(wxRESIZE_BORDER | wxMAXIMIZE_BOX)
                )
    {
        on_destroy_ = on_destroy;
        
        Bind(wxEVT_CLOSE_WINDOW, [this](auto &ev) {
            contents_->CloseEditor();
            Destroy();
        });
        
        contents_ = new PluginEditorContents(this, target_plugin);
        SetClientSize(contents_->GetSize());
        Show(true);
    }
    
    ~PluginEditorFrame() {
    }
    
    bool Destroy() override
    {
        on_destroy_();
        return wxFrame::Destroy();
    }
    
    bool Layout() override
    {
        if(contents_) {
            SetClientSize(contents_->GetSize());
        }
        return wxFrame::Layout();
    }
    
private:
    std::function<void()> on_destroy_;
    PluginEditorContents *contents_ = nullptr;
};

class Keyboard
:   public wxWindow
{
public:
    using PlayingNoteList = std::array<bool, 128>;
    
    Keyboard(wxWindow *parent, wxPoint pos = wxDefaultPosition, wxSize size = wxDefaultSize)
    :   wxWindow(parent, wxID_ANY, pos, size)
    {
        Bind(wxEVT_PAINT, [this](auto &ev) { OnPaint(); });
        playing_notes_[48] = true;
        playing_notes_[52] = true;
        playing_notes_[55] = true;
        playing_notes_[58] = true;
        
        timer_.Bind(wxEVT_TIMER, [this](auto &ev) { OnTimer(); });
        timer_.Start(50);
        Bind(wxEVT_LEFT_DOWN, [this](auto &ev) { OnLeftDown(ev); });
        Bind(wxEVT_LEFT_UP, [this](auto &ev) { OnLeftUp(ev); });
        Bind(wxEVT_MOTION, [this](auto &ev) { OnMotion(ev); });
        Bind(wxEVT_KEY_DOWN, [this](auto &ev) { OnKeyDown(ev); });
        Bind(wxEVT_KEY_UP, [this](auto &ev) { OnKeyUp(ev); });
        
        key_code_for_sample_note_.fill(0);
    }
    
    ~Keyboard()
    {}
    
    static constexpr int kKeyWidth = 15;
    static constexpr int kNumKeys = 128;
    static constexpr int kNumWhiteKeys = 75;
    static constexpr int kFullKeysWidth = kKeyWidth * kNumWhiteKeys;
    static constexpr int kWhiteKeyHeight = 100;
    static constexpr int kBlackKeyHeight = 60;
    
    static wxSize const kWhiteKey;
    static wxSize const kBlackKey;
    
    static wxColor const kWhiteKeyColor;
    static wxColor const kBlackKeyColor;
    static wxColor const kKeyBorderColor;
    static wxColor const kKeyBorderColorPlaying;
    static wxColor const kPlayingNoteColor;
    
    struct KeyProperty {
        KeyProperty(int x, wxSize sz, wxColor col) : rect_(wxPoint(x, 0), sz), color_(col) {}
        wxRect rect_;
        wxColour color_;
    };
    
    std::vector<KeyProperty> const kKeyPropertyList {
        { kKeyWidth * 0, kWhiteKey, kWhiteKeyColor },
        { int(kKeyWidth * 0.5 + 2), kBlackKey, kBlackKeyColor },
        { kKeyWidth * 1, kWhiteKey, kWhiteKeyColor },
        { int(kKeyWidth * 1.5 + 2), kBlackKey, kBlackKeyColor },
        { kKeyWidth * 2, kWhiteKey, kWhiteKeyColor },
        { kKeyWidth * 3, kWhiteKey, kWhiteKeyColor },
        { int(kKeyWidth * 3.5 + 2), kBlackKey, kBlackKeyColor },
        { kKeyWidth * 4, kWhiteKey, kWhiteKeyColor },
        { int(kKeyWidth * 4.5 + 2), kBlackKey, kBlackKeyColor },
        { kKeyWidth * 5, kWhiteKey, kWhiteKeyColor },
        { int(kKeyWidth * 5.5 + 2), kBlackKey, kBlackKeyColor },
        { kKeyWidth * 6, kWhiteKey, kWhiteKeyColor },
    };
    
    std::set<int> const kWhiteKeyIndcies = { 0, 2, 4, 5, 7, 9, 11 };
    std::set<int> const kBlackKeyIndcies = { 1, 3, 6, 8, 10 };
    
    void OnPaint()
    {
        wxPaintDC dc(this);
        
        auto rect = GetClientRect();
        
        dc.SetPen(wxPen(wxColor(0x26, 0x1E, 0x00)));
        dc.SetBrush(wxBrush(wxColor(0x26, 0x1E, 0x00)));
        dc.DrawRectangle(rect);
        
        int const disp_half = rect.GetWidth() / 2;
        int const disp_shift = kFullKeysWidth / 2 - disp_half;
 
        auto draw_key = [&](auto note_num, bool is_playing) {
            int const octave = note_num / 12;
            auto key = kKeyPropertyList[note_num % 12];
            auto key_rect = key.rect_;
            key_rect.Offset(octave * kKeyWidth * 7 - disp_shift, 0);
            
            if(key_rect.GetLeft() >= rect.GetWidth()) { return; }
            if(key_rect.GetRight() < 0) { return; }
            
            wxColor col_pen = kKeyBorderColor;
            wxColor col_brush = key.color_;
            if(is_playing) {
                col_pen = kKeyBorderColorPlaying;
                col_brush = kPlayingNoteColor;
            }
            dc.SetPen(wxPen(col_pen));
            dc.SetBrush(wxBrush(col_brush));
            dc.DrawRoundedRectangle(key_rect, 2);
        };
        
        for(int i = 0; i < kNumKeys; ++i) {
            if(kWhiteKeyIndcies.count(i % 12) != 0) { draw_key(i, playing_notes_[i]); }
        }
        for(int i = 0; i < kNumKeys; ++i) {
            if(kBlackKeyIndcies.count(i % 12) != 0) { draw_key(i, playing_notes_[i]); }
        }
        auto font = wxFont(wxFontInfo(wxSize(8, 10)).Family(wxFONTFAMILY_DEFAULT));
        dc.SetFont(font);
        for(int i = 0; i < kNumKeys; i += 12) {
            int const octave = i / 12;
            dc.DrawLabel(wxString::Format("C%d", i / 12 - 2),
                         wxBitmap(),
                         wxRect(wxPoint(octave * kKeyWidth * 7 - disp_shift, rect.GetHeight() * 0.8),
                                wxSize(kKeyWidth, 10)),
                         wxALIGN_CENTER
                         );
        }
    }
    
    void OnLeftDown(wxMouseEvent const &ev)
    {
        assert(last_dragging_note_ == std::nullopt);
        OnMotion(ev);
    }
    
    void OnLeftUp(wxMouseEvent const &ev)
    {
        if(last_dragging_note_) {
            SendSampleNoteOff(*last_dragging_note_);
        }
        
        last_dragging_note_ = std::nullopt;
    }
    
    void OnMotion(wxMouseEvent const &ev)
    {
        if(ev.LeftIsDown() == false) { return; }
        
        auto pt = ev.GetPosition();
        auto note = PointToNoteNumber(pt);
        
        if(last_dragging_note_ && last_dragging_note_ != note) {
            SendSampleNoteOff(*last_dragging_note_);
        }
        
        if(note) {
            SendSampleNoteOn(*note);
        }
        
        last_dragging_note_ = note;
    }
    
    void OnKeyDown(wxKeyEvent const &ev)
    {
        if(ev.HasAnyModifiers()) { return; }
        
        auto uc = ev.GetUnicodeKey();
        if(uc == WXK_NONE ) { return; }
        
        if(uc == kOctaveUp) {
            if(key_base_ + 12 < 128) { key_base_ += 12; }
            return;
        } else if(uc == kOctaveDown) {
            if(key_base_ - 12 >= 0) { key_base_ -= 12; }
            return;
        }
        
        auto found = std::find(kKeyTable.begin(), kKeyTable.end(), uc);
        if(found == kKeyTable.end()) { return; }
        int note_number = key_base_ + (found - kKeyTable.begin());
        if(note_number >= 128) { return; }
        
        key_code_for_sample_note_[note_number] = uc;
        SendSampleNoteOn(note_number);
    }
    
    void OnKeyUp(wxKeyEvent const &ev)
    {
        auto uc = ev.GetUnicodeKey();
        if(uc == WXK_NONE ) { return; }
        
        for(int i = 0; i < key_code_for_sample_note_.size(); ++i) {
            if(key_code_for_sample_note_[i] == uc) {
                SendSampleNoteOff(i);
                key_code_for_sample_note_[i] = 0;
            }
        }
    }
    
    std::optional<int> PointToNoteNumber(wxPoint pt)
    {
        auto rect = GetClientRect();
        int const disp_half = rect.GetWidth() / 2;
        int const disp_shift = kFullKeysWidth / 2 - disp_half;
        
        double const x = (pt.x + disp_shift);
        int const kOctaveWidth = 7.0 * kKeyWidth;
        
        int octave = (int)(x / (double)kOctaveWidth);
        int x_in_oct = x - (kOctaveWidth * octave);

        std::optional<int> found;
        for(auto const &list: { kBlackKeyIndcies, kWhiteKeyIndcies }) {
            for(auto index: list) {
                auto key = kKeyPropertyList[index];
                if(key.rect_.Contains(x_in_oct, pt.y)) {
                    if(!found) { found = (index + octave * 12); }
                    break;
                }
            }
        }

        if(found && 0 <= *found && *found < 128) {
            return *found;
        } else {
            return std::nullopt;
        }
    }
    
    void OnTimer()
    {
        auto app = MyApp::GetInstance();
        auto proj = app->GetProject();

        std::vector<Project::PlayingNoteInfo> list_seq = proj->GetPlayingSequenceNotes();
        std::vector<Project::PlayingNoteInfo> list_sample = proj->GetPlayingSampleNotes();
        
        PlayingNoteList tmp = {};
        
        for(auto &list: {list_seq, list_sample}) {
            for(auto &note: list) {
                tmp[note.pitch_] = true;
            }
        }
                
        if(tmp != playing_notes_) {
            playing_notes_ = tmp;
            Refresh();
        }
    }
    
private:
    void SendSampleNoteOn(UInt8 note_number)
    {
        assert(note_number < 128);
        
        auto app = MyApp::GetInstance();
        auto proj = app->GetProject();

        proj->SendSampleNoteOn(sample_note_channel, note_number);
    }
    
    void SendSampleNoteOff(UInt8 note_number)
    {
        assert(note_number < 128);
        
        auto app = MyApp::GetInstance();
        auto proj = app->GetProject();

        proj->SendSampleNoteOff(sample_note_channel, note_number);
    }
    
    void SendSampleNoteOffForAllKeyDown()
    {
        for(int i = 0; i < key_code_for_sample_note_.size(); ++i) {
            if(key_code_for_sample_note_[i] != 0) {
                SendSampleNoteOff(i);
            }
        }
        key_code_for_sample_note_.fill(0);
    }
    
private:
    //std::optional<wxPoint> _;
    std::optional<int> last_dragging_note_;
    std::array<wxChar, 128> key_code_for_sample_note_;
    wxTimer timer_;
    PlayingNoteList playing_notes_;
    int key_base_ = 60;
    constexpr static wxChar kOctaveDown = L'Z';
    constexpr static wxChar kOctaveUp = L'X';
    static std::vector<wxChar> const kKeyTable;
    int sample_note_channel = 0;
};

std::vector<wxChar> const Keyboard::kKeyTable = {
    L'A', L'W', L'S', L'E', L'D', L'F', L'T', L'G', L'Y', L'H', L'U', L'J', L'K', L'O', L'L', L'P'
};

wxSize const Keyboard::kWhiteKey { kKeyWidth, kWhiteKeyHeight };
wxSize const Keyboard::kBlackKey { kKeyWidth-4, kBlackKeyHeight };

wxColor const Keyboard::kWhiteKeyColor { 0xFF, 0xFF, 0xF6 };
wxColor const Keyboard::kBlackKeyColor { 0x00, 0x0D, 0x06 };
wxColor const Keyboard::kKeyBorderColor { 0x00, 0x00, 0x00 };
wxColor const Keyboard::kKeyBorderColorPlaying { 0x00, 0x00, 0x00, 0x30 };
wxColor const Keyboard::kPlayingNoteColor { 0x99, 0xEA, 0xFF };

class MyPanel
:   public wxPanel
,   public SingleInstance<MyPanel>
,   public MyApp::FactoryLoadListener
,   public MyApp::Vst3PluginLoadListener
{
public:
    MyPanel(wxWindow *parent)
    : wxPanel(parent)
    {
        this->SetBackgroundColour(wxColour(0x09, 0x21, 0x33));
        
        st_filepath_ = new wxStaticText(this, 100, "", wxDefaultPosition, wxSize(100, 20), wxST_NO_AUTORESIZE);
        st_filepath_->SetBackgroundColour(*wxWHITE);
        st_filepath_->Disable();
        
        btn_load_module_ = new wxButton(this, 101, "Load Module", wxDefaultPosition, wxSize(100, 20));
        
        cho_select_component_ = new wxChoice(this, 102, wxDefaultPosition, wxSize(100, 20));
        cho_select_component_->Hide();
        
        st_class_info_ = new wxStaticText(this, 103, "", wxDefaultPosition, wxSize(100, 100), wxST_NO_AUTORESIZE);
        st_class_info_->SetBackgroundColour(*wxWHITE);
        st_class_info_->Hide();
        
        cho_select_program_ = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxSize(100, 20));
        cho_select_program_->Hide();
        btn_open_editor_ = new wxButton(this, wxID_ANY, "Open Editor", wxDefaultPosition, wxSize(100, 20));
        btn_open_editor_->Hide();
        
        auto hbox1 = new wxBoxSizer(wxHORIZONTAL);
        
        hbox1->Add(st_filepath_, wxSizerFlags(1).Centre().Border(wxRIGHT));
        hbox1->Add(btn_load_module_, wxSizerFlags().Centre().Border(wxLEFT));
        
        auto hbox2 = new wxBoxSizer(wxHORIZONTAL);
        
        auto vbox2 = new wxBoxSizer(wxVERTICAL);
        vbox2->Add(cho_select_component_, wxSizerFlags(0).Expand().Border(wxBOTTOM));
        vbox2->Add(st_class_info_, wxSizerFlags(1).Expand().Border(wxTOP));
        
        auto vbox3 = new wxBoxSizer(wxVERTICAL);
        vbox3->Add(btn_open_editor_, wxSizerFlags(0).Border(wxBOTTOM));
        vbox3->Add(cho_select_program_, wxSizerFlags(0).Expand().Border(wxBOTTOM|wxTOP));
        vbox3->Add(1, 1, wxSizerFlags(1).Expand().Border(wxTOP));
        
        hbox2->Add(vbox2, wxSizerFlags(1).Expand().Border(wxRIGHT));
        hbox2->Add(vbox3, wxSizerFlags(1).Expand().Border(wxLEFT));
  
        auto vbox = new wxBoxSizer(wxVERTICAL);
        vbox->Add(hbox1, wxSizerFlags(0).Expand().Border(wxALL, 10));
        vbox->Add(hbox2, wxSizerFlags(1).Expand().Border(wxALL, 10));
        
        keyboard_ = new Keyboard(this, wxDefaultPosition, wxSize(0, Keyboard::kWhiteKeyHeight));
        Bind(wxEVT_KEY_DOWN, [this](auto &ev) { keyboard_->OnKeyDown(ev); });
        Bind(wxEVT_KEY_UP, [this](auto &ev) { keyboard_->OnKeyUp(ev); });
        
        vbox->Add(keyboard_, wxSizerFlags(0).Expand());
        
        SetSizerAndFit(vbox);
        
        Bind(wxEVT_PAINT, [this](auto &ev) { OnPaint(ev); });
        btn_load_module_->Bind(wxEVT_BUTTON, [this](auto &ev) { OnLoadModule(); });
        cho_select_component_->Bind(wxEVT_CHOICE, [this](auto &ev) { OnSelectComponent(); });
        btn_open_editor_->Bind(wxEVT_BUTTON, [this](auto &ev) { OnOpenEditor(); });
        cho_select_program_->Bind(wxEVT_CHOICE, [this](auto &ev) { OnSelectProgram(); });
        
        MyApp::GetInstance()->AddFactoryLoadListener(this);
        MyApp::GetInstance()->AddVst3PluginLoadListener(this);
    }
    
    ~MyPanel()
    {
        if(editor_frame_) { editor_frame_->Destroy(); }
        MyApp::GetInstance()->RemoveVst3PluginLoadListener(this);
        MyApp::GetInstance()->RemoveFactoryLoadListener(this);
    }
    
private:
    void OnPaint(wxPaintEvent &)
    {
        wxPaintDC pdc(this);
        Draw(pdc);
    }
    
    void Draw(wxDC &dc)
    {
        //dc.SetBrush(wxBrush(wxColour(0x09, 0x21, 0x33)));
        //dc.DrawRectangle(GetClientRect());
    }
    
    void OnLoadModule()
    {
        // load
        wxFileDialog openFileDialog(this,
                                    "Open VST3 file",
                                    //*
                                    "/Library/Audio/Plug-Ins/VST3",
                                    /*/
                                    "../../ext/vst3sdk/build_debug/VST3/Debug",
                                    //*/
                                    "",
                                    "VST3 files (*.vst3)|*.vst3", wxFD_OPEN|wxFD_FILE_MUST_EXIST);
        if (openFileDialog.ShowModal() == wxID_CANCEL) {
            return;
        }
        
        auto path = openFileDialog.GetPath();
        MyApp::GetInstance()->LoadFactory(path);
    }
    
    void OnFactoryLoaded(String path, Vst3PluginFactory *factory) override
    {
        auto const num = factory->GetComponentCount();
        
        st_filepath_->SetLabel(path);
        
        for(int i = 0; i < num; ++i) {
            auto const &info = factory->GetComponentInfo(i);
            
            hwm::dout << L"{}, {}"_format(info.name(), info.category()) << std::endl;
            
            //! カテゴリがkVstAudioEffectClassなComponentを探索する。
            if(info.category() == hwm::to_wstr(kVstAudioEffectClass)) {
                cho_select_component_->Append(info.name(), reinterpret_cast<void*>(i));
            }
        }
        
        cho_select_component_->SetSelection(wxNOT_FOUND);
        cho_select_component_->Show();
        Layout();
    }
    
    void OnFactoryUnloaded() override
    {
        cho_select_component_->Clear();
    }
    
    class ProgramData : public wxClientData
    {
    public:
        ProgramData(UInt32 program_index, Vst::UnitID unit_id)
        :   program_index_(program_index)
        ,   unit_id_(unit_id)
        {}
        
        UInt32 program_index_ = -1;
        Vst::UnitID unit_id_ = -1;
    };
    
    void OnVst3PluginLoaded(Vst3Plugin *plugin) override
    {
        cho_select_program_->Clear();
        
        auto const num = plugin->GetNumUnitInfo();
        for(int un = 0; un < num; ++un) {
            auto const &unit_info = plugin->GetUnitInfoByIndex(un);
            auto const &pl = unit_info.program_list_;
            if(pl.id_ == Vst::kNoProgramListId) { continue; }
            
            cho_select_program_->Append(L"----[" + unit_info.name_ + L"]----");
            for(UInt32 pn = 0; pn < pl.programs_.size(); ++pn) {
                auto const entry = pl.programs_[pn];
                cho_select_program_->Append(entry.name_, new ProgramData{pn, unit_info.id_});
            }
        }
        
        if(num > 0) {
            cho_select_program_->SetSelection(plugin->GetProgramIndex());
        }
        
        btn_open_editor_->Enable(plugin->HasEditor());
    }
    
    void OnVst3PluginUnloaded(Vst3Plugin *plugin) override
    {
        btn_open_editor_->Disable();
    }
    
    void OnSelectComponent() {
        auto sel = cho_select_component_->GetSelection();
        if(sel == wxNOT_FOUND) { return; }
        
        auto const p = cho_select_component_->GetClientData(sel);
        auto const component_index = (int)reinterpret_cast<ptrdiff_t>(p);
        auto const successful = MyApp::GetInstance()->LoadVst3Plugin(component_index);
        
        if(successful) {
            auto const &info = MyApp::GetInstance()->GetFactory()->GetComponentInfo(component_index);
            auto str = wxString::Format(L"[%s]\ncategory: ", info.name(), info.category());
            
            if(info.has_classinfo2()) {
                auto info2 = info.classinfo2();
                str += L"\n";
                str += wxString::Format(L"sub categories: %s\nvendor: %s\nversion: %s\nsdk_version: %s",
                                        info2.sub_categories(),
                                        info2.vendor(),
                                        info2.version(),
                                        info2.sdk_version());
                st_class_info_->SetLabel(str);
            }
            st_class_info_->Show();
            btn_open_editor_->Show();
            cho_select_program_->Show();
            Layout();
        }
    }
    
    void OnOpenEditor()
    {
        if(editor_frame_) { return; }
        
        editor_frame_ = new PluginEditorFrame(this,
                                              MyApp::GetInstance()->GetPlugin(),
                                              [this] {
                                                  editor_frame_ = nullptr;
                                                  btn_open_editor_->Enable();
                                              });
        btn_open_editor_->Disable();
    }
    
    void OnSelectProgram()
    {
        auto sel = cho_select_program_->GetSelection();
        if(sel == wxNOT_FOUND) { return; }
        if(cho_select_program_->HasClientObjectData() == false) { return; }
        
        auto data = dynamic_cast<ProgramData const *>(cho_select_program_->GetClientObject(sel));
        if(!data) { return; }
        
        auto plugin = MyApp::GetInstance()->GetPlugin();
        plugin->SetProgramIndex(data->program_index_, data->unit_id_);
    }
    
    wxStaticText    *st_filepath_;
    wxButton        *btn_load_module_;
    wxChoice        *cho_select_component_;
    wxStaticText    *st_class_info_;
    wxChoice        *cho_select_program_;
    wxButton        *btn_open_editor_;
    Keyboard        *keyboard_;
    PluginEditorFrame *editor_frame_ = nullptr;
};

enum
{
    ID_Play = 1,
    ID_EnableInputs = 2,
};

MyFrame::MyFrame(const wxString& title, const wxPoint& pos, const wxSize& size)
: wxFrame(NULL, wxID_ANY, title, pos, size)
{
    wxMenu *menuFile = new wxMenu;
    menuFile->Append(wxID_EXIT);
    
    wxMenu *menuPlay = new wxMenu;
    menuPlay->Append(ID_Play, "&Play...\tCtrl-P", "Start playback", wxITEM_CHECK);
    menuPlay->Append(ID_EnableInputs,
                     "&Enable Mic Inputs...\tCtrl-I",
                     "Input Mic Inputs",
                     wxITEM_CHECK)
    ->Enable(Project::GetInstance()->CanInputsEnabled());

    wxMenu *menuHelp = new wxMenu;
    menuHelp->Append(wxID_ABOUT);

    wxMenuBar *menuBar = new wxMenuBar;
    menuBar->Append( menuFile, "&File" );
    menuBar->Append( menuPlay, "&Play" );
    menuBar->Append( menuHelp, "&Help" );
    SetMenuBar( menuBar );
    
    Bind(wxEVT_MENU, [this](auto &ev) { OnExit(); }, wxID_EXIT);
    //Bind(wxEVT_CLOSE_WINDOW, [this](auto &ev) { OnExit(); });
    Bind(wxEVT_COMMAND_MENU_SELECTED, [this](auto &ev) { OnPlay(ev); }, ID_Play);
    Bind(wxEVT_COMMAND_MENU_SELECTED, [this](auto &ev) { OnEnableInputs(ev); }, ID_EnableInputs);
    
    Bind(wxEVT_MENU, [this](auto &ev) { OnAbout(ev); }, wxID_ABOUT);
    
    timer_.SetOwner(this);
    Bind(wxEVT_TIMER, [this](auto &ev) { OnTimer(); });
    timer_.Start(1000);
    
    my_panel_ = new MyPanel(this);
    my_panel_->SetSize(GetClientSize());
}

MyFrame::~MyFrame()
{
    int x = 0;
    x = 1;
}

bool MyFrame::Destroy()
{
    MyApp::GetInstance()->BeforeExit();
    //my_panel_->Destroy();
    return wxFrame::Destroy();
}

void MyFrame::OnExit()
{
    //MyApp::GetInstance()->BeforeExit();
    Close( true );
}

void MyFrame::OnAbout(wxCommandEvent& event)
{
    wxMessageBox( "VST3HostDemo",
                 "created by hotwatermorning@gmail.com", wxOK | wxICON_INFORMATION );
}

void MyFrame::OnPlay(wxCommandEvent &ev)
{
    auto &tp = Project::GetInstance()->GetTransporter();
    tp.SetPlaying(ev.IsChecked());
}

void MyFrame::OnEnableInputs(wxCommandEvent &ev)
{
    auto *pj = Project::GetInstance();
    pj->SetInputsEnabled(ev.IsChecked());
}

void MyFrame::OnTimer()
{
}

NS_HWM_END
