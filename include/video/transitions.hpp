// Copyright (C) 1997, 1999-2001, 2008 Nathan Lamont
// Copyright (C) 2008-2012 The Antares Authors
//
// This file is part of Antares, a tactical space combat game.
//
// Antares is free software: you can redistribute it and/or modify it
// under the terms of the Lesser GNU General Public License as published
// by the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Antares is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with Antares.  If not, see http://www.gnu.org/licenses/

#ifndef ANTARES_VIDEO_TRANSITIONS_HPP_
#define ANTARES_VIDEO_TRANSITIONS_HPP_

#include <sfz/sfz.hpp>

#include "drawing/color.hpp"
#include "ui/card.hpp"

namespace antares {

class Sprite;

class Transitions {
  public:
    Transitions();
    ~Transitions();

    void reset();
    void start_boolean(int32_t in_speed, int32_t out_speed, uint8_t goal_color);
    void update_boolean(int32_t time_passed);
    void draw() const;

  private:
    bool _active;
    int32_t _step;
    int32_t _in_speed;
    int32_t _out_speed;
    RgbColor _color;

    DISALLOW_COPY_AND_ASSIGN(Transitions);
};

class ColorFade : public Card {
  public:
    enum Direction {
        TO_COLOR = 0,
        FROM_COLOR = 1,
    };

    ColorFade(Direction direction, const RgbColor& color, int64_t duration, bool allow_skip,
            bool* skipped);

    virtual void become_front();

    virtual void mouse_down(const MouseDownEvent& event);
    virtual void key_down(const KeyDownEvent& event);
    virtual void gamepad_button_down(const GamepadButtonDownEvent& event);
    virtual bool next_timer(int64_t& time);
    virtual void fire_timer();

    virtual void draw() const;

  private:
    const Direction _direction;
    const RgbColor _color;

    const bool _allow_skip;
    bool* _skipped;

    int64_t _start;
    int64_t _next_event;
    const int64_t _duration;

    DISALLOW_COPY_AND_ASSIGN(ColorFade);
};

class PictFade : public Card {
  public:
    PictFade(int pict_id, bool* skipped);
    ~PictFade();

    virtual void become_front();

    virtual void mouse_down(const MouseDownEvent& event);
    virtual void key_down(const KeyDownEvent& event);
    virtual bool next_timer(int64_t& time);
    virtual void fire_timer();

    virtual void draw() const;

  protected:
    virtual int64_t fade_time() const;
    virtual int64_t display_time() const;
    virtual bool skip() const;

  private:
    void wax();
    void wane();

    enum State {
        NEW,
        WAXING,
        FULL,
        WANING,
    };

    State _state;
    bool* _skipped;
    int64_t _wane_start;

    std::unique_ptr<Sprite> _sprite;

    DISALLOW_COPY_AND_ASSIGN(PictFade);
};

}  // namespace antares

#endif // ANTARES_VIDEO_TRANSITIONS_HPP_
