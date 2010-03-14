// Ares, a tactical space combat game.
// Copyright (C) 1997, 1999-2001, 2008 Nathan Lamont
// 
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <sys/socket.h>

#include "sfz/BinaryReader.hpp"
#include "sfz/BinaryWriter.hpp"
#include "sfz/Exception.hpp"
#include "sfz/Format.hpp"
#include "Card.hpp"
#include "CardStack.hpp"
#include "Casts.hpp"
#include "ColorTable.hpp"
#include "Error.hpp"
#include "Event.hpp"
#include "FakeDrawing.hpp"
#include "Threading.hpp"
#include "Time.hpp"
#include "VncServer.hpp"

using sfz::BinaryReader;
using sfz::BinaryWriter;
using sfz::Bytes;
using sfz::BytesPiece;
using sfz::Exception;
using sfz::StringPiece;
using sfz::scoped_ptr;
using sfz::latin1_encoding;
using sfz::print;

namespace antares {

namespace {

class SocketBinaryReader : public BinaryReader {
  public:
    SocketBinaryReader(int fd)
        : _fd(fd) { }

    virtual bool done() const {
        return false;
    }

  protected:
    virtual void read_bytes(uint8_t* bytes, size_t len) {
        while (_buffer.size() < len) {
            uint8_t more[1024];
            ssize_t recd = recv(_fd, more, 1024, 0);
            if (recd <= 0) {
                throw Exception("TODO(sfiera): posix error message");
            }
            _buffer.append(more, recd);
        }
        memcpy(bytes, _buffer.data(), len);
        Bytes new_buffer(BytesPiece(_buffer).substr(len));
        _buffer.swap(&new_buffer);
    }

  private:
    int _fd;
    Bytes _buffer;
};

class SocketBinaryWriter : public BinaryWriter {
  public:
    SocketBinaryWriter(int fd)
        : _fd(fd) { }

    void flush() {
        if (!_buffer.empty()) {
            size_t sent = send(_fd, _buffer.data(), _buffer.size(), 0);
            if (sent != _buffer.size()) {
                throw Exception("TODO(sfiera): posix error message");
            }
            _buffer.clear();
        }
    }

  protected:
    virtual void write_bytes(const uint8_t* bytes, size_t len) {
        _buffer.append(bytes, len);
    }

  private:
    int _fd;
    Bytes _buffer;
};

int64_t usecs() {
    timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000ll + tv.tv_usec;
}

int listen_on(int port) {
    int one = 1;
    int sock;
    struct sockaddr_in addr;

    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        throw Exception("TODO(sfiera): posix error message");
    }
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        throw Exception("TODO(sfiera): posix error message");
    }
    if (listen(sock, 5) < 0) {
        throw Exception("TODO(sfiera): posix error message");
    }

    return sock;
}

int accept_on(int sock) {
    int one = 1;
    sockaddr addr;
    socklen_t addrlen;
    int fd = accept(sock, &addr, &addrlen);
    if (fd < 0) {
        throw Exception("TODO(sfiera): posix error message");
    }

    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    return fd;
}

// Common Messages.

struct PixelFormat {
    uint8_t bits_per_pixel;
    uint8_t depth;
    uint8_t big_endian;
    uint8_t true_color;
    uint16_t red_max;
    uint16_t green_max;
    uint16_t blue_max;
    uint8_t red_shift;
    uint8_t green_shift;
    uint8_t blue_shift;
    uint8_t unused[3];

    void read(BinaryReader* bin) {
        bin->read(&bits_per_pixel);
        bin->read(&depth);
        bin->read(&big_endian);
        bin->read(&true_color);
        bin->read(&red_max);
        bin->read(&green_max);
        bin->read(&blue_max);
        bin->read(&red_shift);
        bin->read(&green_shift);
        bin->read(&blue_shift);
        bin->discard(3);
    }

    void write(BinaryWriter* bin) const {
        bin->write(bits_per_pixel);
        bin->write(depth);
        bin->write(big_endian);
        bin->write(true_color);
        bin->write(red_max);
        bin->write(green_max);
        bin->write(blue_max);
        bin->write(red_shift);
        bin->write(green_shift);
        bin->write(blue_shift);
        bin->pad(3);
    }
};

// 6.1. Handshaking Messages.

struct ProtocolVersion {
    char version[12];

    void read(BinaryReader* bin) {
        bin->read(version, 12);
    }

    void write(BinaryWriter* bin) const {
        bin->write(version, 12);
    }
};

struct SecurityMessage {
    uint8_t number_of_security_types;

    void read(BinaryReader* bin) {
        bin->read(&number_of_security_types);
    }

    void write(BinaryWriter* bin) const {
        bin->write(number_of_security_types);
    }
};

struct SecurityResultMessage {
    uint32_t status;

    void read(BinaryReader* bin) {
        bin->read(&status);
    }

    void write(BinaryWriter* bin) const {
        bin->write(status);
    }
};

// 6.3. Initialization Messages.

struct ClientInitMessage {
    uint8_t shared_flag;

    void read(BinaryReader* bin) {
        bin->read(&shared_flag);
    }

    void write(BinaryWriter* bin) const {
        bin->write(shared_flag);
    }
};

struct ServerInitMessage {
    uint16_t width;
    uint16_t height;
    PixelFormat format;
    uint32_t name_length;

    void read(BinaryReader* bin) {
        bin->read(&width);
        bin->read(&height);
        bin->read(&format);
        bin->read(&name_length);
    }

    void write(BinaryWriter* bin) const {
        bin->write(width);
        bin->write(height);
        bin->write(format);
        bin->write(name_length);
    }
};

// 6.4. Client-to-Server Messages.

enum ClientToServerMessageType {
    SET_PIXEL_FORMAT = 0,
    SET_ENCODINGS = 2,
    FRAMEBUFFER_UPDATE_REQUEST = 3,
    KEY_EVENT = 4,
    POINTER_EVENT = 5,
    CLIENT_CUT_TEXT = 6,
};

struct SetPixelFormatMessage {
    // uint8_t message_type;
    uint8_t unused[3];
    PixelFormat format;

    void read(BinaryReader* bin) {
        // bin->read(&message_type);
        bin->discard(3);
        bin->read(&format);
    }

    void write(BinaryWriter* bin) const {
        // bin->write(message_type);
        bin->pad(3);
        bin->write(format);
    }
};

struct SetEncodingsMessage {
    // uint8_t message_type;
    uint8_t unused;
    uint16_t number_of_encodings;

    void read(BinaryReader* bin) {
        // bin->read(&message_type);
        bin->discard(1);
        bin->read(&number_of_encodings);
    }

    void write(BinaryWriter* bin) const {
        // bin->write(message_type);
        bin->pad(1);
        bin->write(number_of_encodings);
    }
};

struct FramebufferUpdateRequestMessage {
    // uint8_t message_type;
    uint8_t incremental;
    uint16_t x;
    uint16_t y;
    uint16_t w;
    uint16_t h;

    void read(BinaryReader* bin) {
        // bin->read(&message_type);
        bin->read(&incremental);
        bin->read(&x);
        bin->read(&y);
        bin->read(&w);
        bin->read(&h);
    }

    void write(BinaryWriter* bin) const {
        // bin->write(message_type);
        bin->write(incremental);
        bin->write(x);
        bin->write(y);
        bin->write(w);
        bin->write(h);
    }
};

struct KeyEventMessage {
    // uint8_t message_type;
    uint8_t down_flag;
    uint8_t unused[2];
    uint32_t key;

    void read(BinaryReader* bin) {
        // bin->read(&message_type);
        bin->read(&down_flag);
        bin->discard(2);
        bin->read(&key);
    }

    void write(BinaryWriter* bin) const {
        // bin->write(message_type);
        bin->write(down_flag);
        bin->pad(2);
        bin->write(key);
    }
};

struct PointerEventMessage {
    // uint8_t message_type;
    uint8_t button_mask;
    uint16_t x_position;
    uint16_t y_position;

    void read(BinaryReader* bin) {
        // bin->read(&message_type);
        bin->read(&button_mask);
        bin->read(&x_position);
        bin->read(&y_position);
    }

    void write(BinaryWriter* bin) const {
        // bin->write(message_type);
        bin->write(button_mask);
        bin->write(x_position);
        bin->write(y_position);
    }
};

struct ClientCutTextMessage {
    // uint8_t message_type;
    uint8_t unused[3];
    uint32_t length;

    void read(BinaryReader* bin) {
        // bin->read(&message_type);
        bin->discard(3);
        bin->read(&length);
    }

    void write(BinaryWriter* bin) const {
        // bin->write(message_type);
        bin->pad(3);
        bin->write(length);
    }
};

// 6.5. Server-to-Client Messages.

enum ServerToClientMessageType {
    FRAMEBUFFER_UPDATE = 0,
    SET_COLOR_MAP_ENTRIES = 1,
    BELL = 2,
    SERVER_CUT_TEXT = 3,
};

struct FramebufferUpdateMessage {
    // uint8_t message_type;
    uint8_t padding;
    uint16_t number_of_rectangles;

    void read(BinaryReader* bin) {
        // bin->read(&message_type);
        bin->discard(1);
        bin->read(&number_of_rectangles);
    }

    void write(BinaryWriter* bin) const {
        // bin->write(message_type);
        bin->pad(1);
        bin->write(number_of_rectangles);
    }
};

struct FramebufferUpdateRectangle {
    uint16_t x_position;
    uint16_t y_position;
    uint16_t width;
    uint16_t height;
    int32_t encoding_type;

    void read(BinaryReader* bin) {
        bin->read(&x_position);
        bin->read(&y_position);
        bin->read(&width);
        bin->read(&height);
        bin->read(&encoding_type);
    }

    void write(BinaryWriter* bin) const {
        bin->write(x_position);
        bin->write(y_position);
        bin->write(width);
        bin->write(height);
        bin->write(encoding_type);
    }
};

struct SetColorMapEntriesMessage {
    // uint8_t message_type;
    uint8_t padding;
    uint16_t first_color;
    uint16_t number_of_colors;

    void read(BinaryReader* bin) {
        // bin->read(&message_type);
        bin->discard(1);
        bin->read(&first_color);
        bin->read(&number_of_colors);
    }

    void write(BinaryWriter* bin) const {
        // bin->write(message_type);
        bin->pad(1);
        bin->write(first_color);
        bin->write(number_of_colors);
    }
};

struct SetColorMapEntriesColor {
    uint16_t red;
    uint16_t green;
    uint16_t blue;

    void read(BinaryReader* bin) {
        bin->read(&red);
        bin->read(&green);
        bin->read(&blue);
    }

    void write(BinaryWriter* bin) const {
        bin->write(red);
        bin->write(green);
        bin->write(blue);
    }
};

struct ServerCutTextMessage {
    // uint8_t message_type;
    uint8_t unused[3];
    uint32_t length;

    void read(BinaryReader* bin) {
        // bin->read(&message_type);
        bin->discard(3);
        bin->read(&length);
    }

    void write(BinaryWriter* bin) const {
        // bin->write(message_type);
        bin->pad(3);
        bin->write(length);
    }
};

// 6.6 Encodings.

enum {
    RAW = 0,
};

}  // namespace

bool VncVideoDriver::vnc_poll(int64_t timeout) {
    int width = gRealWorld->bounds().right;
    int height = gRealWorld->bounds().bottom;
    SocketBinaryWriter out(_socket.get());
    int64_t stop_time = usecs() + timeout;
    bool unchanged = false;

    do {
        timeout = std::max(0ll, stop_time - usecs());
        timeval tv;
        tv.tv_sec = timeout / 1000000ll;
        tv.tv_usec = timeout % 1000000ll;

        fd_set read;
        fd_set write;
        fd_set error;
        FD_ZERO(&read);
        FD_ZERO(&write);
        FD_ZERO(&error);
        FD_SET(_socket.get(), &read);

        if (select(_socket.get() + 1, &read, &write, &error, &tv) > 0) {
            uint8_t client_message_type;
            _in->read(&client_message_type);
            switch (client_message_type) {
            case SET_PIXEL_FORMAT:
                {
                    SetPixelFormatMessage msg;
                    _in->read(&msg);
                }
                break;

            case SET_ENCODINGS:
                {
                    SetEncodingsMessage msg;
                    _in->read(&msg);
                    for (int i = 0; i < msg.number_of_encodings; ++i) {
                        int32_t encoding_type;
                        _in->read(&encoding_type);
                    }
                }
                break;

            case FRAMEBUFFER_UPDATE_REQUEST:
                {
                    FramebufferUpdateRequestMessage request;
                    _in->read(&request);

                    FramebufferUpdateMessage response;
                    uint8_t server_message_type = FRAMEBUFFER_UPDATE;
                    if (unchanged) {
                        response.number_of_rectangles = 0;
                        out.write(server_message_type);
                        out.write(response);
                    } else {
                        response.number_of_rectangles = 1;

                        FramebufferUpdateRectangle rect;
                        rect.x_position = 0;
                        rect.y_position = 0;
                        rect.width = width;
                        rect.height = height;
                        rect.encoding_type = RAW;

                        out.write(server_message_type);
                        out.write(response);
                        out.write(rect);
                        if (gRealWorld->transition_fraction() == 0.0) {
                            out.write(
                                    reinterpret_cast<const char*>(gRealWorld->bytes()),
                                    width * height * 4);
                        } else {
                            double f = gRealWorld->transition_fraction();
                            double g = 1.0 - f;
                            const RgbColor& to = gRealWorld->transition_to();
                            for (int i = 0; i < gRealWorld->bounds().area(); ++i) {
                                const RgbColor& from = gRealWorld->bytes()[i];
                                out.pad(1);
                                out.write(implicit_cast<uint8_t>(to.red * f + from.red * g));
                                out.write(implicit_cast<uint8_t>(to.green * f + from.green * g));
                                out.write(implicit_cast<uint8_t>(to.blue * f + from.blue * g));
                            }
                        }

                        unchanged = true;
                    }
                }
                break;

            case KEY_EVENT:
                {
                    KeyEventMessage msg;
                    _in->read(&msg);
                    print(1, "key {0} {1}\n", msg.key, msg.down_flag);

                    if (_key_map.find(msg.key) == _key_map.end()) {
                        fail("unknown key");
                    }

                    if (msg.down_flag) {
                        _event_queue.push(new KeyDownEvent(_key_map[msg.key]));
                    } else {
                        _event_queue.push(new KeyUpEvent(_key_map[msg.key]));
                    }
                    // _event_queue.push(new KeyUpEvent(_key_map[msg.key]));
                }
                break;

            case POINTER_EVENT:
                {
                    PointerEventMessage msg;
                    _in->read(&msg);

                    if (_button != implicit_cast<bool>(msg.button_mask & 0x1)) {
                        Point where(msg.x_position, msg.y_position);
                        if (_button) {
                            _event_queue.push(new MouseUpEvent(0, where));
                        } else {
                            _event_queue.push(new MouseDownEvent(0, where));
                        }
                    }

                    _button = msg.button_mask & 0x1;
                    _mouse.h = msg.x_position;
                    _mouse.v = msg.y_position;
                }
                break;

            case CLIENT_CUT_TEXT:
                {
                    ClientCutTextMessage msg;
                    _in->read(&msg);
                    for (size_t i = 0; i < msg.length; ++i) {
                        char c;
                        _in->read(&c, 1);
                    }
                }
                break;

            default:
                {
                    fail("Received %d", implicit_cast<int>(client_message_type));
                }
            }
        }
        out.flush();
    } while (_event_queue.empty() && usecs() < stop_time);

    return false;
}

VncVideoDriver::VncVideoDriver(int port)
        : _start_time(usecs()),
          _listen(listen_on(port)),
          _socket(accept_on(_listen.get())),
          _button(false),
          _in(new SocketBinaryReader(_socket.get())) {
    _mouse.h = 0;
    _mouse.v = 0;

    SocketBinaryWriter out(_socket.get());
    int width = gRealWorld->bounds().right;
    int height = gRealWorld->bounds().bottom;

    {
        // Negotiate version of RFB protocol.  Only 3.8 is offered or accepted.
        ProtocolVersion version;
        strncpy(version.version, "RFB 003.008\n", sizeof(ProtocolVersion));
        out.write(version);
        out.flush();
        _in->read(&version);
        if (memcmp(version.version, "RFB 003.008\n", sizeof(ProtocolVersion)) != 0) {
            throw Exception("unacceptable client version {0}", StringPiece(
                        BytesPiece(reinterpret_cast<const uint8_t*>(version.version), 12),
                        latin1_encoding()));
        }
    }

    {
        // Negotiate security.  No security is provided.
        SecurityMessage security;
        security.number_of_security_types = 1;
        uint8_t security_types[1] = { '\1' };  // None.
        out.write(security);
        out.write(security_types, security.number_of_security_types);
        out.flush();

        uint8_t selected_security;
        _in->read(&selected_security);
        if (selected_security != '\1') {
            throw Exception("unacceptable security {0}", selected_security);
        }

        SecurityResultMessage result;
        result.status = 0;  // OK.
        out.write(result);
        out.flush();
    }

    {
        // Initialize connection.
        ClientInitMessage client_init;
        _in->read(&client_init);

        const char* const name = "Antares";

        ServerInitMessage server_init;
        server_init.width = width;
        server_init.height = height;
        server_init.format.bits_per_pixel = 32;
        server_init.format.depth = 24;
        server_init.format.big_endian = 1;
        server_init.format.true_color = 1;
        server_init.format.red_max = 255;
        server_init.format.green_max = 255;
        server_init.format.blue_max = 255;
        server_init.format.red_shift = 8;
        server_init.format.green_shift = 16;
        server_init.format.blue_shift = 24;
        server_init.name_length = strlen(name);

        out.write(server_init);
        out.write(name, strlen(name));
        out.flush();
    }
    vnc_poll(0);

    _key_map['a'] = Keys::A;
    _key_map['b'] = Keys::B;
    _key_map['c'] = Keys::C;
    _key_map['d'] = Keys::D;
    _key_map['e'] = Keys::E;
    _key_map['f'] = Keys::F;
    _key_map['g'] = Keys::G;
    _key_map['h'] = Keys::H;
    _key_map['i'] = Keys::I;
    _key_map['j'] = Keys::J;
    _key_map['k'] = Keys::K;
    _key_map['l'] = Keys::L;
    _key_map['m'] = Keys::M;
    _key_map['n'] = Keys::N;
    _key_map['o'] = Keys::O;
    _key_map['p'] = Keys::P;
    _key_map['q'] = Keys::Q;
    _key_map['r'] = Keys::R;
    _key_map['s'] = Keys::S;
    _key_map['t'] = Keys::T;
    _key_map['u'] = Keys::U;
    _key_map['v'] = Keys::V;
    _key_map['w'] = Keys::W;
    _key_map['x'] = Keys::X;
    _key_map['y'] = Keys::Y;
    _key_map['z'] = Keys::Z;
    _key_map[' '] = Keys::SPACE;

    _key_map[0xffe5] = Keys::CAPS_LOCK;
    _key_map[0xff09] = Keys::TAB;
    _key_map[0xff1b] = Keys::ESCAPE;
    _key_map[0xff0d] = Keys::RETURN;

    _key_map[0xff51] = Keys::LEFT_ARROW;
    _key_map[0xff52] = Keys::UP_ARROW;
    _key_map[0xff53] = Keys::RIGHT_ARROW;
    _key_map[0xff54] = Keys::DOWN_ARROW;

    _key_map[0xffbe] = Keys::F1;
}

Event* VncVideoDriver::wait_next_event(double sleep) {
    vnc_poll(sleep * 1000000ll);
    if (_event_queue.empty()) {
        return false;
    } else {
        Event* event = _event_queue.front();
        _event_queue.pop();
        return event;
    }
}

bool VncVideoDriver::button() {
    vnc_poll(0);
    return _button;
}

Point VncVideoDriver::get_mouse() {
    vnc_poll(0);
    return _mouse;
}

void VncVideoDriver::get_keys(KeyMap* keys) {
    vnc_poll(0);
    keys->clear();
}

void VncVideoDriver::set_game_state(GameState) { }

int VncVideoDriver::get_demo_scenario() {
    int levels[] = { 0, 5, 23 };
    return levels[rand() % 3];
}

void VncVideoDriver::main_loop_iteration_complete(uint32_t) { }

int VncVideoDriver::ticks() {
    return (usecs() - _start_time) * 60 / 1000000;
}

void VncVideoDriver::loop(CardStack* stack) {
    while (!stack->empty()) {
        double at = stack->top()->next_timer();
        if (at == 0.0) {
            at = std::numeric_limits<double>::infinity();
        }
        scoped_ptr<Event> event(wait_next_event(at - now_secs()));
        if (event.get()) {
            event->send(stack->top());
        } else if (at != std::numeric_limits<double>::infinity()) {
            stack->top()->fire_timer();
        }
    }
}

}  // namespace antares
