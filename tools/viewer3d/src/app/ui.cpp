#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS  // ImVec2 + ImVec2; o imgui_internal.h pede antes do primeiro imgui.h
#endif
#include "app/ui.hpp"

#include "app/embedded_fonts.hpp"
#include "app/icons.hpp"
#include "edit/edits_json.hpp"
#include "edit/history.hpp"
#include "render/gl.hpp"
#include "render/pick.hpp"
#include "render/texture.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <webp/decode.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>
#include <imgui_internal.h>
#include <ImGuizmo.h>
#include <glm/gtc/type_ptr.hpp>
#ifdef IMGUI_ENABLE_FREETYPE
#include <misc/freetype/imgui_freetype.h>
#endif

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace dr2::app {

namespace {

constexpr float kPi = 3.14159265358979f;
// Sobe quando o layout padrão dos painéis muda: um imgui.ini com versão menor é refeito.
constexpr int kLayoutVersion = 1;

// ---- Tema: cinzas frios, um azul de destaque e as cores de estado

constexpr ImVec4 rgb(int r, int g, int b, float a = 1.0f) {
    return ImVec4(static_cast<float>(r) / 255.0f, static_cast<float>(g) / 255.0f, static_cast<float>(b) / 255.0f, a);
}

constexpr ImVec4 kBg0 = rgb(22, 23, 25);  // barras (menu, ferramentas, status) e abas de trás
constexpr ImVec4 kBg1 = rgb(31, 32, 35);  // painéis
constexpr ImVec4 kBg2 = rgb(43, 45, 49);  // botões
constexpr ImVec4 kBg3 = rgb(54, 57, 62);
constexpr ImVec4 kBg4 = rgb(66, 70, 77);
constexpr ImVec4 kLine = rgb(12, 13, 14);  // linhas entre as áreas
constexpr ImVec4 kText = rgb(221, 223, 226);
constexpr ImVec4 kTextDim = rgb(132, 137, 145);
constexpr ImVec4 kAccent = rgb(66, 133, 244);
constexpr ImVec4 kAccentSoft = rgb(40, 82, 150);
constexpr ImVec4 kAccentText = rgb(125, 172, 250);
constexpr ImVec4 kRed = rgb(236, 104, 94);
constexpr ImVec4 kYellow = rgb(232, 184, 76);
constexpr ImVec4 kGreen = rgb(96, 196, 122);
constexpr ImVec4 kPlay = rgb(72, 190, 104);
constexpr ImVec4 kStop = rgb(232, 96, 86);
constexpr ImVec4 kAxis[3] = {rgb(226, 86, 76), rgb(108, 190, 82), rgb(76, 136, 238)};  // X Y Z, também no gizmo

constexpr ImVec4 with_alpha(ImVec4 c, float a) { return ImVec4(c.x, c.y, c.z, a); }
ImU32 u32(const ImVec4& c) { return ImGui::GetColorU32(c); }  // com o alfa do estilo (some junto nos desabilitados)
ImU32 raw(const ImVec4& c) { return ImGui::ColorConvertFloat4ToU32(c); }

void apply_theme(float scale) {
    ImGuiStyle& s = ImGui::GetStyle();
    s = ImGuiStyle();
    ImGui::StyleColorsDark(&s);
    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = kText;
    c[ImGuiCol_TextDisabled] = kTextDim;
    c[ImGuiCol_WindowBg] = kBg1;
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = rgb(36, 38, 41);
    c[ImGuiCol_Border] = kLine;  // também a divisória entre painéis encaixados
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = rgb(20, 21, 23);  // campos: fundos, mais escuros que o painel
    c[ImGuiCol_FrameBgHovered] = rgb(26, 27, 30);
    c[ImGuiCol_FrameBgActive] = rgb(30, 32, 35);
    c[ImGuiCol_TitleBg] = kBg0;
    c[ImGuiCol_TitleBgActive] = kBg0;
    c[ImGuiCol_TitleBgCollapsed] = kBg0;
    c[ImGuiCol_MenuBarBg] = kBg0;
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = kBg3;
    c[ImGuiCol_ScrollbarGrabHovered] = kBg4;
    c[ImGuiCol_ScrollbarGrabActive] = rgb(84, 89, 97);
    c[ImGuiCol_CheckMark] = kAccentText;
    c[ImGuiCol_SliderGrab] = kAccent;
    c[ImGuiCol_SliderGrabActive] = kAccentText;
    c[ImGuiCol_Button] = kBg2;
    c[ImGuiCol_ButtonHovered] = kBg3;
    c[ImGuiCol_ButtonActive] = kBg4;
    c[ImGuiCol_Header] = kAccentSoft;  // linha selecionada
    c[ImGuiCol_HeaderHovered] = ImVec4(1, 1, 1, 0.07f);
    c[ImGuiCol_HeaderActive] = ImVec4(1, 1, 1, 0.11f);
    c[ImGuiCol_Separator] = rgb(50, 52, 57);
    c[ImGuiCol_SeparatorHovered] = with_alpha(kAccent, 0.6f);
    c[ImGuiCol_SeparatorActive] = kAccent;
    c[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ResizeGripHovered] = with_alpha(kAccent, 0.6f);  // e a divisória sob o mouse
    c[ImGuiCol_ResizeGripActive] = kAccent;
    c[ImGuiCol_Tab] = kBg0;
    c[ImGuiCol_TabHovered] = kBg2;
    c[ImGuiCol_TabSelected] = kBg1;
    c[ImGuiCol_TabSelectedOverline] = kAccent;
    c[ImGuiCol_TabDimmed] = kBg0;
    c[ImGuiCol_TabDimmedSelected] = kBg1;
    c[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(0, 0, 0, 0);  // a linha azul só no painel com o foco
    c[ImGuiCol_DockingPreview] = with_alpha(kAccent, 0.55f);
    c[ImGuiCol_DockingEmptyBg] = kBg0;
    c[ImGuiCol_PlotHistogram] = kAccent;
    c[ImGuiCol_PlotHistogramHovered] = kAccentText;
    c[ImGuiCol_TableHeaderBg] = kBg2;
    c[ImGuiCol_TableBorderStrong] = kLine;
    c[ImGuiCol_TableBorderLight] = rgb(44, 46, 50);
    c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.025f);
    c[ImGuiCol_TextLink] = kAccentText;
    c[ImGuiCol_TextSelectedBg] = with_alpha(kAccent, 0.35f);
    c[ImGuiCol_DragDropTarget] = kAccent;
    c[ImGuiCol_NavCursor] = kAccent;
    c[ImGuiCol_NavWindowingHighlight] = ImVec4(1, 1, 1, 0.7f);
    c[ImGuiCol_NavWindowingDimBg] = ImVec4(0, 0, 0, 0.4f);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.5f);

    s.WindowPadding = ImVec2(10, 8);
    s.FramePadding = ImVec2(7, 4);
    s.CellPadding = ImVec2(6, 3);
    s.ItemSpacing = ImVec2(8, 5);
    s.ItemInnerSpacing = ImVec2(6, 4);
    s.IndentSpacing = 16;
    s.ScrollbarSize = 12;
    s.GrabMinSize = 10;
    s.WindowBorderSize = 1;
    s.ChildBorderSize = 1;
    s.PopupBorderSize = 1;
    s.FrameBorderSize = 0;
    s.TabBorderSize = 0;
    s.TabBarBorderSize = 1;
    s.TabBarOverlineSize = 2;
    s.WindowRounding = 6;
    s.ChildRounding = 4;
    s.FrameRounding = 4;
    s.PopupRounding = 6;
    s.ScrollbarRounding = 6;
    s.GrabRounding = 4;
    s.TabRounding = 4;
    s.TabCloseButtonMinWidthSelected = 0;  // o X da aba só aparece com o mouse em cima
    s.DockingSeparatorSize = 2;
    s.WindowMenuButtonPosition = ImGuiDir_None;
    s.WindowTitleAlign = ImVec2(0.0f, 0.5f);
    s.SeparatorTextBorderSize = 1;
    s.SeparatorTextAlign = ImVec2(0.0f, 0.5f);
    s.SeparatorTextPadding = ImVec2(0, 4);
    s.DisabledAlpha = 0.45f;
    s.HoverFlagsForTooltipMouse = ImGuiHoveredFlags_Stationary | ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled;
    s.ScaleAllSizes(scale);
}

// Fontes embutidas (assets/fonts): Inter no texto e seminegrito nos títulos, as duas com os ícones Lucide
// misturados; e os ícones sozinhos, maiores, para a barra de ferramentas.
struct Fonts {
    ImFont* text = nullptr;
    ImFont* head = nullptr;
    ImFont* tool = nullptr;
};

Fonts load_fonts(float scale) {
    ImGuiIO& io = ImGui::GetIO();
#ifdef IMGUI_ENABLE_FREETYPE
    io.Fonts->FontBuilderFlags = ImGuiFreeTypeBuilderFlags_LightHinting;
#endif
    const float text_px = std::round(15.0f * scale), icon_px = std::round(16.0f * scale), tool_px = std::round(18.0f * scale);
    auto add = [&](const fonts::Blob& blob, float px, const ImWchar* ranges, ImFontConfig cfg) {
        cfg.FontDataOwnedByAtlas = false;  // os bytes ficam no executável
        return io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(blob.data), static_cast<int>(blob.size), px, &cfg, ranges);
    };
    // O ícone (Lucide: o quadrado do corpo vai da linha de base até px acima dela) centrado na altura das
    // maiúsculas do Inter (1490 das 2478 unidades entre o topo e o pé da linha).
    ImFontConfig merged;
    merged.MergeMode = true;
    merged.PixelSnapH = true;
    merged.GlyphMinAdvanceX = icon_px;
    merged.GlyphOffset.y = std::round((icon_px - 1490.0f * text_px / 2478.0f) * 0.5f);
    Fonts f;
    f.text = add(fonts::kInterRegular, text_px, icons::kTextRanges, ImFontConfig());
    add(fonts::kLucide, icon_px, icons::kIconRanges, merged);
    f.head = add(fonts::kInterSemiBold, text_px, icons::kTextRanges, ImFontConfig());
    add(fonts::kLucide, icon_px, icons::kIconRanges, merged);
    ImFontConfig tool;
    tool.PixelSnapH = true;
    f.tool = add(fonts::kLucide, tool_px, icons::kIconRanges, tool);
    return f;
}

// ---- Ajudantes

// "e:core_barr~a" -> "core_barr~a" (o kind vai à parte)
std::string short_name(const std::string& name) { return name.size() > 2 && name[1] == ':' ? name.substr(2) : name; }

// Caminho para mostrar: relativo à pasta atual quando fica dentro dela (examples/saves/...), senão o completo.
std::string near_path(const std::string& path) {
    std::error_code ec;
    const std::filesystem::path rel = std::filesystem::proximate(path, std::filesystem::current_path(ec), ec);
    if (ec || rel.empty() || *rel.begin() == "..") return path;
    return rel.generic_string();
}

bool contains_ci(const std::string& text, const char* needle) {
    if (!*needle) return true;
    const auto it = std::search(text.begin(), text.end(), needle, needle + std::strlen(needle),
                                [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
    return it != text.end();
}

// Rótulo de uma instância: id de texto do objects.ens se houver, senão o idnum; cópias dizem de quem.
std::string instance_label(const TrackView& track, std::uint32_t i) {
    const Instances& inst = track.instances();
    const std::uint32_t id = inst.idnum[i];
    if (id >= kAdded) return "cópia de #" + std::to_string(id - kAdded);
    const std::string& name = track.track().types[inst.type[i]].name;
    const auto& ens = track.route().ens_ids;
    if (!name.empty() && name[0] == 'e' && id < ens.size() && !ens[id].empty()) return "#" + std::to_string(id) + "  " + ens[id];
    return "#" + std::to_string(id);
}

const char* layer_icon(render::Layer layer) {
    switch (layer) {
    case render::Layer::Tree: return ICON_TREES;
    case render::Layer::Dist: return ICON_FAR_TERRAIN;
    default: return ICON_OBJECTS;
    }
}

// Ângulo em Y da matriz (convenção de edit::spin: a linha 0 vai de +x para −z com θ positivo).
float yaw_of(const float* m) { return std::atan2(-m[2], m[0]); }

bool project(const glm::mat4& view_proj, const Rect& vp, const glm::vec3& p, glm::vec2& out) {
    const glm::vec4 c = view_proj * glm::vec4(p, 1.0f);
    if (c.w <= 1e-4f) return false;
    out = {vp.x + (c.x / c.w * 0.5f + 0.5f) * vp.w, vp.y + (0.5f - c.y / c.w * 0.5f) * vp.h};
    return true;
}

float snapped(float v, float step) { return step > 0 ? std::round(v / step) * step : v; }

// 0.25 -> "0.25 m" (ponto, como nos campos e no resto do editor)
std::string step_text(float v, const char* unit) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%g%s", static_cast<double>(v), unit);
    return buf;
}

// Mensagem de erro na barra de status (vermelha, fica mais tempo).
bool bad_message(const std::string& m) {
    return m.starts_with("NÃO") || m.starts_with("não ") || m.starts_with("ATENÇÃO") || m.find("falhou") != std::string::npos;
}

// Texto alinhado à direita do espaço que sobra na linha (célula de tabela ou janela). `frame`: na altura do
// texto de um campo (linhas da Cena com FramePadding).
void right_text(const std::string& text, const ImVec4& color, bool frame = false) {
    const float w = ImGui::CalcTextSize(text.c_str()).x;
    const float avail = ImGui::GetContentRegionAvail().x;
    if (avail > w) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - w);
    if (frame) ImGui::AlignTextToFramePadding();
    ImGui::TextColored(color, "%s", text.c_str());
}

// Na mesma linha, encostado na direita.
void same_line_right(const char* text, const ImVec4& color) {
    ImGui::SameLine();
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float x = at.x + ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(text).x;
    ImGui::SetCursorScreenPos(ImVec2(std::max(at.x, x), at.y));
    ImGui::TextColored(color, "%s", text);
}

// Bolinha de estado antes de um texto na mesma linha.
void dot(const ImVec4& color) {
    const float h = ImGui::GetTextLineHeight();
    const float r = std::round(h * 0.2f);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(2.0f * r, h));
    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + r, p.y + h * 0.5f + 0.5f), r, u32(color));
    ImGui::SameLine(0, std::round(r * 1.6f));
}

// Cabeçalho que dobra (Matriz, Tipo e materiais): cinza, para não competir com a seleção azul.
bool fold(const char* label, ImGuiTreeNodeFlags flags = 0) {
    ImGui::PushStyleColor(ImGuiCol_Header, kBg2);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, kBg3);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, kBg4);
    const bool open = ImGui::CollapsingHeader(label, flags);
    ImGui::PopStyleColor(3);
    return open;
}

// Caixa de marcar e botão de opção no tom dos botões: no fundo escuro dos campos eles sumiam no painel.
void push_toggle_colors() {
    ImGui::PushStyleColor(ImGuiCol_FrameBg, kBg3);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, kBg4);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, rgb(84, 89, 97));
}

bool check(const char* label, bool* v) {
    push_toggle_colors();
    const bool changed = ImGui::Checkbox(label, v);
    ImGui::PopStyleColor(3);
    return changed;
}

bool radio(const char* label, bool active) {
    push_toggle_colors();
    const bool pressed = ImGui::RadioButton(label, active);
    ImGui::PopStyleColor(3);
    return pressed;
}

// Botão principal de uma janela (o que o Enter faz).
bool primary_button(const char* label, const ImVec4& color = kAccent) {
    ImGui::PushStyleColor(ImGuiCol_Button, color);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(std::min(1.0f, color.x * 1.12f), std::min(1.0f, color.y * 1.12f), std::min(1.0f, color.z * 1.12f), 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(color.x * 0.85f, color.y * 0.85f, color.z * 0.85f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    const bool pressed = ImGui::Button(label);
    ImGui::PopStyleColor(4);
    return pressed;
}

// Tabela de propriedades: nome cinza à esquerda, valor à direita (quebra linha se não couber).
bool begin_props(const char* id) {
    if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings)) return false;
    ImGui::TableSetupColumn("nome", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("valor", ImGuiTableColumnFlags_WidthStretch);
    return true;
}

void prop(const char* key, const std::string& value, const ImVec4& color = kText) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextDisabled("%s", key);
    ImGui::TableNextColumn();
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextWrapped("%s", value.c_str());
    ImGui::PopStyleColor();
}

std::string fmt(const char* format, ...) IM_FMTARGS(1);
std::string fmt(const char* format, ...) {
    char buf[512];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buf, sizeof buf, format, args);
    va_end(args);
    return buf;
}

// Só o Ring tem o porte para o jogo (scripts/research/ring_deploy.py).
constexpr const char* kPortedTrack = "synthetic__dr2hook_ring";
const char* const kModeArg[] = {"bot", "drive", "freecam"};

constexpr float kMoveSteps[] = {0.1f, 0.25f, 0.5f, 1.0f, 2.0f, 5.0f};
constexpr float kTurnSteps[] = {1.0f, 5.0f, 10.0f, 15.0f, 30.0f, 45.0f, 90.0f};

const char* const kToolIcons[] = {ICON_SELECT, ICON_MOVE, ICON_ROTATE};
const char* const kToolNames[] = {"Selecionar", "Mover", "Girar"};

}  // namespace

// Seção [DR2Editor][Editor] do imgui.ini: painéis à mostra, versão do layout e as opções do editor.
struct UiSettings {
    static void* open(ImGuiContext*, ImGuiSettingsHandler* handler, const char* name) {
        return std::strcmp(name, "Editor") == 0 ? handler->UserData : nullptr;
    }
    static void read_line(ImGuiContext*, ImGuiSettingsHandler*, void* entry, const char* line) {
        EditorUi& ui = *static_cast<EditorUi*>(entry);
        int i = 0;
        float f = 0.0f;
        if (std::sscanf(line, "Layout=%d", &i) == 1) ui.layout_version_ = i;
        else if (std::sscanf(line, "Cena=%d", &i) == 1) ui.show_scene_ = i != 0;
        else if (std::sscanf(line, "Historico=%d", &i) == 1) ui.show_history_ = i != 0;
        else if (std::sscanf(line, "Inspector=%d", &i) == 1) ui.show_inspector_ = i != 0;
        else if (std::sscanf(line, "Encaixe=%d", &i) == 1) ui.snap_ = i != 0;
        else if (std::sscanf(line, "PassoMover=%f", &f) == 1 && f > 0.0f) ui.snap_move_step_ = f;
        else if (std::sscanf(line, "PassoGirar=%f", &f) == 1 && f > 0.0f) ui.snap_turn_step_ = f;
        else if (std::sscanf(line, "GrudarNoChao=%d", &i) == 1) ui.follow_ground_ = i != 0;
        else if (std::sscanf(line, "EixosDoObjeto=%d", &i) == 1) ui.gizmo_local_ = i != 0;
        else if (std::sscanf(line, "TesteRapido=%d", &i) == 1) ui.launch_.quick = i != 0;
        else if (std::sscanf(line, "TesteModo=%d", &i) == 1 && (i == 0 || i == 2)) ui.launch_.mode = i;
    }
    static void write_all(ImGuiContext*, ImGuiSettingsHandler* handler, ImGuiTextBuffer* out) {
        const EditorUi& ui = *static_cast<const EditorUi*>(handler->UserData);
        out->appendf("[%s][Editor]\n%s\n", handler->TypeName, ui.settings_text().c_str());
    }
};

EditorUi::EditorUi(SDL_Window* window, void* gl_context, bool persist) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigWindowsMoveFromTitleBarOnly = true;  // arrastar dentro de um painel solto não o leva junto
    io.IniFilename = nullptr;                     // nada de imgui.ini na pasta de trabalho
    if (persist) {
        if (char* pref = SDL_GetPrefPath("DR2ModLoader", "viewer3d")) {
            ini_path_ = std::string(pref) + "imgui.ini";
            SDL_free(pref);
            io.IniFilename = ini_path_.c_str();
        }
    }
    ImGuiSettingsHandler handler;
    handler.TypeName = "DR2Editor";
    handler.TypeHash = ImHashStr("DR2Editor");
    handler.ReadOpenFn = UiSettings::open;
    handler.ReadLineFn = UiSettings::read_line;
    handler.WriteAllFn = UiSettings::write_all;
    handler.UserData = this;
    ImGui::AddSettingsHandler(&handler);

    scale_ = std::max(1.0f, SDL_GetWindowDisplayScale(window));
    apply_theme(scale_);
    const Fonts f = load_fonts(scale_);
    font_text_ = f.text;
    font_head_ = f.head;
    font_tool_ = f.tool;
    ImGui_ImplSDL3_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init("#version 330");
}

EditorUi::~EditorUi() {
    if (about_tex_) glDeleteTextures(1, &about_tex_);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();  // grava o imgui.ini
}

bool EditorUi::event(const SDL_Event& e) {
    const ImGuiIO& io = ImGui::GetIO();
    // teclas de atalho (sem campo ativo, sem menu ou janela aberta) não vão ao ImGui: a fila dele
    // trata uma tecla por quadro, e com poucos fps centenas de teclas atrasam o menu por dezenas de
    // segundos. Modificadores vão sempre (Ctrl+clique para digitar), e a soltura de uma tecla que o
    // ImGui recebeu também.
    const bool popup = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    const bool ui_keys = io.WantCaptureKeyboard || io.WantTextInput || popup;
    if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) {
        const SDL_Scancode sc = e.key.scancode;
        const bool modifier = (e.key.key == SDLK_LCTRL || e.key.key == SDLK_RCTRL || e.key.key == SDLK_LSHIFT ||
                               e.key.key == SDLK_RSHIFT || e.key.key == SDLK_LALT || e.key.key == SDLK_RALT);
        // repetição e soltura de uma tecla cujo apertar foi ao ImGui continuam indo para ele (senão ela fica presa)
        const bool held = sc < SDL_SCANCODE_COUNT && imgui_keys_[sc];
        const bool forward = modifier || held || (e.type == SDL_EVENT_KEY_DOWN && ui_keys);
        if (sc < SDL_SCANCODE_COUNT) {
            if (e.type == SDL_EVENT_KEY_DOWN && forward) imgui_keys_[sc] = true;
            if (e.type == SDL_EVENT_KEY_UP) imgui_keys_[sc] = false;
        }
        if (forward) ImGui_ImplSDL3_ProcessEvent(&e);
        return e.type == SDL_EVENT_KEY_DOWN && ui_keys;
    }
    if (e.type == SDL_EVENT_TEXT_INPUT) {
        if (io.WantTextInput) ImGui_ImplSDL3_ProcessEvent(&e);
        return io.WantCaptureKeyboard;
    }
    ImGui_ImplSDL3_ProcessEvent(&e);
    switch (e.type) {
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        return io.WantCaptureMouse;
    case SDL_EVENT_MOUSE_WHEEL:
        return io.WantCaptureMouse && !ImGuizmo::IsOver();  // a roda sobre o gizmo ainda dá zoom
    default:
        return false;
    }
}

bool EditorUi::wants_keyboard() const { return ImGui::GetIO().WantCaptureKeyboard; }

bool EditorUi::ask_quit(TrackView* track) {
    if (!track || !track->unsaved()) return true;
    pending_ = Pending::Quit;
    return false;
}

void EditorUi::ask_open(TrackView* track, const std::string& dir) {
    if (track && track->unsaved()) {
        pending_ = Pending::Open;
        pending_dir_ = dir;
    } else {
        open_request = dir;
    }
}

void EditorUi::scan_tracks() {
    tracks_.clear();
    std::error_code ec;
    for (const char* root : {"build/uiview/tracks", "examples/tracks"}) {  // exportadas primeiro, exemplos no fim
        const std::size_t first = tracks_.size();
        for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
            if (std::filesystem::is_regular_file(entry.path() / "track.json", ec)) tracks_.push_back(entry.path().generic_string());
        }
        std::sort(tracks_.begin() + static_cast<std::ptrdiff_t>(first), tracks_.end());
    }
    tracks_scanned_ = true;
}

std::string EditorUi::window_title(const TrackView* track) {
    if (!track) return "DR2Hook Editor de Pistas";
    const std::string file = std::filesystem::path(track->out_path()).filename().string();
    return track->track().id + " · " + file + (track->unsaved() ? " *" : "") + " — DR2Hook Editor de Pistas";
}

bool EditorUi::open_on_start(const std::string& what) {
    if (what == "exibicao") display_request_ = true;
    else if (what == "atalhos") show_help_ = true;
    else if (what == "sobre") about_request_ = true;
    else return false;
    return true;
}

std::string EditorUi::settings_text() const {
    char buf[320];
    std::snprintf(buf, sizeof buf,
                  "Layout=%d\nCena=%d\nHistorico=%d\nInspector=%d\nEncaixe=%d\nPassoMover=%g\nPassoGirar=%g\nGrudarNoChao=%d\n"
                  "EixosDoObjeto=%d\nTesteRapido=%d\nTesteModo=%d\n",
                  layout_version_, show_scene_ ? 1 : 0, show_history_ ? 1 : 0, show_inspector_ ? 1 : 0, snap_ ? 1 : 0,
                  static_cast<double>(snap_move_step_), static_cast<double>(snap_turn_step_), follow_ground_ ? 1 : 0,
                  gizmo_local_ ? 1 : 0, launch_.quick ? 1 : 0, launch_.mode);
    return buf;
}

void EditorUi::watch_settings() {
    if (!ImGui::GetIO().IniFilename) return;
    std::string now = settings_text();
    if (now == settings_seen_) return;
    if (!settings_seen_.empty()) ImGui::MarkIniSettingsDirty();  // o ImGui grava alguns segundos depois
    settings_seen_ = std::move(now);
}

void EditorUi::update_focus(const TrackView& track) {
    const int obj = track.selected();
    const int slot = track.route().slot_index(track.slot_selected());
    const int cam = track.replay_selected();
    if (obj != focus_obj_ && obj >= 0) focus_ = Focus::Object;
    else if (slot != focus_slot_ && slot >= 0) focus_ = Focus::Slot;
    else if (cam != focus_cam_ && cam >= 0) focus_ = Focus::Camera;
    // o que estava no Inspector saiu da seleção: mostra o que ainda está selecionado, ou a pista
    if ((focus_ == Focus::Object && obj < 0) || (focus_ == Focus::Slot && slot < 0) || (focus_ == Focus::Camera && cam < 0))
        focus_ = obj >= 0 ? Focus::Object : slot >= 0 ? Focus::Slot : cam >= 0 ? Focus::Camera : Focus::Track;
    focus_obj_ = obj;
    focus_slot_ = slot;
    focus_cam_ = cam;
}

Rect EditorUi::frame(TrackView* track, render::OrbitCamera& cam, float fps) {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();  // logo depois do NewFrame: a janela do gizmo nasce atrás dos painéis
    poll_launch();
    poll_control();
    if (track) {
        // encaixe e grudar no chão são do editor: valem em qualquer pista aberta e ficam no imgui.ini
        track->snap_move = snap_ ? snap_move_step_ : 0.0f;
        track->snap_turn = snap_ ? snap_turn_step_ : 0.0f;
        track->follow_ground = follow_ground_;
        update_focus(*track);
    }

    // a ordem importa: cada barra tira a sua faixa da janela, e o encaixe fica com o que sobra
    menu_bar(track, cam);
    const bool show = track && panels;
    if (show) toolbar(*track);
    status_bar(track, fps);
    vp_ = dock_space(show);
    if (show) {
        if (show_scene_) {
            if (ImGui::Begin(ICON_SCENE "  Cena###Cena", &show_scene_)) scene_tree(*track, cam);
            ImGui::End();
        }
        if (show_history_) {
            if (ImGui::Begin(ICON_HISTORY "  Histórico###Historico", &show_history_)) history(*track);
            ImGui::End();
        }
        if (show_inspector_) {
            if (ImGui::Begin(ICON_INSPECTOR "  Inspector###Inspector", &show_inspector_)) inspector(*track, cam);
            ImGui::End();
        }
    }
    if (show_help_) help_window();
    about_window();
    modals(track);
    watch_settings();
    return vp_;
}

// ---- Encaixe dos painéis

Rect EditorUi::dock_space(bool visible) {
    ImGuiViewportP* viewport = static_cast<ImGuiViewportP*>(ImGui::GetMainViewport());
    const ImRect work = viewport->GetBuildWorkRect();  // a janela menos as barras deste quadro
    Rect area{work.Min.x, work.Min.y, std::max(1.0f, work.GetWidth()), std::max(1.0f, work.GetHeight())};
    ImGui::SetNextWindowPos(work.Min);
    ImGui::SetNextWindowSize(work.GetSize());
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGuiWindowFlags host = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                            ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings |
                            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (!visible) host |= ImGuiWindowFlags_NoInputs;
    ImGui::Begin("##encaixe", nullptr, host);
    ImGui::PopStyleVar(3);
    const ImGuiID id = ImGui::GetID("Encaixe");
    if (visible) {
        if (reset_layout_ || layout_version_ < kLayoutVersion || !ImGui::DockBuilderGetNode(id)) {
            default_layout(id, area.w, area.h);
            if (reset_layout_) show_scene_ = show_history_ = show_inspector_ = true;
            reset_layout_ = false;
            layout_version_ = kLayoutVersion;
        }
        // o nó central fica vazio e deixa o mouse passar: é o viewport 3D
        ImGui::DockSpace(id, ImVec2(0, 0), ImGuiDockNodeFlags_PassthruCentralNode | ImGuiDockNodeFlags_NoDockingOverCentralNode);
        if (const ImGuiDockNode* central = ImGui::DockBuilderGetCentralNode(id))
            area = Rect{central->Pos.x, central->Pos.y, std::max(1.0f, central->Size.x), std::max(1.0f, central->Size.y)};
    } else {
        // painéis escondidos (F10): os nós ficam guardados e o 3D pega a área toda
        ImGui::DockSpace(id, ImVec2(0, 0), ImGuiDockNodeFlags_KeepAliveOnly);
    }
    ImGui::End();
    return area;
}

void EditorUi::default_layout(ImGuiID dock, float w, float h) {
    ImGui::DockBuilderRemoveNode(dock);
    ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dock, ImVec2(w, h));
    // Cena e Histórico à esquerda (o Histórico embaixo), Inspector à direita; o resto é o 3D
    const float left = std::clamp(300.0f * scale_ / w, 0.12f, 0.40f);
    const float right = std::clamp(330.0f * scale_ / (w * (1.0f - left)), 0.12f, 0.45f);
    ImGuiID center = dock;
    ImGuiID left_top = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, left, nullptr, &center);
    const ImGuiID right_id = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, right, nullptr, &center);
    const ImGuiID left_bottom = ImGui::DockBuilderSplitNode(left_top, ImGuiDir_Down, 0.30f, nullptr, &left_top);
    ImGui::DockBuilderDockWindow("###Cena", left_top);
    ImGui::DockBuilderDockWindow("###Historico", left_bottom);
    ImGui::DockBuilderDockWindow("###Inspector", right_id);
    ImGui::DockBuilderFinish(dock);
    ImGui::MarkIniSettingsDirty();
}

// ---- Partes pequenas

bool EditorUi::tool_button(const char* id, const char* icon, bool on, bool enabled, unsigned tint) {
    const float size = std::round(28.0f * scale_);
    ImGui::PushID(id);
    ImGui::BeginDisabled(!enabled);
    ImGui::PushStyleColor(ImGuiCol_Button, on ? kAccentSoft : ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, on ? rgb(50, 98, 172) : kBg2);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, on ? kAccent : kBg3);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::Button("##b", ImVec2(size, size));
    ImGui::PopStyleColor(3);
    if (icon && *icon) {
        ImFont* font = font_tool_ ? font_tool_ : ImGui::GetFont();
        const float fs = font->FontSize;
        const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, icon);
        const ImU32 col = tint ? ImGui::GetColorU32(static_cast<ImU32>(tint)) : u32(on ? ImVec4(1, 1, 1, 1) : kText);
        ImGui::GetWindowDrawList()->AddText(font, fs, ImVec2(std::round(p.x + (size - ts.x) * 0.5f), std::round(p.y + (size - fs) * 0.5f)),
                                            col, icon);
    }
    ImGui::EndDisabled();
    ImGui::PopID();
    return pressed;
}

void EditorUi::tip(const char* title, const char* keys, const char* body) const {
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) || !ImGui::BeginTooltip()) return;
    ImGui::PushFont(font_head_);
    ImGui::TextUnformatted(title);
    ImGui::PopFont();
    if (keys && *keys) {
        ImGui::SameLine(0, std::round(12.0f * scale_));
        ImGui::TextDisabled("%s", keys);
    }
    if (body && *body) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, rgb(190, 194, 200));
        ImGui::TextUnformatted(body);
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();
    }
    ImGui::EndTooltip();
}

void EditorUi::section(const char* label) {
    ImGui::Dummy(ImVec2(0, std::round(2.0f * scale_)));
    ImGui::PushFont(font_head_);
    ImGui::SeparatorText(label);
    ImGui::PopFont();
}

bool EditorUi::layer_row(const char* id, const char* icon, const char* name, const std::string& count, bool* shown,
                         const char* keys, int flags) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    const bool off = shown && !*shown;
    ImGui::PushStyleColor(ImGuiCol_Text, off ? kTextDim : kText);
    const bool open = ImGui::TreeNodeEx(id,
                                        static_cast<ImGuiTreeNodeFlags>(flags) | ImGuiTreeNodeFlags_SpanAllColumns |
                                            ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_FramePadding |
                                            ImGuiTreeNodeFlags_NoTreePushOnOpen,
                                        "%s  %s", icon, name);
    ImGui::PopStyleColor();
    ImGui::TableSetColumnIndex(1);
    if (!count.empty()) right_text(count, kTextDim, true);
    ImGui::TableSetColumnIndex(2);
    if (shown) {
        // olho: mostra ou esconde a camada sem abrir nem fechar o nó
        ImGui::PushID(id);
        const float h = ImGui::GetFrameHeight();
        const float w = std::max(h, ImGui::GetContentRegionAvail().x);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("##olho", ImVec2(w, h))) *shown = !*shown;
        const bool hovered = ImGui::IsItemHovered();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (hovered) dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), u32(kBg3), ImGui::GetStyle().FrameRounding);
        const char* glyph = *shown ? ICON_SHOW : ICON_HIDE;
        const ImVec2 ts = ImGui::CalcTextSize(glyph);
        const ImVec4 color = *shown ? (hovered ? kText : kTextDim) : with_alpha(kTextDim, hovered ? 0.9f : 0.5f);
        dl->AddText(ImVec2(std::round(p.x + (w - ts.x) * 0.5f), std::round(p.y + (h - ImGui::GetFontSize()) * 0.5f)), u32(color), glyph);
        const std::string title = std::string(*shown ? "Esconder " : "Mostrar ") + name;
        tip(title.c_str(), keys);
        ImGui::PopID();
    }
    if (open && !(flags & ImGuiTreeNodeFlags_Leaf)) ImGui::TreePush(id);
    return open;
}

// ---- Menu

void EditorUi::menu_bar(TrackView* track, render::OrbitCamera& cam) {
    const float s = scale_;
    // linhas mais altas no menu e nas listas que ele abre
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(std::round(8.0f * s), std::round(6.0f * s)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(std::round(6.0f * s), std::round(6.0f * s)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(std::round(8.0f * s), std::round(8.0f * s)));
    ImGui::PushStyleColor(ImGuiCol_Header, kBg3);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, kAccentSoft);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, kAccent);
    if (!ImGui::BeginMainMenuBar()) {
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(3);
        return;
    }
    auto toggle = [](const char* label, const char* icon, const char* keys, bool& on, bool enabled = true) {
        if (ImGui::MenuItemEx(label, icon, keys, on, enabled)) on = !on;
    };

    if (ImGui::BeginMenu("Arquivo")) {
        if (ImGui::BeginMenuEx("Abrir pista", ICON_OPEN)) {
            if (!tracks_scanned_) scan_tracks();
            if (tracks_.empty()) ImGui::TextDisabled("nada em build/uiview/tracks nem em examples/tracks");
            for (const std::string& dir : tracks_) {
                std::error_code ec;
                const bool current = track && std::filesystem::equivalent(dir, track->track().dir, ec);
                std::string label = std::filesystem::path(dir).filename().string();
                if (dir.rfind("examples/", 0) == 0) label += " (exemplo)";
                if (ImGui::MenuItem((label + "###" + dir).c_str(), nullptr, current, !current)) ask_open(track, dir);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Procurar de novo")) scan_tracks();
            ImGui::EndMenu();
        } else {
            tracks_scanned_ = false;  // relê ao abrir o submenu
        }
        if (ImGui::MenuItemEx("Gravar edits.json", ICON_SAVE, "Ctrl+S", false, track != nullptr)) track->save();
        if (ImGui::MenuItemEx("Recuperar autosave", ICON_HISTORY, nullptr, false, track && track->has_autosave())) track->recover_autosave();
        if (track) tip("Recuperar autosave", nullptr, "Volta às edições do último autosave (a cada 60 s); fica não gravado até o Ctrl+S");
        ImGui::Separator();
        if (ImGui::MenuItemEx("Sair", ICON_QUIT, "Ctrl+Q") && ask_quit(track)) quit_request = true;
        ImGui::EndMenu();
    }
    if (track && ImGui::BeginMenu("Editar")) {
        const int sel = track->selected();
        const bool alive = sel >= 0 && !track->instances().hidden[static_cast<std::size_t>(sel)];
        if (ImGui::MenuItemEx("Desfazer", ICON_UNDO, "Ctrl+Z", false, track->history().pos() > 0)) track->undo();
        if (ImGui::MenuItemEx("Refazer", ICON_REDO, "Ctrl+Y", false, track->history().pos() < track->history().size())) track->redo();
        ImGui::Separator();
        if (ImGui::MenuItemEx("Duplicar", ICON_DUPLICATE, "Ctrl+D", false, alive)) track->duplicate_selected();
        if (ImGui::MenuItemEx("Apagar", ICON_DELETE, "Delete", false, alive)) track->delete_selected();
        if (ImGui::MenuItemEx("Restaurar do arquivo", ICON_RESTORE, "R", false, sel >= 0)) track->restore_selected();
        ImGui::Separator();
        if (ImGui::MenuItemEx("Pôr no chão", ICON_SETTLE, "T", false, alive)) track->settle_selected();
        if (ImGui::MenuItemEx("Alinhar ao terreno", ICON_ALIGN, "Shift+T", false, alive)) track->align_selected();
        if (ImGui::MenuItemEx("Alinhar todos deste tipo", nullptr, nullptr, false, alive)) track->align_type();
        if (ImGui::MenuItemEx("Girar +15°", ICON_ROTATE, "E", false, alive)) track->turn_selected(15.0f);
        if (ImGui::MenuItemEx("Girar −15°", nullptr, "Q", false, alive)) track->turn_selected(-15.0f);
        ImGui::Separator();
        if (ImGui::MenuItemEx("Enquadrar", ICON_FRAME, "F")) {
            if (sel >= 0) track->frame_selected(cam);
            else track->frame_route(cam);
        }
        if (ImGui::MenuItemEx("Tirar seleção", ICON_CLOSE, "Esc", false, sel >= 0)) track->deselect();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Exibir")) {
        if (track) {
            toggle("Terreno", ICON_TERRAIN, "F1", track->show_terrain());
            toggle("Objetos", ICON_OBJECTS, "F2", track->layers().obj);
            toggle("Árvores", ICON_TREES, "F3", track->layers().tree);
            toggle("Terreno distante", ICON_FAR_TERRAIN, "F4", track->layers().dist);
            toggle("Portões", ICON_GATES, "G", track->show_gates());
            toggle("Linha da IA", ICON_AI_LINE, "I", track->show_ai());
            toggle("Câmeras do replay", ICON_CAMERAS, "C", track->show_replay());
            if (ImGui::MenuItemEx("Ver pela próxima câmera", nullptr, "Shift+C", false, !track->route().replay.cameras.empty())) {
                const int n = static_cast<int>(track->route().replay.cameras.size());
                track->look_through((track->replay_selected() + 1) % n, cam);
            }
            toggle("Largada (onde o carro nasce)", ICON_GRIDS, "L", track->show_grids());
            if (ImGui::MenuItemEx("Ver da próxima vaga", nullptr, "Shift+L", false, track->route().slot_count() > 0)) {
                const Route& r = track->route();
                track->look_from_slot(r.slot_at((r.slot_index(track->slot_selected()) + 1) % r.slot_count()), cam);
            }
            ImGui::Separator();
            toggle("Carro do jogo ao vivo", ICON_CAR, nullptr, track->show_live());
            using LV = TrackView::LiveView;
            static const char* const views[] = {"Vista livre", "Seguir o carro do jogo", "Ver pela câmera do jogo"};
            for (int k = 0; k < 3; ++k)
                if (ImGui::MenuItemEx(views[k], nullptr, k == 0 ? "V" : nullptr, track->live_view() == static_cast<LV>(k)))
                    track->set_live_view(static_cast<LV>(k));
            ImGui::Separator();
        }
        toggle("Cena", ICON_SCENE, nullptr, show_scene_, track != nullptr);
        toggle("Histórico", ICON_HISTORY, nullptr, show_history_, track != nullptr);
        toggle("Inspector", ICON_INSPECTOR, nullptr, show_inspector_, track != nullptr);
        toggle("Barra e painéis", nullptr, "F10", panels);
        if (ImGui::MenuItemEx("Restaurar layout padrão", ICON_LAYOUT, nullptr, false, track != nullptr)) {
            reset_layout_ = true;
            panels = true;
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Jogo")) {
        const bool busy = launch_.child.running(), game = game_.running();
        const bool paused = track && track->live_connected() && track->live_sample().paused;
        if (ImGui::MenuItemEx(busy ? "Ver o teste em andamento…" : "Testar no jogo…", ICON_PLAY, "F5", false, track != nullptr))
            open_launch(track);
        if (ImGui::MenuItemEx(paused ? "Continuar a especial" : "Pausar a especial", ICON_PAUSE, "F6", false,
                              game && !control_.child.running()))
            toggle_pause(track);
        if (ImGui::MenuItemEx(busy ? "Parar o teste e fechar o jogo" : "Fechar o jogo", ICON_STOP, "Shift+F5", false, busy || game))
            stop_game(track);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Ajuda")) {
        toggle("Atalhos", ICON_KEYBOARD, "F11", show_help_);
        ImGui::Separator();
        if (ImGui::MenuItemEx("Sobre o Editor de Pistas", ICON_INFO)) about_request_ = true;
        ImGui::EndMenu();
    }

    // à direita: o jogo (útil com a barra escondida)
    {
        std::string state;
        ImVec4 color = kTextDim;
        if (launch_.child.running()) {
            state = fmt("%s  testando no jogo · %d%%", ICON_BUSY, static_cast<int>(launch_.progress.fraction * 100.0f + 0.5f));
            color = kAccentText;
        } else if (game_.running()) {
            state = std::string(ICON_GAME "  jogo aberto");
        }
        if (!state.empty()) same_line_right(state.c_str(), color);
    }
    ImGui::EndMainMenuBar();
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(3);
}

// ---- Barra de ferramentas

void EditorUi::toolbar(TrackView& track) {
    const float s = scale_;
    const float btn = std::round(28.0f * s), pad = std::round(5.0f * s), side = std::round(8.0f * s);
    const float tight = std::round(2.0f * s), gap = std::round(7.0f * s);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(side, pad));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, kBg0);
    const bool open = ImGui::BeginViewportSideBar("##ferramentas", ImGui::GetMainViewport(), ImGuiDir_Up, btn + 2.0f * pad,
                                                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                                                      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                                      ImGuiWindowFlags_NoNav);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    if (!open) {
        ImGui::End();
        return;
    }
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    ImDrawList* dl = win->DrawList;
    dl->PushClipRect(win->Pos, win->Pos + win->Size, false);
    dl->AddLine(ImVec2(win->Pos.x, win->Pos.y + win->Size.y - 0.5f), ImVec2(win->Pos.x + win->Size.x, win->Pos.y + win->Size.y - 0.5f), raw(kLine));
    dl->PopClipRect();

    // tudo numa linha: cada item volta à altura da linha (os campos, mais baixos, ficam centrados nela)
    const float line_y = ImGui::GetCursorPosY();
    const float frame_h = ImGui::GetFrameHeight();
    auto next = [&](float spacing) {
        ImGui::SameLine(0, spacing);
        ImGui::SetCursorPosY(line_y);
    };
    auto divider = [&] {
        next(gap);
        const ImVec2 q = ImGui::GetCursorScreenPos();
        dl->AddLine(ImVec2(q.x, q.y + std::round(6.0f * s)), ImVec2(q.x, q.y + btn - std::round(6.0f * s)), raw(rgb(58, 61, 67)));
        ImGui::Dummy(ImVec2(1, btn));
    };
    auto frame_colors = [] {
        ImGui::PushStyleColor(ImGuiCol_FrameBg, kBg2);
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, kBg3);
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, kBg4);
    };
    const ImVec2 list_spacing(ImGui::GetStyle().ItemSpacing.x, std::round(6.0f * s));  // linhas das listas que abrem

    // ferramentas
    static const char* const keys[] = {"1", "2", "3"};
    static const char* const tips[] = {"Clique num objeto para selecionar. Arrastar orbita a câmera",
                                       "Arraste o objeto no chão; com Shift, sobe e desce. No gizmo, as setas prendem num eixo e os quadrados num plano",
                                       "Arraste para os lados para girar em Y, ou pelo anel do gizmo"};
    for (int k = 0; k < 3; ++k) {
        if (k) next(tight);
        if (tool_button(kToolNames[k], kToolIcons[k], static_cast<int>(track.tool()) == k)) track.set_tool(static_cast<TrackView::Tool>(k));
        tip(kToolNames[k], keys[k], tips[k]);
    }
    next(tight);
    if (tool_button("eixos", gizmo_local_ ? ICON_AXES_LOCAL : ICON_AXES_WORLD, false)) gizmo_local_ = !gizmo_local_;
    tip(gizmo_local_ ? "Gizmo nos eixos do objeto" : "Gizmo nos eixos da pista", "X",
        gizmo_local_ ? "As setas giram junto com o objeto (andar para a frente ou para o lado dele). Clique para voltar aos da pista"
                     : "As setas apontam para X, Y e Z da pista. Clique para as setas girarem junto com o objeto");

    // histórico e gravar
    divider();
    const edit::History& h = track.history();
    next(gap);
    if (tool_button("desfazer", ICON_UNDO, false, h.pos() > 0)) track.undo();
    tip("Desfazer", "Ctrl+Z", h.pos() > 0 ? h.entries()[h.pos() - 1].label.c_str() : "Nada a desfazer");
    next(tight);
    if (tool_button("refazer", ICON_REDO, false, h.pos() < h.size())) track.redo();
    tip("Refazer", "Ctrl+Y", h.pos() < h.size() ? h.entries()[h.pos()].label.c_str() : "Nada a refazer");
    next(tight);
    const ImVec2 save_at = ImGui::GetCursorScreenPos();
    const bool dirty = track.unsaved();
    if (tool_button("gravar", ICON_SAVE)) track.save();
    if (dirty) dl->AddCircleFilled(ImVec2(save_at.x + btn - std::round(7.0f * s), save_at.y + std::round(7.0f * s)), 3.0f * s, raw(kYellow));
    tip(dirty ? "Gravar (há edições não gravadas)" : "Gravar", "Ctrl+S", ("Grava " + near_path(track.out_path())).c_str());

    // encaixe
    divider();
    next(gap);
    if (tool_button("encaixe", ICON_SNAP, snap_)) snap_ = !snap_;
    tip(snap_ ? "Encaixe ligado" : "Encaixe desligado", nullptr,
        "Mover e girar andam nos passos ao lado. Escolher um passo liga o encaixe");
    auto step_combo = [&](const char* id, float& step, const float* values, int n, const char* unit, const char* title,
                          const char* body) {
        float widest = 0.0f;
        for (int k = 0; k < n; ++k) widest = std::max(widest, ImGui::CalcTextSize(step_text(values[k], unit).c_str()).x);
        next(std::round(4.0f * s));
        ImGui::SetCursorPosY(line_y + std::round((btn - frame_h) * 0.5f));
        frame_colors();
        ImGui::PushStyleColor(ImGuiCol_Text, snap_ ? kText : kTextDim);  // desligado: o passo fica guardado, em cinza
        ImGui::SetNextItemWidth(widest + 2.0f * ImGui::GetStyle().FramePadding.x + frame_h);
        const bool open_list = ImGui::BeginCombo(id, step_text(step, unit).c_str(), ImGuiComboFlags_HeightLarge);
        ImGui::PopStyleColor(4);
        if (open_list) {
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, list_spacing);
            for (int k = 0; k < n; ++k) {
                if (ImGui::Selectable(step_text(values[k], unit).c_str(), snap_ && step == values[k])) {
                    step = values[k];
                    snap_ = true;
                }
            }
            ImGui::PopStyleVar();
            ImGui::EndCombo();
        }
        tip(title, nullptr, body);
    };
    step_combo("##passo_mover", snap_move_step_, kMoveSteps, static_cast<int>(std::size(kMoveSteps)), " m", "Passo do mover",
               "No arraste no chão, com Shift e pelo gizmo (na grade da pista; com os eixos do objeto, em passos de onde começou)");
    step_combo("##passo_girar", snap_turn_step_, kTurnSteps, static_cast<int>(std::size(kTurnSteps)), "°", "Passo do girar",
               "No arraste e pelo anel do gizmo, em passos de onde começou");
    next(std::round(4.0f * s));
    if (tool_button("chao", ICON_FOLLOW_GROUND, follow_ground_)) follow_ground_ = !follow_ground_;
    tip("Grudar no chão", nullptr, "Ao arrastar no chão ou pelo gizmo (sem ser a seta verde), a altura do objeto acompanha o terreno (mantém a folga que ele tinha)");

    // jogo, no meio da barra
    const float play_w = 3.0f * btn + 2.0f * tight;
    next(gap * 2.0f);
    const float mid = std::round((ImGui::GetWindowWidth() - play_w) * 0.5f);
    if (ImGui::GetCursorPosX() < mid) ImGui::SetCursorPosX(mid);
    play_group(track);

    // rota e exibição, na direita
    const std::string route_text = std::string(ICON_ROUTE "  ") + track.route().name;
    const float route_w = ImGui::CalcTextSize(route_text.c_str()).x + 2.0f * ImGui::GetStyle().FramePadding.x + frame_h;
    next(gap * 2.0f);
    const float right_x = ImGui::GetWindowWidth() - side - (route_w + gap + btn);
    if (ImGui::GetCursorPosX() < right_x) ImGui::SetCursorPosX(right_x);
    ImGui::SetCursorPosY(line_y + std::round((btn - frame_h) * 0.5f));
    frame_colors();
    ImGui::SetNextItemWidth(route_w);
    const bool routes_open = ImGui::BeginCombo("##rota", route_text.c_str());
    ImGui::PopStyleColor(3);
    if (routes_open) {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, list_spacing);
        for (std::size_t k = 0; k < track.track().routes.size(); ++k)
            if (ImGui::Selectable(track.track().routes[k].name.c_str(), k == track.route_index())) track.open_route(k);
        ImGui::PopStyleVar();
        ImGui::EndCombo();
    }
    tip("Rota", "Tab / Shift+Tab", "Cada rota guarda as próprias edições e o próprio histórico");
    next(gap);
    const ImVec2 at = ImGui::GetCursorScreenPos();
    // o pedido do --ui espera os painéis aparecerem (a janela que aparece pega o foco e fecha os popups)
    const bool requested = display_request_ && ImGui::GetFrameCount() > 3;
    if (tool_button("exibicao", ICON_SHOW, display_open_) || requested) {
        ImGui::OpenPopup("##exibicao");
        display_request_ = false;
    }
    tip("Exibição", nullptr, "Distância de desenho, camadas e o jogo ao vivo");
    ImGui::SetNextWindowPos(ImVec2(at.x + btn, at.y + btn + std::round(6.0f * s)), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    display_popup(track);
    ImGui::End();
}

void EditorUi::play_group(TrackView& track) {
    const bool busy = launch_.child.running();
    const bool game = game_.running();
    const bool paused = track.live_connected() && track.live_sample().paused;
    const bool controlling = control_.child.running();
    const float s = scale_, size = std::round(28.0f * s), tight = std::round(2.0f * s);
    const float line_y = ImGui::GetCursorPosY();

    // Jogar: enquanto o teste roda, um arco gira no lugar do triângulo e a linha embaixo enche
    const ImVec2 p = ImGui::GetCursorScreenPos();
    if (tool_button("jogar", busy ? "" : ICON_PLAY, false, true, busy ? 0u : raw(kPlay))) open_launch(&track);
    if (busy) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 c(p.x + size * 0.5f, p.y + size * 0.5f - std::round(1.0f * s));
        const float t = static_cast<float>(ImGui::GetTime()) * 5.0f;
        dl->PathArcTo(c, size * 0.24f, t, t + kPi * 1.5f, 24);
        dl->PathStroke(raw(kPlay), ImDrawFlags_None, std::max(1.5f, 2.0f * s));
        const float x0 = p.x + std::round(5.0f * s), x1 = p.x + size - std::round(5.0f * s);
        const float f = std::clamp(launch_.progress.fraction, 0.0f, 1.0f);
        const float y0 = p.y + size - std::round(4.0f * s), y1 = y0 + std::max(1.0f, std::round(2.0f * s));
        dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), raw(kBg3), 1.0f);
        dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + (x1 - x0) * f, y1), raw(kPlay), 1.0f);
        const Progress& pr = launch_.progress;
        const std::string body = (pr.label.empty() ? std::string("Começando") : pr.label) +
                                 fmt(" (%d%%). Clique para ver o andamento", static_cast<int>(pr.fraction * 100.0f + 0.5f));
        tip("Testando no jogo", "F5", body.c_str());
    } else {
        tip("Testar no jogo", "F5", "Porta a pista para a overlay e abre o jogo nela");
    }

    ImGui::SameLine(0, tight);
    ImGui::SetCursorPosY(line_y);
    if (tool_button("pausar", ICON_PAUSE, paused, game && !controlling)) toggle_pause(&track);
    tip(paused ? "Continuar" : "Pausar", "F6",
        !game ? "O jogo não está aberto"
        : controlling ? "Esperando o comando anterior terminar"
        : paused ? "Continua a especial aberta no jogo"
                 : "Pausa a especial aberta no jogo");

    ImGui::SameLine(0, tight);
    ImGui::SetCursorPosY(line_y);
    if (tool_button("parar", ICON_STOP, false, busy || game, raw(kStop))) stop_game(&track);
    tip("Parar", "Shift+F5", busy ? "Cancela o teste em andamento e fecha o jogo" : game ? "Fecha o jogo" : "Nada rodando");
}

void EditorUi::display_popup(TrackView& track) {
    const float s = scale_;
    ImGui::SetNextWindowSize(ImVec2(std::round(310.0f * s), 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(std::round(12.0f * s), std::round(10.0f * s)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(std::round(8.0f * s), std::round(6.0f * s)));
    display_open_ = ImGui::BeginPopup("##exibicao");
    if (display_open_) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
        section("Distância de desenho");
        ImGui::TextUnformatted("Objetos e árvores");
        same_line_right("[  ]", kTextDim);
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::SliderFloat("##dist", &track.draw_dist(), 100.0f, 4000.0f, "%.0f m", ImGuiSliderFlags_Logarithmic);
        tip("Raio de desenho", "[ e ]", "Objetos e árvores mais longe que isto não são desenhados");
        ImGui::TextUnformatted("Terreno");
        ImGui::SetNextItemWidth(-FLT_MIN);
        float km = track.terrain_dist() / 1000.0f;
        if (ImGui::SliderFloat("##terreno", &km, 0.0f, 20.0f, km <= 0.0f ? "sem limite" : "%.1f km"))
            track.terrain_dist() = km < 0.25f ? 0.0f : km * 1000.0f;
        tip("Raio do terreno", nullptr, "A partir da câmera. Em pistas grandes, um raio menor desenha menos (o horizonte some)");

        // os itens ligam e desligam sem fechar a janela
        ImGui::PushItemFlag(ImGuiItemFlags_AutoClosePopups, false);
        auto layer = [](const char* label, const char* icon, const char* keys, bool& on) {
            if (ImGui::MenuItemEx(label, icon, keys, on)) on = !on;
        };
        section("Camadas");
        layer("Terreno", ICON_TERRAIN, "F1", track.show_terrain());
        layer("Objetos", ICON_OBJECTS, "F2", track.layers().obj);
        layer("Árvores", ICON_TREES, "F3", track.layers().tree);
        layer("Terreno distante", ICON_FAR_TERRAIN, "F4", track.layers().dist);
        layer("Portões", ICON_GATES, "G", track.show_gates());
        layer("Linha da IA", ICON_AI_LINE, "I", track.show_ai());
        layer("Câmeras do replay", ICON_CAMERAS, "C", track.show_replay());
        layer("Largada", ICON_GRIDS, "L", track.show_grids());
        section("Jogo ao vivo");
        layer("Carro do jogo", ICON_CAR, nullptr, track.show_live());
        using LV = TrackView::LiveView;
        static const char* const views[] = {"Vista livre", "Seguir o carro do jogo", "Ver pela câmera do jogo"};
        for (int k = 0; k < 3; ++k)
            if (ImGui::MenuItemEx(views[k], nullptr, k == 0 ? "V" : nullptr, track.live_view() == static_cast<LV>(k)))
                track.set_live_view(static_cast<LV>(k));
        ImGui::PopItemFlag();
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(2);
}

// ---- Barra de status

void EditorUi::status_bar(TrackView* track, float fps) {
    const float s = scale_;
    const float font = ImGui::GetFontSize();
    const float h = std::round(font + 9.0f * s);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(std::round(10.0f * s), std::round((h - font) * 0.5f)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, kBg0);
    const bool open = ImGui::BeginViewportSideBar("##status", ImGui::GetMainViewport(), ImGuiDir_Down, h,
                                                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                                                      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                                      ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    if (!open) {
        ImGui::End();
        return;
    }
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    ImDrawList* dl = win->DrawList;
    dl->PushClipRect(win->Pos, win->Pos + win->Size, false);
    dl->AddLine(ImVec2(win->Pos.x, win->Pos.y + 0.5f), ImVec2(win->Pos.x + win->Size.x, win->Pos.y + 0.5f), raw(kLine));
    dl->PopClipRect();
    const float sp = std::round(16.0f * s);

    // direita primeiro: a mensagem do meio é cortada antes dela
    std::string right;
    if (track)
        right = fmt("%s   ·   inst %zu/%u   ·   %.0f m   ·   ", track->live_status().c_str(), track->objects().visible(),
                    track->instances().n, static_cast<double>(track->draw_dist()));
    right += fps > 0.0f ? fmt("%.0f fps", static_cast<double>(fps)) : std::string("— fps");  // a 1ª medida sai aos 0,5 s
    const float dot_w = track ? std::round(font * 0.4f) + std::round(font * 0.32f) : 0.0f;
    const float right_x = win->Pos.x + win->Size.x - std::round(10.0f * s) - ImGui::CalcTextSize(right.c_str()).x - dot_w;

    if (track) {
        const int t = static_cast<int>(track->tool());
        ImGui::TextDisabled("%s  %s", kToolIcons[t], kToolNames[t]);
        ImGui::SameLine(0, sp);
        if (track->unsaved()) {
            dot(kYellow);
            ImGui::TextColored(kYellow, "não gravado");
        } else {
            dot(kTextDim);
            ImGui::TextDisabled("gravado");
        }
        ImGui::SameLine(0, sp);
    }
    if (const int glerr = gl::warned_errors()) {
        ImGui::TextColored(kRed, "%s  %d erro(s) de GL (veja o terminal)", ICON_ALERT, glerr);
        ImGui::SameLine(0, sp);
    }
    const ImVec2 msg_at = ImGui::GetCursorScreenPos();
    ImGui::PushClipRect(msg_at, ImVec2(std::max(msg_at.x, right_x - sp), win->Pos.y + win->Size.y), true);
    if (track) {
        // a mensagem mais recente: a dos painéis (abrir pista, jogo) ou a da pista (gravar, rota)
        const bool mine = !message_.empty() && message_age() < track->status_age();
        const std::string& msg = mine ? message_ : track->status();
        const bool bad = bad_message(msg);
        // a mensagem some depois de um tempo (erros ficam mais) para não contradizer o estado atual
        const double age = mine ? message_age() : track->status_age();
        const bool fresh = !msg.empty() && age < (bad ? 30.0 : 8.0);
        if (fresh) {
            if (bad) ImGui::TextColored(kRed, "%s  %s", ICON_ALERT, msg.c_str());
            else ImGui::TextUnformatted(msg.c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", msg.c_str());
        } else if (launch_.child.running()) {
            const Progress& p = launch_.progress;
            if (p.steps > 0)
                ImGui::TextColored(kAccentText, "Testando no jogo · etapa %d de %d: %s · %d%%", p.step, p.steps, p.label.c_str(),
                                   static_cast<int>(p.fraction * 100.0f + 0.5f));
            else ImGui::TextColored(kAccentText, "Testando no jogo · começando");
        } else {
            ImGui::TextDisabled("%s", sel_hint(*track).c_str());
        }
    } else {
        ImGui::TextUnformatted(message_.empty() ? "Cena de teste: abra uma pista em Arquivo > Abrir pista" : message_.c_str());
    }
    ImGui::PopClipRect();

    ImGui::SameLine();
    ImGui::SetCursorScreenPos(ImVec2(std::max(ImGui::GetCursorScreenPos().x + sp, right_x), ImGui::GetCursorScreenPos().y));
    if (track) dot(track->live_connected() ? kGreen : kTextDim);
    ImGui::TextDisabled("%s", right.c_str());
    ImGui::End();
}

std::string EditorUi::sel_hint(const TrackView& track) const {
    switch (track.tool()) {
    case TrackView::Tool::Move: return "Mover: arraste o objeto no chão (Shift sobe e desce), uma seta ou um quadrado do gizmo; X troca os eixos";
    case TrackView::Tool::Rotate: return "Girar: arraste para os lados ou pelo anel do gizmo; Q/E giram 15°";
    default: return "Clique num objeto para selecionar; arraste para orbitar, botão direito para pan, roda para zoom";
    }
}

// ---- Cena

void EditorUi::scene_tree(TrackView& track, render::OrbitCamera& cam) {
    const Instances& inst = track.instances();
    const auto& types = track.objects().types();
    const int sel = track.selected();
    if (sel != last_sel_) {
        scroll_to_sel_ = sel >= 0;
        last_sel_ = sel;
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##filtro", ICON_SEARCH "  Filtrar tipos pelo nome", filter_, sizeof filter_);

    // nome | contagem ou estado | olho; a tabela rola sozinha e ocupa o resto do painel
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(std::round(4.0f * scale_), std::round(1.0f * scale_)));
    const bool table = ImGui::BeginTable("##cena", 3, ImGuiTableFlags_ScrollY | ImGuiTableFlags_NoSavedSettings);
    ImGui::PopStyleVar();
    if (!table) return;
    ImGui::TableSetupColumn("nome", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("info", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("desfeita").x);
    ImGui::TableSetupColumn("olho", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());

    const Route& route = track.route();
    layer_row("terreno", ICON_TERRAIN, "Terreno", std::to_string(track.terrain().meshes()), &track.show_terrain(), "F1",
              ImGuiTreeNodeFlags_Leaf);
    layer_row("portoes", ICON_GATES, "Portões", std::to_string(route.gates.size()), &track.show_gates(), "G", ImGuiTreeNodeFlags_Leaf);
    layer_row("ia", ICON_AI_LINE, "Linha da IA", std::to_string(route.ai.size()), &track.show_ai(), "I", ImGuiTreeNodeFlags_Leaf);
    grids_tree(track, cam);
    replay_tree(track, cam);
    layer_row("carro", ICON_CAR, "Carro do jogo", track.live_connected() ? "ao vivo" : "", &track.show_live(), nullptr,
              ImGuiTreeNodeFlags_Leaf);

    struct LayerRow {
        const char* id;
        const char* label;
        const char* keys;
        render::Layer layer;
        bool* on;
    };
    const LayerRow rows[] = {{"objetos", "Objetos", "F2", render::Layer::Obj, &track.layers().obj},
                             {"arvores", "Árvores", "F3", render::Layer::Tree, &track.layers().tree},
                             {"distante", "Terreno distante", "F4", render::Layer::Dist, &track.layers().dist}};
    for (const LayerRow& row : rows) {
        std::size_t n_inst = 0;
        for (const auto& ty : types)
            if (ty.layer == row.layer) n_inst += ty.group.size();
        const bool sel_here = sel >= 0 && types[inst.type[static_cast<std::size_t>(sel)]].layer == row.layer;
        if (sel_here && scroll_to_sel_) ImGui::SetNextItemOpen(true);
        if (!layer_row(row.id, layer_icon(row.layer), row.label, std::to_string(n_inst), row.on, row.keys,
                       row.layer == render::Layer::Obj ? ImGuiTreeNodeFlags_DefaultOpen : 0))
            continue;
        for (std::size_t t = 0; t < types.size(); ++t) {
            const auto& ty = types[t];
            if (ty.layer != row.layer || ty.group.empty() || !contains_ci(ty.name, filter_)) continue;
            const bool sel_type = sel >= 0 && inst.type[static_cast<std::size_t>(sel)] == t;
            if (sel_type && scroll_to_sel_) ImGui::SetNextItemOpen(true);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::PushStyleColor(ImGuiCol_Text, sel_type ? kAccentText : ty.empty ? kTextDim : kText);
            const bool open = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<std::intptr_t>(t)), ImGuiTreeNodeFlags_SpanAllColumns,
                                                "%s", short_name(ty.name).c_str());
            ImGui::PopStyleColor();
            if (ty.empty) tip(short_name(ty.name).c_str(), nullptr, "Sem malha: marcador, não desenha");
            ImGui::TableSetColumnIndex(1);
            right_text(std::to_string(ty.group.size()), kTextDim);
            if (!open) continue;
            std::size_t sel_pos = ty.group.size();
            if (sel_type && scroll_to_sel_)
                sel_pos = static_cast<std::size_t>(
                    std::find(ty.group.begin(), ty.group.end(), static_cast<std::uint32_t>(sel)) - ty.group.begin());
            ImGuiListClipper clip;
            clip.Begin(static_cast<int>(ty.group.size()));
            if (sel_pos < ty.group.size()) clip.IncludeItemByIndex(static_cast<int>(sel_pos));
            while (clip.Step()) {
                for (int r = clip.DisplayStart; r < clip.DisplayEnd; ++r) {
                    const std::uint32_t i = ty.group[static_cast<std::size_t>(r)];
                    const bool hidden = inst.hidden[i] != 0;
                    const bool added = inst.idnum[i] >= kAdded;
                    const bool edited = !hidden && edit::changed(inst, i);
                    const char* state = hidden ? (added ? "desfeita" : "apagada") : added ? "nova" : edited ? "editada" : nullptr;
                    const ImVec4 color = hidden ? kTextDim : (edited || added) ? kYellow : kText;
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::PushID(static_cast<int>(i));
                    ImGui::PushStyleColor(ImGuiCol_Text, color);
                    if (ImGui::Selectable(instance_label(track, i).c_str(), static_cast<int>(i) == sel,
                                          ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                        track.select(static_cast<int>(i));
                        last_sel_ = track.selected();
                        focus_ = Focus::Object;
                        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) track.frame_selected(cam);
                    }
                    ImGui::PopStyleColor();
                    ImGui::PopID();
                    if (static_cast<std::size_t>(r) == sel_pos && scroll_to_sel_) {
                        ImGui::SetScrollHereY(0.4f);
                        scroll_to_sel_ = false;
                    }
                    if (state) {
                        ImGui::TableSetColumnIndex(1);
                        right_text(state, color);
                    }
                }
            }
            ImGui::TreePop();
        }
        ImGui::TreePop();
    }
    ImGui::EndTable();
}

void EditorUi::grids_tree(TrackView& track, render::OrbitCamera& cam) {
    const Route& route = track.route();
    if (route.grids.empty()) return;
    const int flat = route.slot_index(track.slot_selected());
    if (flat != last_slot_sel_) {  // Shift+L ou --look abrem a lista na vaga nova
        last_slot_sel_ = flat;
        if (flat >= 0) ImGui::SetNextItemOpen(true);
    }
    if (!layer_row("largada", ICON_GRIDS, "Largada", std::to_string(route.slot_count()), &track.show_grids(), "L")) return;
    const SlotRef sel = track.slot_selected();
    for (std::size_t g = 0; g < route.grids.size(); ++g) {
        const Grid& grid = route.grids[g];
        const bool here = sel && sel.grid == static_cast<int>(g);
        if (here && flat != last_slot_open_) ImGui::SetNextItemOpen(true);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        const bool open = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<std::intptr_t>(g)),
                                            ImGuiTreeNodeFlags_SpanAllColumns | (g == 0 ? ImGuiTreeNodeFlags_DefaultOpen : 0), "%s",
                                            grid.name.c_str());
        if (!grid.role.empty()) tip(grid.name.c_str(), nullptr, grid.role.c_str());
        ImGui::TableSetColumnIndex(1);
        right_text(std::to_string(grid.slots.size()), kTextDim);
        if (!open) continue;
        for (std::size_t k = 0; k < grid.slots.size(); ++k) {
            const GridSlot& s = grid.slots[k];
            const SlotRef ref{static_cast<int>(g), static_cast<int>(k)};
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::PushID(static_cast<int>(k));
            const std::string text = s.s >= 0 ? fmt("%s  %.0f m", s.name.c_str(), s.s) : s.name;
            if (ImGui::Selectable(text.c_str(), here && sel.slot == static_cast<int>(k),
                                  ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                track.select_slot(ref);
                focus_ = Focus::Slot;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) track.look_from_slot(ref, cam);
            }
            ImGui::PopID();
            if (s.lat != 0.0) {
                ImGui::TableSetColumnIndex(1);
                right_text(s.lat > 0 ? "esq." : "dir.", kTextDim);
            }
        }
        if (!grid.markers.empty()) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            if (grid.markers.size() == 1) ImGui::TextDisabled("1 nó de apoio");
            else ImGui::TextDisabled("%zu nós de apoio", grid.markers.size());
            tip("Nós de apoio", nullptr, "Pontos da grade sem vaga de carro (car_grid_spline_*)");
        }
        ImGui::TreePop();
    }
    last_slot_open_ = flat;
    ImGui::TreePop();
}

void EditorUi::replay_tree(TrackView& track, render::OrbitCamera& cam) {
    const Replay& rep = track.route().replay;
    if (rep.cameras.empty() && rep.zones.empty()) return;
    if (track.replay_selected() != last_replay_sel_) {  // Shift+C ou --look abrem a lista na câmera nova
        last_replay_sel_ = track.replay_selected();
        if (last_replay_sel_ >= 0) ImGui::SetNextItemOpen(true);
    }
    if (!layer_row("replay", ICON_CAMERAS, "Câmeras do replay", std::to_string(rep.cameras.size()), &track.show_replay(), "C"))
        return;
    const int sel = track.replay_selected();
    for (std::size_t i = 0; i < rep.cameras.size(); ++i) {
        const ReplayCamera& c = rep.cameras[i];
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::Selectable(c.name.c_str(), static_cast<int>(i) == sel,
                              ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
            track.select_replay(static_cast<int>(i));
            focus_ = Focus::Camera;
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) track.look_through(static_cast<int>(i), cam);
        }
        ImGui::PopID();
        tip(c.name.c_str(), nullptr, (c.kind + (c.role.empty() ? "" : "  ·  " + c.role)).c_str());
    }
    if (!rep.zones.empty()) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        const bool open = ImGui::TreeNodeEx("zonas", ImGuiTreeNodeFlags_SpanAllColumns, "Zonas de troca");
        ImGui::TableSetColumnIndex(1);
        right_text(std::to_string(rep.zones.size()), kTextDim);
        if (open) {
            for (std::size_t i = 0; i < rep.zones.size(); ++i) {
                const ReplayZone& z = rep.zones[i];
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                std::string text = z.name + fmt("  %.0f m", z.s);
                if (z.lap > 0) text += "  (volta " + std::to_string(z.lap) + ")";
                const int target = z.sw.empty() ? -1 : rep.find(z.sw.front().camera);
                ImGui::PushID(static_cast<int>(i));
                if (ImGui::Selectable(text.c_str(), target >= 0 && target == sel, ImGuiSelectableFlags_SpanAllColumns)) {
                    track.select_replay(target);
                    if (target >= 0) focus_ = Focus::Camera;
                }
                ImGui::PopID();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
                    std::string body;
                    for (const ReplaySwitch& w : z.sw) body += w.camera + fmt("  %d%%\n", static_cast<int>(w.p * 100 + 0.5));
                    if (!body.empty()) body.pop_back();
                    tip("Troca para", nullptr, body.c_str());
                }
            }
            ImGui::TreePop();
        }
    }
    if (!rep.bounds.empty()) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextDisabled("%zu prismas em volta das peças altas", rep.bounds.size());
    }
    ImGui::TreePop();
}

// ---- Histórico

void EditorUi::history(TrackView& track) {
    const edit::History& h = track.history();
    ImGui::TextDisabled("passo %zu de %zu", h.pos(), h.size());
    ImGui::BeginChild("##passos", ImVec2(0, 0), ImGuiChildFlags_None);
    // com o histórico cheio, os passos mais antigos saíram: o início da lista não é mais o arquivo
    const std::string first = h.dropped() ? fmt("%s  Início (%zu passos mais antigos descartados)", ICON_OPEN, h.dropped())
                                          : std::string(ICON_OPEN "  Arquivo aberto");
    if (ImGui::Selectable(first.c_str(), h.pos() == 0)) track.history_go(0);
    if (h.dropped()) tip("Início do histórico", nullptr, "Desfazer tudo não volta mais ao arquivo; R restaura um objeto à matriz do arquivo");
    for (std::size_t k = 0; k < h.entries().size(); ++k) {
        const auto& e = h.entries()[k];
        const bool future = k >= h.pos();
        if (future) ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
        const std::string label = fmt("%zu.  %s%s###h%zu", k + 1, e.label.c_str(), e.before.size() > 1 ? " (vários)" : "", k);
        if (ImGui::Selectable(label.c_str(), k + 1 == h.pos())) track.history_go(k + 1);
        if (future) ImGui::PopStyleColor();
    }
    // rola para o fim só quando entra um passo novo (senão não dá para rolar a lista)
    if (h.size() != last_hist_size_) {
        if (h.pos() == h.size()) ImGui::SetScrollHereY(1.0f);
        last_hist_size_ = h.size();
    }
    ImGui::EndChild();
}

// ---- Inspector

void EditorUi::inspector(TrackView& track, render::OrbitCamera& cam) {
    switch (focus_) {
    case Focus::Slot:
        slot_inspector(track, cam);
        return;
    case Focus::Camera:
        camera_inspector(track, cam);
        return;
    case Focus::Object:
        if (track.selected() >= 0) break;
        [[fallthrough]];
    default:
        track_inspector(track);
        return;
    }

    const Instances& inst = track.instances();
    const auto i = static_cast<std::uint32_t>(track.selected());
    const std::string& name = track.track().types[inst.type[i]].name;
    const auto& ty = track.objects().types()[inst.type[i]];
    const bool hidden = inst.hidden[i] != 0;
    const bool added = inst.idnum[i] >= kAdded;
    const bool edited = !hidden && edit::changed(inst, i);
    const float s = scale_;

    ImGui::PushFont(font_head_);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::Text("%s  %s", layer_icon(ty.layer), short_name(name).c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopFont();
    ImGui::TextDisabled("%s  ·  kind %c  ·  %s", instance_label(track, i).c_str(), name.empty() ? '?' : name[0], TrackView::source_file(name));
    if (hidden) {
        dot(kTextDim);
        ImGui::TextDisabled(added ? "Cópia desfeita" : "Apagado nesta sessão");
    } else if (added) {
        dot(kYellow);
        ImGui::TextColored(kYellow, "Cópia nova (vai como added no edits.json)");
    } else if (edited) {
        dot(kYellow);
        ImGui::TextColored(kYellow, "Editado");
    } else {
        dot(kTextDim);
        ImGui::TextDisabled("Como no arquivo");
    }

    // Transformação: posição e giro em Y editáveis, escala só leitura
    section("Transformação");
    ImGui::BeginDisabled(hidden);
    ImGui::PushID(static_cast<int>(i));  // campos de outra seleção são outros campos (o texto aberto não passa adiante)
    static float base[kInstFloats];
    static float yaw0 = 0.0f;
    const float* m = inst.matrix(i);
    if (begin_props("##transf")) {
        static const char* const axis[] = {"X", "Y", "Z"};
        static const char* const labels[] = {"Mover X (campo)", "Mover Y (campo)", "Mover Z (campo)"};
        for (int a = 0; a < 3; ++a) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(kAxis[a], "%s", axis[a]);
            ImGui::TableNextColumn();
            ImGui::PushID(a);
            float v = m[9 + a];
            const ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::SetNextItemWidth(-FLT_MIN);
            const bool changed = ImGui::DragFloat("##v", &v, 0.05f, 0.0f, 0.0f, "%.3f m");
            const bool activated = ImGui::IsItemActivated(), deactivated = ImGui::IsItemDeactivated();
            tip(fmt("Posição %s", axis[a]).c_str(), nullptr, "Em metros. Arraste ou dê Ctrl+clique para digitar");
            // faixa da cor do eixo na borda do campo
            ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + std::round(3.0f * s), p.y + ImGui::GetFrameHeight()), u32(kAxis[a]),
                                                      ImGui::GetStyle().FrameRounding, ImDrawFlags_RoundCornersLeft);
            if (activated) track.begin_change(i);
            if (changed) {
                if (!track.changing()) track.begin_change(i);
                float next[kInstFloats];
                std::copy(m, m + kInstFloats, next);
                next[9 + a] = v;
                track.set_matrix(i, next);
            }
            if (deactivated) track.end_change(labels[a]);
            ImGui::PopID();
        }

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Giro Y");
        ImGui::TableNextColumn();
        float deg = yaw_of(m) * 180.0f / kPi;
        ImGui::SetNextItemWidth(-FLT_MIN);
        const bool yaw_changed = ImGui::DragFloat("##giro", &deg, 0.5f, -360.0f, 360.0f, "%.2f°");
        if (ImGui::IsItemActivated() && track.begin_change(i)) {
            std::copy(m, m + kInstFloats, base);
            yaw0 = yaw_of(m);
        }
        if (yaw_changed) {
            if (!track.changing() && track.begin_change(i)) {
                std::copy(m, m + kInstFloats, base);
                yaw0 = yaw_of(m);
            }
            float next[kInstFloats];
            if (std::isfinite(deg)) {
                edit::spin(next, base, deg * kPi / 180.0f - yaw0);
                track.set_matrix(i, next);
            }
        }
        if (ImGui::IsItemDeactivated()) track.end_change("Girar (campo)");
        tip("Giro em Y", "Q / E", "Em graus. Arraste ou dê Ctrl+clique para digitar");

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TableNextColumn();
        static const float turns[] = {-90.0f, -15.0f, 15.0f, 90.0f};
        static const char* const turn_labels[] = {"−90°", "−15°", "+15°", "+90°"};
        const float sp = ImGui::GetStyle().ItemSpacing.x * 0.5f;
        const float bw = std::floor((ImGui::GetContentRegionAvail().x - 3.0f * sp) / 4.0f);
        for (int k = 0; k < 4; ++k) {
            if (k) ImGui::SameLine(0, sp);
            if (ImGui::Button(turn_labels[k], ImVec2(bw, 0))) track.turn_selected(turns[k]);
        }

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Escala");
        ImGui::TableNextColumn();
        const float sx = std::hypot(m[0], m[1], m[2]), sy = std::hypot(m[3], m[4], m[5]), sz = std::hypot(m[6], m[7], m[8]);
        ImGui::TextDisabled("%.3f   %.3f   %.3f", static_cast<double>(sx), static_cast<double>(sy), static_cast<double>(sz));
        ImGui::EndTable();
    }
    ImGui::PopID();
    ImGui::EndDisabled();

    section("Ações");
    const float half = std::floor((ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f);
    auto action = [&](const char* icon, const char* title, const char* keys, const char* body, bool enabled, float width) {
        ImGui::BeginDisabled(!enabled);
        const bool pressed = ImGui::Button((std::string(icon) + "  " + title).c_str(), ImVec2(width, 0));
        ImGui::EndDisabled();
        tip(title, keys, body);
        return pressed;
    };
    if (action(ICON_FRAME, "Enquadrar", "F", "Leva a câmera até o objeto", true, half)) track.frame_selected(cam);
    ImGui::SameLine();
    if (action(ICON_RESTORE, "Restaurar", "R", "Volta à matriz do arquivo e mostra de novo", hidden || edited, half))
        track.restore_selected();
    if (!hidden) {
        if (action(ICON_DUPLICATE, "Duplicar", "Ctrl+D", "Só objetos e: (objects.ens); ornamentos e árvores têm contagem fixa",
                   name.starts_with("e:"), half))
            track.duplicate_selected();
        ImGui::SameLine();
        if (action(ICON_DELETE, "Apagar", "Delete", nullptr, true, half)) track.delete_selected();
        if (action(ICON_SETTLE, "Pôr no chão", "T", "Baixa (ou sobe) o objeto até o terreno que está sob ele, só na altura", true, half))
            track.settle_selected();
        ImGui::SameLine();
        if (action(ICON_ALIGN, "Alinhar ao terreno", "Shift+T",
                   "Inclina o objeto junto com o chão sob a base e desce o que ficaria no ar (até 25°). Árvores ficam em pé", true, half))
            track.align_selected();
        if (action(ICON_ALIGN, "Alinhar todos deste tipo", nullptr,
                   "Alinha ao terreno todas as instâncias à mostra deste tipo, num passo só (Ctrl+Z desfaz tudo)", true, -FLT_MIN))
            track.align_type();
        check("Manter em pé ao alinhar", &track.align_upright);
        tip("Manter em pé", nullptr,
            "Para prédios, tendas e placas: não inclina, só desce até o ponto mais baixo do chão sob a base (nada fica no ar)");
    }

    ImGui::Spacing();
    if (fold("Matriz (atual | arquivo)")) {
        if (ImGui::BeginTable("##m", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings)) {
            static const char* const rows[] = {"linha 0", "linha 1", "linha 2", "posição"};
            for (int r = 0; r < 4; ++r) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", rows[r]);
                for (int c = 0; c < 3; ++c) {
                    ImGui::TableNextColumn();
                    const float a = m[r * 3 + c], b = inst.m0[i * kInstFloats + static_cast<std::size_t>(r * 3 + c)];
                    if (a != b) ImGui::TextColored(kYellow, "%.4g", static_cast<double>(a));
                    else ImGui::Text("%.4g", static_cast<double>(a));
                    if (a != b && ImGui::IsItemHovered()) ImGui::SetTooltip("arquivo: %.6g", static_cast<double>(b));
                }
            }
            ImGui::EndTable();
        }
    }
    if (fold("Tipo e materiais", ImGuiTreeNodeFlags_DefaultOpen)) {
        const TypeInfo& info = track.track().types[inst.type[i]];
        ImGui::TextDisabled("%zu malha(s) em objects.bin · %zu instância(s) nesta rota", info.count, ty.group.size());
        if (ty.empty) {
            ImGui::TextDisabled("Sem malha: não desenha (marcador)");
        } else {
            const glm::vec3 size = ty.hi - ty.lo;
            ImGui::TextDisabled("caixa %.2f × %.2f × %.2f m", static_cast<double>(size.x), static_cast<double>(size.y), static_cast<double>(size.z));
        }
        const float thumb = ImGui::GetTextLineHeight() * 3.0f;
        for (std::size_t k = 0; k < ty.parts.size(); ++k) {
            const auto& part = ty.parts[k];
            ImGui::PushID(static_cast<int>(k));
            const GLuint tex = track.textures().for_material(part.material);
            if (tex) {
                ImGui::Image(static_cast<ImTextureID>(tex), ImVec2(thumb, thumb));
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::Image(static_cast<ImTextureID>(tex), ImVec2(256.0f * s, 256.0f * s));
                    ImGui::EndTooltip();
                }
            } else {
                ImGui::ColorButton("##cor", ImVec4(part.color.r, part.color.g, part.color.b, 1.0f), ImGuiColorEditFlags_NoTooltip,
                                   ImVec2(thumb, thumb));
            }
            ImGui::SameLine();
            ImGui::BeginGroup();
            ImGui::TextWrapped("%s", part.material.c_str());
            const auto it = track.track().materials.find(part.material);
            if (it != track.track().materials.end()) ImGui::TextDisabled("%s", it->second.c_str());
            else ImGui::TextDisabled("sem textura (cor fixa)");
            ImGui::TextDisabled("%d triângulos", part.count / 3);
            ImGui::EndGroup();
            ImGui::PopID();
        }
    }
}

void EditorUi::track_inspector(TrackView& track) {
    const Track& t = track.track();
    const Instances& inst = track.instances();
    ImGui::PushFont(font_head_);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::Text("%s  %s", ICON_ROUTE, t.id.c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopFont();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", t.src.c_str());
    ImGui::TextDisabled("%s", t.dir.c_str());
    ImGui::PopTextWrapPos();

    section("Pista");
    if (begin_props("##pista")) {
        prop("Rota", track.route().name + fmt("  (%zu de %zu)", track.route_index() + 1, t.routes.size()));
        prop("Terreno", track.route().terrain_file);
        prop("Malhas", std::to_string(track.terrain().meshes()));
        prop("Vértices", std::to_string(track.terrain().vertices()));
        prop("Triângulos", std::to_string(track.terrain().triangles()));
        prop("Instâncias", std::to_string(track.objects().visible()) + " visíveis de " + std::to_string(inst.n));
        prop("Tipos", std::to_string(t.types.size()));
        prop("Materiais", std::to_string(t.materials.size()));
        prop("Portões", std::to_string(track.route().gates.size()));
        prop("Linhas da IA", std::to_string(track.route().ai.size()));
        prop("Grava em", near_path(track.out_path()));
        tip("Grava em", "Ctrl+S", track.out_path().c_str());
        ImGui::EndTable();
    }

    section("Memória de vídeo");
    if (begin_props("##vram")) {
        auto& tex = track.textures();
        prop("Texturas", fmt("%zu na GPU, %zu na fila, %zu falharam", tex.loaded(), tex.pending(), tex.failed()));
        prop("Das texturas", fmt("%.0f de %.0f MB%s", static_cast<double>(tex.gpu_bytes()) / 1048576.0, static_cast<double>(tex.budget()) / 1048576.0,
                                 tex.over_budget() ? "  (acima do limite!)" : ""),
             tex.over_budget() ? kRed : kText);
        if (std::size_t total = 0, free = 0; gl::vram_kb(total, free))
            prop("Da GPU", total ? fmt("%.0f MB usados de %.0f MB", static_cast<double>(total - free) / 1024.0, static_cast<double>(total) / 1024.0)
                                 : fmt("%.0f MB livres", static_cast<double>(free) / 1024.0));
        if (tex.evicted() || tex.downscaled())
            prop("Sem espaço", fmt("%zu reduzidas, %zu descartadas", tex.downscaled(), tex.evicted()), kYellow);
        ImGui::EndTable();
    }
    ImGui::Spacing();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s  Clique num objeto (no 3D ou na Cena), numa vaga de largada ou numa câmera do replay para ver os detalhes aqui.",
                        ICON_INFO);
    ImGui::PopTextWrapPos();
}

void EditorUi::slot_inspector(TrackView& track, render::OrbitCamera& cam) {
    const Route& route = track.route();
    const SlotRef sel = track.slot_selected();
    const GridSlot* s = route.slot(sel);
    if (!s) return;
    const Grid& g = route.grids[static_cast<std::size_t>(sel.grid)];
    ImGui::PushFont(font_head_);
    ImGui::Text("%s  %s / %s", ICON_GRIDS, g.name.c_str(), s->name.c_str());
    ImGui::PopFont();
    if (!g.role.empty()) ImGui::TextDisabled("%s", g.role.c_str());

    section("Vaga de largada");
    if (begin_props("##vaga")) {
        prop("Posição", fmt("%.1f   %.1f   %.1f", s->pos[0], s->pos[1], s->pos[2]));
        if (s->s >= 0) prop("Na pista", fmt("em %.0f m, %.1f m %s do centro", s->s, std::fabs(s->lat), s->lat >= 0 ? "à esquerda" : "à direita"));
        prop("Caixa", fmt("%.1f × %.1f m", static_cast<double>(s->width), static_cast<double>(s->length)));
        float center = 0, wheels = 0;
        if (!track.slot_clearance(*s, center, wheels)) {
            prop("Altura", "sem terreno embaixo", kYellow);
        } else {
            const bool low = wheels < TrackView::kSlotLow, high = center > TrackView::kSlotHigh;
            prop("Altura", fmt("%.2f m acima do chão (rodas %.2f m)", static_cast<double>(center), static_cast<double>(wheels)),
                 low ? kRed : high ? kYellow : kGreen);
            if (low) prop("", "dentro do chão: no jogo o carro nasce enterrado e a carga trava", kRed);
            else if (high) prop("", "alto demais: o carro cai ao nascer", kYellow);
        }
        ImGui::EndTable();
    }

    section("Ver");
    const float half = std::floor((ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f);
    if (ImGui::Button(ICON_CAR "  Ver do carro", ImVec2(half, 0))) track.look_from_slot(sel, cam);
    tip("Ver do carro", "Shift+L", "Põe a vista no banco do piloto, olhando para a frente da vaga (Shift+L: próxima vaga)");
    ImGui::SameLine();
    if (ImGui::Button(ICON_FRAME "  Enquadrar", ImVec2(half, 0))) track.frame_slot(sel, cam);
    tip("Enquadrar", nullptr, "Vista de trás e de cima da vaga");
    if (ImGui::Button(ICON_CLOSE "  Tirar o destaque", ImVec2(-FLT_MIN, 0))) track.select_slot(SlotRef{});
    tip("Tirar o destaque", nullptr, "A vaga deixa de ficar marcada no 3D e o Inspector volta à pista");
}

void EditorUi::camera_inspector(TrackView& track, render::OrbitCamera& cam) {
    const Replay& rep = track.route().replay;
    const int sel = track.replay_selected();
    if (sel < 0 || sel >= static_cast<int>(rep.cameras.size())) return;
    const ReplayCamera& c = rep.cameras[static_cast<std::size_t>(sel)];
    ImGui::PushFont(font_head_);
    ImGui::Text("%s  %s", ICON_CAMERAS, c.name.c_str());
    ImGui::PopFont();
    ImGui::TextDisabled("%s%s%s", c.kind.c_str(), c.role.empty() ? "" : "  ·  ", c.role.c_str());

    section("Câmera do replay");
    if (begin_props("##camera")) {
        prop("Posição", fmt("%.1f   %.1f   %.1f", c.pos[0], c.pos[1], c.pos[2]));
        if (c.s >= 0) prop("Na pista", fmt("em %.0f m", c.s));
        if (!c.path.empty()) prop("Caminho", fmt("%zu trechos, %.1f s%s", c.path.size() / 4, c.duration, c.target.empty() ? "" : ", com alvo"));
        std::string by;
        for (const ReplayZone& z : rep.zones)
            for (const ReplaySwitch& w : z.sw)
                if (w.camera == c.name) by += (by.empty() ? "" : ", ") + z.name + fmt(" (%d%%)", static_cast<int>(w.p * 100 + 0.5));
        prop("Ligada por", by.empty() ? "nenhuma zona (tomada do jogo)" : by);
        ImGui::EndTable();
    }

    section("Ver");
    if (ImGui::Button(ICON_CAMERAS "  Ver por esta câmera", ImVec2(-FLT_MIN, 0))) track.look_through(sel, cam);
    tip("Ver por esta câmera", "Shift+C", "Põe a vista no lugar da câmera, olhando para onde ela olha (Shift+C: próxima)");
    if (ImGui::Button(ICON_CLOSE "  Tirar o destaque", ImVec2(-FLT_MIN, 0))) track.select_replay(-1);
    tip("Tirar o destaque", nullptr, "A câmera deixa de ficar marcada no 3D e o Inspector volta à pista");
}

// ---- Janelas

// ---- Sobre

void EditorUi::about_window() {
    const char* const title = ICON_INFO "  Sobre###Sobre";
    // nos primeiros quadros as janelas que aparecem tomam o foco e fechariam o popup (--ui sobre)
    if (about_request_ && ImGui::GetFrameCount() > 3) {
        ImGui::OpenPopup(title);
        about_request_ = false;
    }
    if (!ImGui::IsPopupOpen(title)) return;
    if (!about_tex_) {
        int w = 0, h = 0;
        if (uint8_t* rgba = WebPDecodeRGBA(fonts::kAboutImage.data, fonts::kAboutImage.size, &w, &h)) {
            glGenTextures(1, &about_tex_);
            glBindTexture(GL_TEXTURE_2D, about_tex_);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
            glBindTexture(GL_TEXTURE_2D, 0);
            WebPFree(rgba);
            about_w_ = w, about_h_ = h;
        }
    }

    const float s = scale_;
    const float width = std::round(360.0f * s);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(width + 2.0f * ImGui::GetStyle().WindowPadding.x, 0.0f));
    if (!ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize)) return;
    auto centered = [&](const char* text, const ImVec4& color, ImFont* font = nullptr) {
        if (font) ImGui::PushFont(font);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (width - ImGui::CalcTextSize(text).x) * 0.5f));
        ImGui::TextColored(color, "%s", text);
        if (font) ImGui::PopFont();
    };
    if (about_tex_ && about_w_ > 0) {
        const ImVec2 size(width, std::round(width * static_cast<float>(about_h_) / static_cast<float>(about_w_)));
        const ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::Image(static_cast<ImTextureID>(about_tex_), size);
        ImGui::GetWindowDrawList()->AddRect(at, at + size, raw(kLine));
    }
    ImGui::Spacing();
    centered("Editor de Pistas", kText, font_head_);
    centered("Ferramenta do DR2Hook para DiRT Rally 2.0", kTextDim);
    ImGui::Spacing();
    section("Créditos");
    if (begin_props("##sobre")) {
        prop("Criado por", "dsnsilvf");
        ImGui::EndTable();
    }
    ImGui::Spacing();
    section("Agradecimentos especiais");
    ImGui::Bullet();
    ImGui::TextUnformatted("Ego Engine Modding Community");
    ImGui::Bullet();
    ImGui::TextUnformatted("Ssor");
    ImGui::SameLine();
    ImGui::TextDisabled("(Discord)");
    ImGui::Spacing();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
    ImGui::TextDisabled("Feito com SDL3, Dear ImGui, as fontes Inter e os ícones Lucide.");
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    const bool enter = ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);
    if (primary_button("Fechar") || enter || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void EditorUi::help_window() {
    const float s = scale_;
    ImGui::SetNextWindowSize(ImVec2(std::round(560.0f * s), std::round(640.0f * s)), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    if (!ImGui::Begin(ICON_KEYBOARD "  Atalhos###Atalhos", &show_help_,
                      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    struct Key {
        const char* keys;  // nulo: título do grupo
        const char* what;
    };
    static const Key keys[] = {
        {nullptr, "Câmera"},
        {"Botão esquerdo", "clique seleciona; arrastar orbita (fora de objeto ou com Selecionar)"},
        {"Direito / meio / Shift+esquerdo", "pan"},
        {"Roda", "zoom"},
        {"W A S D", "andar (Shift = ×3)"},
        {"F", "enquadrar a seleção ou a rota"},
        {"V", "vista: livre / seguir o carro do jogo / pela câmera do jogo"},
        {nullptr, "Edição"},
        {"1 / 2 / 3", "Selecionar / Mover / Girar"},
        {"Shift ao mover", "sobe e desce"},
        {"Setas do gizmo", "mover só em X, Y ou Z"},
        {"Quadrados do gizmo", "mover num plano (o verde é o chão, X e Z)"},
        {"Centro do gizmo", "mover junto com a tela"},
        {"Anel do gizmo", "girar em Y em torno do objeto"},
        {"X", "eixos do gizmo: do mundo / do objeto"},
        {"Q / E (Shift)", "girar −15° / +15° (±90°)"},
        {"Delete", "apagar"},
        {"Ctrl+D", "duplicar (objetos e:)"},
        {"R", "restaurar do arquivo"},
        {"T", "pôr no chão (altura do terreno sob o objeto)"},
        {"Shift+T", "alinhar ao terreno (inclina com o chão)"},
        {"Esc", "tirar seleção; fechar janela de confirmação"},
        {"Ctrl+Z / Ctrl+Y", "desfazer / refazer"},
        {"Ctrl+S", "gravar edits.json"},
        {"Ctrl+Q", "sair (pergunta se há edições não gravadas)"},
        {nullptr, "Exibição"},
        {"Tab / Shift+Tab", "próxima / anterior rota"},
        {"F1 F2 F3 F4", "terreno, objetos, árvores, terreno distante"},
        {"G / I", "portões / linha da IA"},
        {"C / Shift+C", "câmeras do replay / ver pela próxima"},
        {"L / Shift+L", "largada (vagas do carro) / ver da próxima vaga"},
        {"[ / ]", "distância de desenho"},
        {"F10 / F11", "barra e painéis / esta janela"},
        {nullptr, "Jogo"},
        {"F5", "testar no jogo (porta a pista e abre o jogo nela)"},
        {"F6", "pausar / continuar a especial"},
        {"Shift+F5", "parar: cancela o teste e fecha o jogo"},
    };
    float key_w = 0.0f;
    for (const Key& k : keys)
        if (k.keys) key_w = std::max(key_w, ImGui::CalcTextSize(k.keys).x);
    bool open = false;
    for (const Key& k : keys) {
        if (!k.keys) {
            if (open) ImGui::EndTable();
            section(k.what);
            open = ImGui::BeginTable(k.what, 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings);
            if (open) {
                ImGui::TableSetupColumn("tecla", ImGuiTableColumnFlags_WidthFixed, key_w);
                ImGui::TableSetupColumn("faz", ImGuiTableColumnFlags_WidthStretch);
            }
            continue;
        }
        if (!open) continue;
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextColored(kAccentText, "%s", k.keys);
        ImGui::TableNextColumn();
        ImGui::TextWrapped("%s", k.what);
    }
    if (open) ImGui::EndTable();
    ImGui::End();
}

void EditorUi::modals(TrackView* track) {
    launch_modal(track);
    const char* const title = ICON_WARNING "  Edições não gravadas###naogravadas";
    if (pending_ != Pending::None && !ImGui::IsPopupOpen(title)) ImGui::OpenPopup(title);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) return;
    const bool quit = pending_ == Pending::Quit;
    ImGui::TextUnformatted(quit ? "Há edições que não estão no edits.json." : "Abrir outra pista descarta as edições desta.");
    if (track) ImGui::TextDisabled("%s", track->out_path().c_str());
    ImGui::Spacing();
    auto done = [&](bool go) {
        if (go) {
            if (quit) quit_request = true;
            else open_request = pending_dir_;
        }
        pending_ = Pending::None;
        ImGui::CloseCurrentPopup();
    };
    if (primary_button(quit ? ICON_SAVE "  Gravar e sair" : ICON_SAVE "  Gravar e abrir")) {
        if (track && track->save()) done(true);
        else done(false);  // a falha fica na barra de status; nada se perde
    }
    ImGui::SameLine();
    if (ImGui::Button(quit ? "Sair sem gravar" : "Abrir sem gravar")) done(true);
    ImGui::SameLine();
    if (ImGui::Button("Cancelar") || ImGui::IsKeyPressed(ImGuiKey_Escape)) done(false);
    ImGui::EndPopup();
}

// ---- Jogo: testar (F5), pausar (F6) e parar (Shift+F5)

void EditorUi::open_launch(TrackView* track) {
    if (!track) {
        show_message("Testar no jogo: abra uma pista antes");
        return;
    }
    // com um teste rodando, mostra o progresso; senão, as opções de um teste novo
    if (!launch_.child.running()) launch_.started = false;
    launch_.open = true;
}

void EditorUi::toggle_pause(TrackView* track) {
    if (!game_.running()) {
        show_message("Pausar: o jogo não está aberto");
        return;
    }
    if (control_.child.running()) return;  // o comando anterior ainda não voltou
    // o estado vem do jogo ao vivo; sem ele, pausa (o jogo responde se o topo não é a corrida)
    const bool paused = track && track->live_connected() && track->live_sample().paused;
    run_control(track, {"--cmd", paused ? "unpause" : "pause"}, paused ? "Continuar" : "Pausar");
}

void EditorUi::stop_game(TrackView* track) {
    const bool busy = launch_.child.running();
    if (!busy && !game_.running()) {
        show_message("Parar: nada rodando (o jogo está fechado)");
        return;
    }
    if (busy) {
        launch_.child.terminate();
        launch_.progress.feed("@fail cancelado pelo Parar");
    }
    run_control(track, {"--close"}, "Parar");
}

void EditorUi::run_control(TrackView* track, std::vector<std::string> args, std::string what) {
    Control& C = control_;
    if (C.repo.empty()) C.repo = find_repo(track ? track->track().dir : std::string());
    if (C.repo.empty()) {
        show_message(what + " falhou: não achei scripts/research/ring_deploy.py (rode o viewer da raiz do repositório)");
        return;
    }
    C.next_args = std::move(args);
    C.next_what = std::move(what);
    if (C.child.running()) C.child.terminate();  // o mais novo vale: o poll começa o da fila quando este sair
    else poll_control();
}

void EditorUi::poll_control() {
    Control& C = control_;
    const bool was = C.child.running();
    C.child.poll(C.progress);
    if (was && !C.child.running() && C.next_args.empty()) {
        const Progress& p = C.progress;
        show_message(p.state == Progress::State::Done ? C.what + ": " + p.result : C.what + " falhou: " + p.result);
    }
    if (C.child.running() || C.next_args.empty()) return;
    C.progress = Progress{};
    C.what = std::move(C.next_what);
    std::vector<std::string> argv = {"python3", "-u", C.repo + "/scripts/research/ring_deploy.py"};
    argv.insert(argv.end(), C.next_args.begin(), C.next_args.end());
    C.next_args.clear();
    C.next_what.clear();
    std::string err;
    if (!C.child.start(argv, err)) show_message(C.what + " falhou: " + err);
    else show_message(C.what + "…");
}

void EditorUi::poll_launch() {
    const bool was = launch_.child.running();
    launch_.child.poll(launch_.progress);
    if (!was || launch_.child.running()) return;
    launch_.finished_at = launch_.child.seconds();
    const Progress& p = launch_.progress;
    show_message(p.state == Progress::State::Done ? "Testar no jogo: " + p.result : "Testar no jogo falhou: " + p.result);
}

void EditorUi::start_launch(TrackView& track) {
    Launch& L = launch_;
    L.progress = Progress{};
    L.finished_at = -1.0;
    L.started = true;
    L.follow_log = true;
    const std::string repo = find_repo(track.track().dir);
    if (repo.empty()) {
        L.progress.feed("@fail não achei scripts/research/ring_deploy.py (rode o viewer da raiz do repositório)");
        return;
    }
    // as edições do jeito que estão agora, gravadas ou não (o edits.json do usuário não muda)
    const std::string edits = repo + "/build/re/ring_deploy/viewer.edits.json";
    try {
        edit::write_text(edits, track.current_edits());
    } catch (const std::exception& e) {
        L.progress.feed(std::string("@fail não gravei as edições do teste: ") + e.what());
        return;
    }
    std::vector<std::string> argv = {"python3", "-u", repo + "/scripts/research/ring_deploy.py", "--mode", kModeArg[L.mode],
                                     "--edits", edits};
    if (L.quick) argv.push_back("--quick");
    std::string cmd;
    for (std::size_t k = 2; k < argv.size(); ++k) cmd += (k > 2 ? " " : "") + argv[k];
    L.progress.log.push_back("$ " + cmd);
    std::string err;
    if (!L.child.start(argv, err)) L.progress.feed("@fail " + err);
}

void EditorUi::launch_modal(TrackView* track) {
    Launch& L = launch_;
    const char* const title = ICON_PLAY "  Testar no jogo###testar";
    if (L.open) {
        if (!ImGui::IsPopupOpen(title)) ImGui::OpenPopup(title);
        L.open = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) return;
    const bool enter = ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
    const bool esc = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    const float s = scale_;

    if (!L.started) {
        // ---- opções
        const bool ring = track && track->track().id == kPortedTrack;
        const std::string repo = track ? find_repo(track->track().dir) : std::string();
        ImGui::TextDisabled("Pista");
        ImGui::SameLine();
        ImGui::TextColored(kAccentText, "%s", track ? track->track().id.c_str() : "(nenhuma)");
        ImGui::SameLine();
        ImGui::TextDisabled("route_0");
        check("Inicialização rápida", &L.quick);
        tip("Inicialização rápida", "R",
            "Pula a tela de carregamento: a foto aérea renderizada e o traçado (fica a da última vez). Economiza uns 20 s. No jogo, "
            "tela preta com o log ao vivo e sem som do boot até a largada. As outras etapas só refazem o que mudou");
        section("Quem dirige");
        static const char* const modes[] = {"Bot dirige", "Eu dirijo", "Câmera livre"};
        static const char* const tips[] = {
            "O AutoStage abre a pista pelo benchmark do jogo: o carro anda sozinho e a câmera segue o carro",
            "Ainda não: o AutoStage usa o benchmark, que não passa o controle ao jogador. Falta achar como (engenharia reversa do benchmark)",
            "O bot dirige e a câmera fica solta (o teste manda F9 na largada; no jogo, F9 alterna)"};
        for (int k = 0; k < 3; ++k) {
            ImGui::BeginDisabled(k == 1);
            if (radio(modes[k], L.mode == k)) L.mode = k;
            if (k == 1) {
                ImGui::SameLine();
                ImGui::TextDisabled("(ainda não)");
            }
            ImGui::EndDisabled();
            tip(modes[k], k == 1 ? nullptr : "↑ ↓", tips[k]);
        }
        ImGui::Spacing();
        ImGui::Separator();
        if (track && track->unsaved())
            ImGui::TextColored(kYellow, "%s  As edições não gravadas entram no teste (o edits.json fica como está).", ICON_INFO);
        if (track && track->route().name != "route_0")
            ImGui::TextColored(kYellow, "%s  O teste leva só a route_0; as edições da %s ficam de fora.", ICON_WARNING, track->route().name.c_str());
        if (track && !ring) ImGui::TextColored(kRed, "%s  Só o DR2 Hook Ring (%s) tem o porte para o jogo por enquanto.", ICON_ALERT, kPortedTrack);
        if (ring && repo.empty())
            ImGui::TextColored(kRed, "%s  Não achei scripts/research/ring_deploy.py: rode o viewer da raiz do repositório.", ICON_ALERT);
        ImGui::TextDisabled("Fecha o jogo se estiver aberto e o abre direto na pista.");
        ImGui::Spacing();
        const bool can = ring && !repo.empty();
        ImGui::BeginDisabled(!can);
        const bool go = primary_button(ICON_PLAY "  Iniciar", rgb(46, 140, 76)) || (can && enter);
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancelar") || esc) {
            ImGui::CloseCurrentPopup();
        } else if (go && track) {
            start_launch(*track);
        }
        ImGui::SameLine(0, std::round(14.0f * s));
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Enter inicia · Esc cancela · ↑↓ quem dirige · R rápida");
        if (ImGui::IsKeyPressed(ImGuiKey_R, false)) L.quick = !L.quick;
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) L.mode = L.mode == 2 ? 0 : 2;  // pula o "Eu dirijo"
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) L.mode = L.mode == 0 ? 2 : 0;
        ImGui::EndPopup();
        return;
    }

    // ---- progresso
    const Progress& p = L.progress;
    const bool busy = L.child.running();
    const float width = std::max(560.0f * s, ImGui::GetMainViewport()->Size.x * 0.4f);
    if (p.steps > 0) ImGui::Text("Etapa %d de %d: %s", p.step, p.steps, p.label.c_str());
    else ImGui::TextUnformatted(busy ? "Começando…" : "");
    const double secs = busy ? L.child.seconds() : std::max(0.0, L.finished_at);
    const std::string overlay = fmt("%d%%  ·  %.0f s", static_cast<int>(p.fraction * 100.0f + 0.5f), secs);
    const ImVec4 bar = p.state == Progress::State::Failed ? kStop : p.state == Progress::State::Done ? kPlay : kAccent;
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, bar);
    ImGui::ProgressBar(p.fraction, ImVec2(width, 0), overlay.c_str());
    ImGui::PopStyleColor();
    const float log_h = ImGui::GetTextLineHeightWithSpacing() * 14;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, rgb(20, 21, 23));
    if (ImGui::BeginChild("##log", ImVec2(width, log_h), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar)) {
        for (const std::string& line : p.log) {
            const bool err = line.rfind("erro:", 0) == 0;
            const bool ok = line.rfind("pronto:", 0) == 0;
            const bool head = line.rfind("– ", 0) == 0;
            if (err || ok || head) ImGui::PushStyleColor(ImGuiCol_Text, err ? kRed : ok ? kGreen : kYellow);
            ImGui::TextUnformatted(line.c_str());
            if (err || ok || head) ImGui::PopStyleColor();
        }
        // segue o fim enquanto ninguém rolou para cima
        if (L.follow_log) ImGui::SetScrollHereY(1.0f);
        L.follow_log = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 2.0f;
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    if (p.state == Progress::State::Done) ImGui::TextColored(kGreen, "%s  Pronto: %s", ICON_CHECK, p.result.c_str());
    else if (p.state == Progress::State::Failed) ImGui::TextColored(kRed, "%s  Falhou: %s", ICON_ALERT, p.result.c_str());
    else ImGui::TextDisabled("O editor segue usável: Esconder deixa o teste rodando (o botão verde mostra o andamento).");
    ImGui::Spacing();
    if (busy) {
        if (ImGui::Button("Esconder (Esc)") || esc) ImGui::CloseCurrentPopup();
        ImGui::SameLine();
        if (ImGui::Button("Cancelar o teste")) {
            L.child.terminate();
            L.progress.feed("@fail cancelado (o jogo, se já abriu, continua aberto)");
        }
    } else {
        if (primary_button("Fechar (Enter)") || enter || esc) ImGui::CloseCurrentPopup();
        ImGui::SameLine();
        if (ImGui::Button(ICON_PLAY "  Testar de novo (F5)") || ImGui::IsKeyPressed(ImGuiKey_F5, false)) L.started = false;
    }
    ImGui::EndPopup();
}

// ---- Gizmo

namespace {

// Eixos do mundo que cada alça do ImGuizmo mexe (bits x, y, z), para o encaixe absoluto e o grudar no chão.
int moved_axes(int type) {
    switch (type) {
    case ImGuizmo::MT_MOVE_X: return 1;
    case ImGuizmo::MT_MOVE_Y: return 2;
    case ImGuizmo::MT_MOVE_Z: return 4;
    case ImGuizmo::MT_MOVE_YZ: return 2 | 4;
    case ImGuizmo::MT_MOVE_ZX: return 4 | 1;
    case ImGuizmo::MT_MOVE_XY: return 1 | 2;
    case ImGuizmo::MT_MOVE_SCREEN: return 7;
    default: return 0;
    }
}

const char* gizmo_label(int type) {
    switch (type) {
    case ImGuizmo::MT_MOVE_X: return "Mover X";
    case ImGuizmo::MT_MOVE_Y: return "Mover Y";
    case ImGuizmo::MT_MOVE_Z: return "Mover Z";
    case ImGuizmo::MT_MOVE_YZ: return "Mover no plano YZ";
    case ImGuizmo::MT_MOVE_ZX: return "Mover no chão (XZ)";
    case ImGuizmo::MT_MOVE_XY: return "Mover no plano XY";
    case ImGuizmo::MT_MOVE_SCREEN: return "Mover na tela";
    case ImGuizmo::MT_ROTATE_Y: return "Girar Y";
    default: return "Gizmo";
    }
}

}  // namespace

// Cores do tema (kAxis) e tamanhos na escala da tela; o realce é o amarelo da seleção.
void EditorUi::gizmo_style() {
    ImGuizmo::Style& st = ImGuizmo::GetStyle();
    const float s = scale_;
    st.TranslationLineThickness = 3.0f * s;
    st.TranslationLineArrowSize = 7.0f * s;
    st.RotationLineThickness = 3.0f * s;
    st.RotationOuterLineThickness = 2.0f * s;
    st.HatchedAxisLineThickness = 6.0f * s;
    st.CenterCircleSize = 5.0f * s;
    for (int a = 0; a < 3; ++a) {
        st.Colors[ImGuizmo::DIRECTION_X + a] = kAxis[a];
        st.Colors[ImGuizmo::PLANE_X + a] = ImVec4(kAxis[a].x, kAxis[a].y, kAxis[a].z, 0.45f);
    }
    st.Colors[ImGuizmo::SELECTION] = ImVec4(1.0f, 0.86f, 0.32f, 1.0f);
    st.Colors[ImGuizmo::ROTATION_USING_BORDER] = ImVec4(1.0f, 0.86f, 0.32f, 1.0f);
    st.Colors[ImGuizmo::ROTATION_USING_FILL] = ImVec4(1.0f, 0.86f, 0.32f, 0.35f);
    st.Colors[ImGuizmo::TRANSLATION_LINE] = ImVec4(1.0f, 1.0f, 1.0f, 0.55f);
}

// O ImGuizmo mexe numa cópia 4×4 da matriz da seleção (arraste preso ao eixo ou ao plano pelo raio do mouse, anel
// do giro em Y, o quanto andou escrito ao lado). Aqui a cópia volta à pista com o encaixe e o grudar no chão, e
// cada arraste vira uma entrada no histórico.
void EditorUi::gizmo(TrackView& track, const render::OrbitCamera& cam, const Rect& vp) {
    const int sel = track.selected();
    const bool shown = sel >= 0 && track.tool() != TrackView::Tool::Navigate && !track.instances().hidden[static_cast<std::size_t>(sel)];
    auto finish = [&] {
        if (gizmo_open_) track.end_change(gizmo_label(gizmo_type_));
        gizmo_drag_ = gizmo_open_ = false;
    };
    if (!shown) {
        if (gizmo_drag_) finish();  // a seleção sumiu no meio do arraste
        return;
    }
    const auto i = static_cast<std::uint32_t>(sel);
    const float* m = track.instances().matrix(i);
    float model[16] = {m[0], m[1], m[2], 0.0f, m[3], m[4], m[5], 0.0f, m[6], m[7], m[8], 0.0f, m[9], m[10], m[11], 1.0f};
    const glm::mat4 view = cam.view(), proj = cam.proj(vp.w / std::max(1.0f, vp.h));
    const bool move = track.tool() == TrackView::Tool::Move;
    // encaixe: no girar e no mover pelos eixos do objeto, o do ImGuizmo (passos contados de onde começou); no mover
    // pelos eixos do mundo, a grade absoluta logo abaixo, como no arraste no chão
    const float step = move ? (gizmo_local_ ? track.snap_move : 0.0f) : track.snap_turn;
    const float snap[3] = {step, step, step};

    gizmo_style();
    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetGizmoSizeClipSpace(0.2f);  // fração da altura da vista (o padrão, 0,1, some na tela cheia)
    ImGuizmo::AllowAxisFlip(false);         // a seta aponta sempre para o lado positivo, como na Unity
    ImGuizmo::SetRect(vp.x, vp.y, vp.w, vp.h);
    // um clique que o app já pegou (orbitar, arrastar o objeto) não começa um arraste do gizmo
    ImGuizmo::Enable(!(viewport_drag && ImGui::IsMouseClicked(ImGuiMouseButton_Left)));
    const bool changed = ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj), move ? ImGuizmo::TRANSLATE : ImGuizmo::ROTATE_Y,
                                              gizmo_local_ ? ImGuizmo::LOCAL : ImGuizmo::WORLD, model, nullptr, step > 0.0f ? snap : nullptr);
    const bool using_now = ImGuizmo::IsUsing();
    if (using_now && !gizmo_drag_) {
        gizmo_drag_ = true;
        gizmo_type_ = ImGuizmo::GetActiveHandleType();
        gizmo_open_ = track.begin_change(i);  // falha com outra mudança aberta (um campo do Inspector): só não aplica
        float gy = 0.0f;
        gizmo_y0_ = m[10];
        gizmo_ground_ = track.follow_ground && track.terrain_height(m[9], m[11], m[10] + 1.0f, gy);
        gizmo_ground_off_ = gizmo_ground_ ? m[10] - gy : 0.0f;
    }
    if (gizmo_open_ && changed) {
        float next[kInstFloats] = {model[0], model[1], model[2], model[4], model[5], model[6], model[8], model[9], model[10], model[12], model[13], model[14]};
        const int axes = moved_axes(gizmo_type_);
        if (move && !gizmo_local_ && track.snap_move > 0.0f)
            for (int a = 0; a < 3; ++a)
                if (axes & (1 << a)) next[9 + a] = snapped(next[9 + a], track.snap_move);
        float gy = 0.0f;
        // grudado: se a alça não mexe na altura, ela acompanha o terreno, com a folga de quando pegou o objeto
        if (move && gizmo_ground_ && !(axes & 2) && track.terrain_height(next[9], next[11], gizmo_y0_ + 1.0f, gy)) next[10] = gy + gizmo_ground_off_;
        track.set_matrix(i, next);
    }
    if (!using_now && gizmo_drag_) finish();
}

void EditorUi::render(TrackView* track, const render::OrbitCamera& cam, const glm::mat4& view_proj, const Rect& vp) {
    const int sel = track ? track->selected() : -1;
    ImDrawList* dl = ImGui::GetBackgroundDrawList(ImGui::GetMainViewport());  // atrás dos painéis, no 3D
    if (track && sel >= 0) {
        // caixa local do tipo, transformada, em todas as ferramentas: a seleção se vê mesmo de longe
        const auto i = static_cast<std::size_t>(sel);
        const auto& ty = track->objects().types()[track->instances().type[i]];
        const float* m = track->instances().matrix(i);
        const glm::vec3 lo = ty.empty ? glm::vec3(-1.0f) : ty.lo, hi = ty.empty ? glm::vec3(1.0f) : ty.hi;
        glm::vec2 corner[8];
        bool all = true;
        for (int k = 0; k < 8; ++k) {
            const glm::vec3 p((k & 1) ? hi.x : lo.x, (k & 2) ? hi.y : lo.y, (k & 4) ? hi.z : lo.z);
            const glm::vec3 w = p.x * glm::vec3(m[0], m[1], m[2]) + p.y * glm::vec3(m[3], m[4], m[5]) + p.z * glm::vec3(m[6], m[7], m[8]) +
                                glm::vec3(m[9], m[10], m[11]);
            all = project(view_proj, vp, w, corner[k]) && all;
        }
        if (all) {
            dl->PushClipRect(ImVec2(vp.x, vp.y), ImVec2(vp.x + vp.w, vp.y + vp.h), true);
            const ImU32 col = track->instances().hidden[i] ? IM_COL32(160, 160, 160, 200) : IM_COL32(255, 200, 60, 230);
            static const int edges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
            for (const auto& e : edges)
                dl->AddLine(ImVec2(corner[e[0]].x, corner[e[0]].y), ImVec2(corner[e[1]].x, corner[e[1]].y), col, 1.5f);
            dl->PopClipRect();
        }
    }
    if (track) gizmo(*track, cam, vp);
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

}  // namespace dr2::app
