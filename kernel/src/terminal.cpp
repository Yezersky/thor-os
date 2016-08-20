//=======================================================================
// Copyright Baptiste Wicht 2013-2016.
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
//  http://www.boost.org/LICENSE_1_0.txt)
//=======================================================================

#include <array.hpp>

#include "terminal.hpp"
#include "keyboard.hpp"
#include "console.hpp"
#include "assert.hpp"
#include "logging.hpp"

namespace {

constexpr const size_t MAX_TERMINALS = 2;
size_t active_terminal;

bool shift = false;

std::array<stdio::virtual_terminal, MAX_TERMINALS> terminals;

} //end of anonymous namespace

void stdio::virtual_terminal::print(char key){
    //TODO If it is not the active terminal, buffer it
    k_print(key);
}

void stdio::virtual_terminal::send_input(char key){
    if(canonical){
        //Key released
        if(key & 0x80){
            key &= ~(0x80);
            if(key == keyboard::KEY_LEFT_SHIFT || key == keyboard::KEY_RIGHT_SHIFT){
                shift = false;
            }
        }
        //Key pressed
        else {
            if(key == keyboard::KEY_LEFT_SHIFT || key == keyboard::KEY_RIGHT_SHIFT){
                shift = true;
            } else if(key == keyboard::KEY_BACKSPACE){
                if(!input_buffer.empty()){
                    input_buffer.pop_last();
                    print('\b');
                }
            } else {
                auto qwertz_key =
                    shift
                    ? keyboard::shift_key_to_ascii(key)
                    : keyboard::key_to_ascii(key);

                if(qwertz_key){
                    input_buffer.push(qwertz_key);

                    print(qwertz_key);

                    if(qwertz_key == '\n'){
                        // Transfer current line to the canonical buffer
                        while(!input_buffer.empty()){
                            canonical_buffer.push(input_buffer.pop());
                        }

                        if(!input_queue.empty()){
                            input_queue.wake_up();
                        }
                    }
                }
            }
        }
    } else {
        // The complete processing of the key will be done by the
        // userspace program
        auto code = keyboard::raw_key_to_keycode(key);
        raw_buffer.push(static_cast<size_t>(code));

        if(!input_queue.empty()){
            input_queue.wake_up();
        }

        thor_assert(!raw_buffer.full(), "raw buffer is full!");
    }
}

void stdio::virtual_terminal::send_mouse_input(keycode key){
    if(!canonical && mouse){
        raw_buffer.push(static_cast<size_t>(key));

        if(!input_queue.empty()){
            input_queue.wake_up();
        }

        thor_assert(!raw_buffer.full(), "raw buffer is full!");
    }
}

size_t stdio::virtual_terminal::read_input_can(char* buffer, size_t max){
    size_t read = 0;

    while(true){
        while(!canonical_buffer.empty()){
            auto c = canonical_buffer.pop();

            buffer[read++] = c;

            if(read >= max || c == '\n'){
                return read;
            }
        }

        input_queue.sleep();
    }
}

// TODO In case of max < read, the timeout is not respected
size_t stdio::virtual_terminal::read_input_can(char* buffer, size_t max, size_t ms){
    size_t read = 0;

    while(true){
        while(!canonical_buffer.empty()){
            auto c = canonical_buffer.pop();

            buffer[read++] = c;

            if(read >= max || c == '\n'){
                return read;
            }
        }

        if(!ms){
            return read;
        }

        if(!input_queue.sleep(ms)){
            return read;
        }
    }
}

size_t stdio::virtual_terminal::read_input_raw(){
    if(raw_buffer.empty()){
        input_queue.sleep();
    }

    return raw_buffer.pop();
}

size_t stdio::virtual_terminal::read_input_raw(size_t ms){
    if(raw_buffer.empty()){
        if(!ms){
            return static_cast<size_t>(keycode::TIMEOUT);
        }

        if(!input_queue.sleep(ms)){
            return static_cast<size_t>(keycode::TIMEOUT);
        }
    }

    thor_assert(!raw_buffer.empty(), "There is a problem with the sleep queue");

    return raw_buffer.pop();
}

void stdio::virtual_terminal::set_canonical(bool can){
    logging::logf(logging::log_level::TRACE, "Switched terminal %u canonical mode from %u to %u\n", id, uint64_t(canonical), uint64_t(can));

    canonical = can;
}

void stdio::virtual_terminal::set_mouse(bool m){
    logging::logf(logging::log_level::TRACE, "Switched terminal %u mouse handling mode from %u to %u\n", id, uint64_t(mouse), uint64_t(m));

    mouse = m;
}

void stdio::init_terminals(){
    for(size_t i  = 0; i < MAX_TERMINALS; ++i){
        auto& terminal = terminals[i];

        terminal.id = i;
        terminal.active = false;
        terminal.canonical = true;
        terminal.mouse = false;
    }

    active_terminal = 0;
    terminals[active_terminal].active = true;
}

stdio::virtual_terminal& stdio::get_active_terminal(){
    return terminals[active_terminal];
}

stdio::virtual_terminal& stdio::get_terminal(size_t id){
    thor_assert(id < MAX_TERMINALS, "Out of bound tty");

    return terminals[id];
}
