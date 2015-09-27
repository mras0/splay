#ifndef GUI_H_INCLUDED
#define GUI_H_INCLUDED

#include <memory>
#include <string>
#include <functional>
#include <ostream>

extern std::ostream debug_output_stream;

class knob {
public:
    double value() const {
        return do_value();
    }

    void value(double val) {
        do_value(val);
    }

    using observer_type = std::function<void(double)>;

    void add_observer(observer_type o) {
        do_add_observer(o);
    }

protected:
    virtual ~knob() = default;

private:
    virtual double do_value() const = 0;
    virtual void do_value(double val) = 0;
    virtual void do_add_observer(observer_type o) = 0;
};

class text_window {
public:
    std::string text() const {
        return do_text();
    }

    void text(const std::string& val) {
        do_text(val);
    }

private:
    virtual std::string do_text() const = 0;
    virtual void do_text(const std::string& val) = 0;
};

class bitmap_window {
public:
    int width() const {
        return do_width();
    }

    int height() const {
        return do_height(); 
    }

    void update_pixels(const unsigned* pixels) {
        do_update_pixels(pixels);
    }
private:
    virtual int do_width() const = 0;
    virtual int do_height() const = 0;
    virtual void do_update_pixels(const unsigned*) = 0;
};

class gui {
public:
    explicit gui(int width, int height);
    ~gui();

    gui(const gui&) = delete;
    gui& operator=(const gui&) = delete;

    using job_type = std::function<void (void)>;
    using key_listener_type = std::function<void(bool pressed, int vk)>;

    void add_job(job_type job);

    void main_loop();

    void set_on_idle(const std::function<void(void)>& on_idle);

    void add_key_listener(key_listener_type key_listener);
    
    knob& make_knob(int x, int y, int width, int height);
    text_window& make_label(const std::string& text, int x, int y, int width, int height);
    bitmap_window& make_bitmap_window(int x, int y, int width, int height);

private:
    class impl;
    std::unique_ptr<impl> impl_;
};

#endif