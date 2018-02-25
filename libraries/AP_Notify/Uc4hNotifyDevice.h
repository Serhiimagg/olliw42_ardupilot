/*
 *  AP_Notify Library. 
 * based upon a prototype library by David "Buzz" Bussenschutt.
 */

/*
   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#pragma once

#include <AP_HAL/AP_HAL.h>
#include "NotifyDevice.h"

class Uc4hNotifyDevice: public NotifyDevice {
public:
    Uc4hNotifyDevice();

    // init - initialised the LED
    virtual bool init(void);

    // healthy - returns true if the LED is operating properly
    virtual bool healthy() { return _healthy; }

    // update - updates led according to timed_updated.  Should be
    // called at 50Hz
    virtual void update();

    enum UC4HNOTIFYTYPEENUM {
        UC4HNOTIFYTYPE_FLAGS = 0,
        UC4HNOTIFYTYPE_RGBLEDS, //subtype is the number of leds
    };

    enum UC4HNOTIFYMODEENUM {
        UC4HNOTIFYMODE_FLAGS = 0,
        UC4HNOTIFYMODE_3RGBLEDS,
    };
    uint16_t notify_mode; //this tells in which mode the notify device is

private:

    AP_UAVCAN *_ap_uavcan[MAX_NUMBER_OF_CAN_DRIVERS];
    bool _healthy;
    uint64_t _task_time_last; //to slow down
    bool _updated;

    void find_CAN(void);
    void send_CAN_notify_message(void);

    void update_slow(void);

    struct {
        union {
            struct {
                uint8_t rgb[3][3];
            } rgb3leds;
            uint8_t buf[32]; //can be up to 64, but this shoukld be plenty for us
        };
    } _notify_data;

    void set_led_rgb(uint8_t lednr, uint8_t r, uint8_t g, uint8_t b);
    uint16_t _led_task_count;
    void update_mode_3rgbleds(void);
};
