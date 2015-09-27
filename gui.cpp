#include "gui.h"
#include "job_queue.h"
#include <Windows.h>
#include <Windowsx.h>
#include <stdexcept>
#include <string>
#include <sstream>
#include <cassert>
#include <cmath>
#include <vector>
#include <codecvt>

static HINSTANCE g_hInstance = GetModuleHandle(nullptr);

#include <streambuf>
#include <ostream>
class debug_output_stream_buffer : public std::streambuf {
public:
    static debug_output_stream_buffer* instance() {
        static debug_output_stream_buffer instance_;
        return &instance_;
    }

private:
    debug_output_stream_buffer() = default;
    ~debug_output_stream_buffer() = default;
    thread_local static std::string buffer_;

    virtual std::streamsize xsputn(const char_type* s, std::streamsize count) override {
        for (std::streamsize i = 0; i < count; ++i) {
            buffer_ += s[i];
            if (s[i] == '\n') {
                OutputDebugStringA(buffer_.c_str());
                buffer_.clear();
            }
        }
        return count;
    }

    virtual int_type overflow(int_type ch) override {
        if (ch != traits_type::eof()) {
            const auto c = static_cast<char_type>(ch);
            sputn(&c, 1);
        }
        return ch;
    }
};
thread_local std::string debug_output_stream_buffer::buffer_;

std::ostream debug_output_stream{debug_output_stream_buffer::instance()};

std::wstring utf8_to_utf16(const std::string& utf8)
{
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> utf16conv;
    return utf16conv.from_bytes(utf8.data());
}

std::string utf16_to_utf8(const std::wstring& utf16)
{
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> utf16conv;
    return utf16conv.to_bytes(utf16.data());
}

void repaint(HWND window)
{
    RedrawWindow(window, nullptr, nullptr, RDW_ERASE | RDW_INVALIDATE);
}

class win32_error : public std::runtime_error {
public:
    explicit win32_error(const char* func, unsigned error = GetLastError()) : std::runtime_error(format_message(func, error)) {
    }

private:
    static std::string format_message(const char* func, unsigned error) {
        std::ostringstream oss;
        oss << func << " failed: " << error;
        return oss.str();
    }
};

class window_base {
public:
    window_base(const window_base&) = delete;
    window_base& operator=(const window_base&) = delete;

    HWND hwnd() {
        return hwnd_;
    }

protected:
    window_base() = default;
    virtual ~window_base() = default;
    virtual const wchar_t* class_name() const = 0;

    virtual void paint_content(HDC hdc, const RECT& rcPaint) {
        (void) hdc; (void) rcPaint;
    }

    virtual LRESULT wndproc(UINT msg, WPARAM wparam, LPARAM lparam) {
        switch (msg) {
        case WM_PAINT:
            {
                PAINTSTRUCT ps;
                if (BeginPaint(hwnd(), &ps)) {
                    if (!IsRectEmpty(&ps.rcPaint)) {
                        paint_content(ps.hdc, ps.rcPaint);
                    }
                    EndPaint(hwnd(), &ps);
                }
                return 0;
            }
        }
        return DefWindowProc(hwnd(), msg, wparam, lparam);
    }

    void do_create(const wchar_t* name, DWORD style, int x, int y, int width, int height, HWND parent) {
        do_register_class();
        if (!CreateWindow(class_name(), name, style, x, y, width, height, parent, nullptr, g_hInstance, this)) {
            throw win32_error("CreateWindow");
        }
        assert(IsWindow(hwnd()));
    }

private:
    HWND hwnd_;

    void do_register_class() {
        WNDCLASS wc ={0,};
        wc.style         = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc   = s_wndproc;
        wc.cbClsExtra    = 0;
        wc.cbWndExtra    = 0;
        wc.hInstance     = g_hInstance;
        wc.hIcon         = LoadIcon(nullptr, IDI_APPLICATION);
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszMenuName  = nullptr;
        wc.lpszClassName = class_name();

        if (!RegisterClass(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            throw win32_error("RegisterClass");
        }
    }

    static LRESULT CALLBACK s_wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
        window_base* self = nullptr;
        if (msg == WM_NCCREATE) {
            auto cs = *reinterpret_cast<const CREATESTRUCT*>(lparam);
            self = reinterpret_cast<window_base*>(cs.lpCreateParams);
            self->hwnd_ = hwnd;
            SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        } else {
            self = reinterpret_cast<window_base*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
        }

        auto ret = self ? self->wndproc(msg, wparam, lparam) : DefWindowProc(hwnd, msg, wparam, lparam);
        
        if (msg == WM_NCDESTROY) {
            SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
            assert(self);
            self->hwnd_ = nullptr;
            delete self;
        }
        return ret;
    }
};

struct gdi_obj_deleter {
    void operator()(void* obj) {
        DeleteObject(obj);
    }
};
using gdi_obj = std::unique_ptr<void, gdi_obj_deleter>;

struct dc_obj_deleter {
    using pointer = HDC;
    void operator()(HDC obj) {
        DeleteDC(obj);
    }
};
using dc_obj = std::unique_ptr<HDC, dc_obj_deleter>;

class window_dc {
public:
    window_dc(HWND hwnd) : hwnd_(hwnd), hdc_(GetDC(hwnd_)) {
        if (!hdc_) throw win32_error("GetDC");
    }
    ~window_dc() {
        ReleaseDC(hwnd_, hdc_);
    }

    HDC hdc() { return hdc_; }

private:
    HWND hwnd_;
    HDC  hdc_;

    window_dc(const window_dc&) = delete;
    window_dc& operator=(const window_dc&) = delete;
};

gdi_obj default_font(int height = 12)
{
    const wchar_t* const face_name = L"MS Shell Dlg 2";
    HFONT font = CreateFont(height, 0, 0, 0, FW_DONTCARE, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, FF_DONTCARE, face_name);
    if (!font) {
        throw win32_error("CreateFont");
    }
    return gdi_obj{font};
}

class knob_impl : public window_base, public knob {
public:
    static knob_impl* create(int x, int y, int width, int height, HWND parent) {
        auto w = std::unique_ptr<knob_impl>(new knob_impl{});
        w->do_create(L"", WS_CHILD|WS_VISIBLE, x, y, width, height, parent);
        return w.release();
    }

private:
    double value_;
    std::vector<observer_type> observers_;

    knob_impl() : value_(0) {
    }

    virtual double do_value() const override {
        return value_;
    }

    virtual void do_value(double x) override {
        assert(x >= 0 && x <= 1.0);
        value_ = x;
        for (const auto& obs : observers_) {
            obs(value_);
        }
        repaint(hwnd());
    }

    virtual void do_add_observer(observer_type o) override {
        observers_.push_back(o);
    }

    // The indicator moves clocwise (from 270-sep ... 0 ... 270+sep)
    //           /|\
    //          / | \
    //         /__|__\
    //    270-sep 270 270+sep (=-90-2*sep)

    static constexpr double pi = 3.14159265359;
    static constexpr double sep = 20;
    static constexpr double min_deg = 270-sep;

    static double value_to_ang(double val) {
        //constexpr double max_deg = 270+sep;
        const double deg = min_deg - val * (360-2*sep);
        return deg * (pi / 180.0);
    }

    static double ang_to_value(const double ang) {
        assert(ang >= -pi && ang <= pi);
        auto deg = ang * 180.0 / pi;  // to degrees
        if (deg < -90) deg = deg+360; // move valid range [-180; 180] -> [-90; 270]
        deg -= min_deg;               // center around our zero point
        deg /= -(360-2*sep);          // apply 
        return deg;
    }

    virtual void paint_content(HDC hdc, const RECT&) override {
        RECT rcClient;
        GetClientRect(hwnd(), &rcClient);
        auto border = gdi_obj{CreatePen(PS_SOLID, 2, RGB(0, 0, 0))};
        auto marker_line = gdi_obj{CreatePen(PS_SOLID, 4, RGB(0, 0, 0))};
        auto fill = gdi_obj{CreateSolidBrush(capturing_ ? RGB(200, 150, 150) : RGB(200, 40, 40))};

        const auto w = rcClient.right - rcClient.left;
        const auto h = rcClient.bottom - rcClient.top;

        const auto cx = rcClient.left + w/2;
        const auto cy = rcClient.left + h/2;

        auto old_pen = SelectPen(hdc, border.get());
        auto old_brush = SelectBrush(hdc, fill.get());
        Ellipse(hdc, rcClient.left, rcClient.top, rcClient.right, rcClient.bottom);

        SelectBrush(hdc, marker_line.get());
        MoveToEx(hdc, cx, cy, nullptr);

        const auto ang = value_to_ang(value());

        LineTo(hdc, static_cast<int>(cx + (w/2)*cos(ang)), static_cast<int>(cy - (h/2)*sin(ang)));
        SelectPen(hdc, old_pen);
        SelectBrush(hdc, old_brush);
    }

    int lbutton_x_ = -1;
    int lbutton_y_ = -1;
    bool capturing_ = false;

    void set_value_from_coords(int x, int y) {
        assert(capturing_);
        const auto dx = x - lbutton_x_;
        const auto dy = y - lbutton_y_;

        double n =  ang_to_value(atan2(-dy, dx));
        if (n < 0.0) n = 0.0;
        if (n > 1.0) n = 1.0;
        value(n);
    }

    void on_lbutton_down(int x, int y) {
        //debug_output_stream << (int)capturing_ << " on_lbutton_down " << x << " " << y << "\n";
        assert(!capturing_);
        SetCapture(hwnd());
        lbutton_x_ = x;
        lbutton_y_ = y;
        capturing_ = true;
        repaint(hwnd());
    }

    void on_lbutton_up(int x, int y) {
        if (capturing_) {
            //debug_output_stream << (int)capturing_ << " on_lbutton_up " << x << " " << y << "\n";
            ReleaseCapture();
            set_value_from_coords(x, y);
            capturing_ = false;
        }
    }

    void on_mouse_move(int x, int y) {
        //debug_output_stream << (int)capturing_ << " on_mouse_move " << x << " " << y << " " << flags << "\n";
        if (capturing_) {
            set_value_from_coords(x, y);
        }
    }

    virtual LRESULT wndproc(UINT msg, WPARAM wparam, LPARAM lparam) override {
        switch (msg) {
        case WM_LBUTTONDOWN:
            on_lbutton_down(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
            return 0;
        case WM_LBUTTONUP:
            on_lbutton_up(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
            return 0;
        case WM_MOUSEMOVE:
            on_mouse_move(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
            return 0;
        }
        return __super::wndproc(msg, wparam, lparam);
    }

    virtual const wchar_t* class_name() const override {
        return L"knob";
    }
};

template<typename ObjectType>
class gdi_selector {
public:
    gdi_selector(HDC hdc, ObjectType obj) : hdc_(hdc), old_(SelectObject(hdc_, static_cast<HGDIOBJ>(obj))) {
    }

    ~gdi_selector() {
        SelectObject(hdc_, old_);
    }

    gdi_selector(const gdi_selector&) = delete;
    gdi_selector& operator=(const gdi_selector&) = delete;

private:
    HDC     hdc_;
    HGDIOBJ old_;
};

using font_selector = gdi_selector<HFONT>;

class label_impl : public window_base, public text_window {
public:
    static label_impl* create(const std::wstring& text, int x, int y, int width, int height, HWND parent) {
        auto w = std::unique_ptr<label_impl>(new label_impl{});
        w->do_create(text.c_str(), WS_CHILD|WS_VISIBLE, x, y, width, height, parent);
        return w.release();
    }

    void set_font(gdi_obj& font) {
        font_ = static_cast<HFONT>(font.get());
    }

    ~label_impl() = default;

    label_impl(const label_impl&) = delete;
    label_impl& operator=(const label_impl&) = delete;
private:
    HFONT font_;

    explicit label_impl() : font_(nullptr) {
    }

    virtual const wchar_t* class_name() const override {
        return L"label";
    }

    virtual void paint_content(HDC hdc, const RECT&) override {

        wchar_t text[100];
        const int textlen = GetWindowTextW(hwnd(), text, _countof(text));
        if (textlen <= 0) return;

        RECT rcClient;
        GetClientRect(hwnd(), &rcClient);
        font_selector font_guard{hdc, font_};
        DrawTextW(hdc, text, textlen, &rcClient, DT_CENTER);
    }

    virtual std::string do_text() const override {
        assert(false);
        return "";
    }

    virtual void do_text(const std::string& in) override {
        SetWindowText(hwnd(), utf8_to_utf16(in).c_str());
        repaint(hwnd());
    }
};

class bitmap_window_impl : public window_base, public bitmap_window {
public:
    static bitmap_window_impl* create(int x, int y, int width, int height, HWND parent) {
        auto w = std::unique_ptr<bitmap_window_impl>(new bitmap_window_impl{width, height});
        w->do_create(L"", WS_CHILD|WS_VISIBLE, x, y, width, height, parent);
        return w.release();
    }

private:
    int                   width_;
    int                   height_;
    dc_obj                mem_dc_;
    gdi_obj               mem_bitmap_;

    bitmap_window_impl(int width, int height) : width_(width), height_(height) {
    }

    virtual const wchar_t* class_name() const override {
        return L"bitmap_window_impl";
    }

    virtual void paint_content(HDC hdc, const RECT&) override {
        BitBlt(hdc, 0, 0, width_, height_, mem_dc_.get(), 0, 0, SRCCOPY);
    }

    void on_create() {
        window_dc wdc{hwnd()};
        HDC hdc = wdc.hdc();
        mem_dc_.reset(CreateCompatibleDC(hdc));
        if (!mem_dc_) {
            throw win32_error("CreateCompatibleDC");
        }
        mem_bitmap_.reset(CreateCompatibleBitmap(hdc, width_, height_));
        if (!mem_bitmap_) {
            throw win32_error("CreateCompatibleBitmap");
        }
        if (!SelectObject(mem_dc_.get(), mem_bitmap_.get())) {
            throw win32_error("SelectObject");
        }
    }

    virtual LRESULT wndproc(UINT msg, WPARAM wparam, LPARAM lparam) override {
        switch (msg) {
        case WM_CREATE:
            on_create();
            return 0;
        }
        return __super::wndproc(msg, wparam, lparam);
    }

    virtual int do_width() const override {
        return width_;
    }

    virtual int do_height() const override {
        return height_;
    }

    virtual void do_update_pixels(const unsigned* pixels) override {
        BITMAPINFO bmi;
        ZeroMemory(&bmi, sizeof(bmi));
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth  = width_;
        bmi.bmiHeader.biHeight = height_;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        SetDIBits(mem_dc_.get(), static_cast<HBITMAP>(mem_bitmap_.get()), 0, height_, pixels, &bmi, DIB_RGB_COLORS);
        repaint(hwnd());
    }
};

class main_window : public window_base {
public:
    static main_window* create(int width, int height) {
        auto w = std::unique_ptr<main_window>(new main_window{});
        w->do_create(L"Main Window", WS_OVERLAPPEDWINDOW, 20, 20, width, height, nullptr);
        return w.release();
    }
private:

    main_window() {
    }

    HBRUSH on_ctl_color(HDC hdc, HWND hStatic) {
        (void)hdc; (void)hStatic;
        //SetBkMode(hdc, TRANSPARENT);
        //return GetStockBrush(HOLLOW_BRUSH);
        return GetSysColorBrush(COLOR_WINDOW);
    }

    virtual LRESULT wndproc(UINT msg, WPARAM wparam, LPARAM lparam) override {
        switch (msg) {
        case WM_DESTROY:
            PostQuitMessage(0);
            break;
        case WM_CTLCOLORSTATIC:
            return reinterpret_cast<LRESULT>(on_ctl_color(reinterpret_cast<HDC>(wparam), reinterpret_cast<HWND>(lparam)));
        }
        return __super::wndproc(msg, wparam, lparam);
    }

    virtual const wchar_t* class_name() const override {
        return L"main_window";
    }
};

class gui::impl {
public:
    impl(int width, int height)
        : on_idle_(nullptr)
        , font_(default_font())
        , main_window_(*main_window::create(width, height)) {
    }

    ~impl() {
    }

    void add_job(job_type job) {
        job_queue_.push(job);
        PostThreadMessage(GetWindowThreadProcessId(main_window_.hwnd(), nullptr), WM_NULL, 0, 0);
    }

    void main_loop() {
        MSG msg;
        HWND hMainWindow = main_window_.hwnd();
        ShowWindow(hMainWindow, SW_SHOW);
        for (;;) {

            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
#if 0
                char title[256] = "(null)";
                char clazz[256] = "(null)";
                if (msg.hwnd) {
                    GetWindowTextA(msg.hwnd, title, _countof(title));
                    GetClassNameA(msg.hwnd, clazz, _countof(clazz));
                }

                switch (msg.message) {
#define P(m, e) case m: debug_output_stream << msg.hwnd << " " << title << " " << clazz << " " << #m << " " << e << " act " << GetActiveWindow() << " fg " << GetForegroundWindow() <<  std::endl; break
#define X(m, e) case m: break
                    X(WM_NULL, "");
                    P(WM_SETFOCUS, "");
                    P(WM_KILLFOCUS, "");
                    X(WM_PAINT, "");
                    P(WM_MOUSEACTIVATE, "");
                    X(WM_NCMOUSEMOVE, "");
                    X(WM_KEYDOWN, "");
                    X(WM_KEYUP, "");
                    P(WM_CHAR, "");
                    X(WM_TIMER, "");
                    X(WM_MOUSEMOVE   , (msg.wParam & MK_LBUTTON) << " " << GET_X_LPARAM(msg.lParam) << " " << GET_Y_LPARAM(msg.lParam));
                    P(WM_LBUTTONDOWN , (msg.wParam & MK_LBUTTON) << " " << GET_X_LPARAM(msg.lParam) << " " << GET_Y_LPARAM(msg.lParam));
                    P(WM_LBUTTONUP   , (msg.wParam & MK_LBUTTON) << " " << GET_X_LPARAM(msg.lParam) << " " << GET_Y_LPARAM(msg.lParam));
                    X(WM_NCMOUSEHOVER, "");
                    X(WM_NCMOUSELEAVE, "");

                    X(/*WM_DWMNCRENDERINGCHANGED*/0x031F, "");
                default:
                    debug_output_stream << msg.hwnd << " " << title << " " << clazz << " " << std::hex << " " << msg.message << std::dec << std::endl;
#undef X
#undef P
                }
#endif

                if (msg.message == WM_QUIT) {
                    return;
                }

                bool handled = false;
                if (IsWindow(hMainWindow)) {
                    job_queue_.execute_all();
                    auto notify_key_listeners = [&] (bool pressed, WPARAM vk) {
                        for (auto& l: key_listeners_) {
                            l(pressed, static_cast<int>(vk));
                            handled = true;
                        }
                    };
                    if (msg.message == WM_KEYUP) {
                        notify_key_listeners(false, msg.wParam);
                        // HACK: Close main window on escape
                        if (msg.wParam == VK_ESCAPE) SendMessage(main_window_.hwnd(), WM_CLOSE, 0, 0);
                    } else if (msg.message == WM_KEYDOWN) {
                        if (((msg.lParam>>30) & 1) == 0) { // Only notify if key was up (to avoid repeats)
                            notify_key_listeners(true, msg.wParam);
                        }
                    }
                }
                //TranslateMessage(&msg); -- We don't care about WM_(SYS)(DEAD)CHAR
                if (!handled) DispatchMessage(&msg);
            }

            if (on_idle_) on_idle_();
        }
    }

    void set_on_idle(const std::function<void(void)>& on_idle) {
        on_idle_ = on_idle;
    }

    knob& make_knob(int x, int y, int width, int height) {
        return *knob_impl::create(x, y, width, height, main_window_.hwnd());
    }

    text_window& make_label(const std::string& text, int x, int y, int width, int height) {
        auto& label = *label_impl::create(utf8_to_utf16(text), x, y, width, height, main_window_.hwnd());
        label.set_font(font_);
        return label;
    }

    bitmap_window& make_bitmap_window(int x, int y, int width, int height) {
        return *bitmap_window_impl::create(x, y, width, height, main_window_.hwnd());
    }

    void add_key_listener(key_listener_type key_listener) {
        key_listeners_.push_back(std::move(key_listener));
    }

private:
    std::function<void(void)> on_idle_;
    gdi_obj font_;
    main_window& main_window_;
    job_queue job_queue_;
    std::vector<key_listener_type> key_listeners_;
};

gui::gui(int width, int height)
    : impl_(new impl(width, height))
{
}

gui::~gui() = default;

void gui::add_job(job_type job)
{
    impl_->add_job(job);
}

void gui::main_loop()
{
    impl_->main_loop();
}

void gui::add_key_listener(key_listener_type key_listener)
{
    impl_->add_key_listener(key_listener);
}

void gui::set_on_idle(const std::function<void(void)>& on_idle)
{
    impl_->set_on_idle(on_idle);
}

knob& gui::make_knob(int x, int y, int width, int height)
{
    return impl_->make_knob(x, y, width, height);
}

text_window& gui::make_label(const std::string& text, int x, int y, int width, int height)
{
    return impl_->make_label(text, x, y, width, height);
}

bitmap_window& gui::make_bitmap_window(int x, int y, int width, int height)
{
    return impl_->make_bitmap_window(x, y, width, height);
}
