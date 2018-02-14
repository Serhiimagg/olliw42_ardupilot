#include "BP_Mount_STorM32.h"
#include <AP_HAL/AP_HAL.h>
#include "../ArduCopter/Copter.h"
#include <GCS_MAVLink/GCS_MAVLink.h>

//using #if HAL_WITH_UAVCAN doesn't appear to work here, why ???
#include <AP_UAVCAN/AP_UAVCAN.h>


extern const AP_HAL::HAL& hal;
//extern Copter copter; //not needed then Copter.h is included


BP_Mount_STorM32::BP_Mount_STorM32(AP_Mount &frontend, AP_Mount::mount_state &state, uint8_t instance) :
    AP_Mount_Backend(frontend, state, instance),
    _initialised(false),
    _armed(false)
{
    for (uint8_t i = 0; i < MAX_NUMBER_OF_CAN_DRIVERS; i++) {
        _ap_uavcan[i] = nullptr;
    }
    _uart = nullptr;
    _mount_type = AP_Mount::Mount_Type_None; //will be determined in init()

    _task_time_last = 0;
    _task_counter = TASK_SLOT0;

    _bitmask = SEND_STORM32LINK_V2 | SEND_CMD_SETINPUTS | SEND_CMD_DOCAMERA;

    _status.pitch_deg = _status.roll_deg = _status.yaw_deg = 0.0f;
    _status_updated = false;

    _target_to_send = false;
    _target_mode_last = MAV_MOUNT_MODE_RETRACT;
}


//------------------------------------------------------
// BP_Mount_STorM32 interface functions, ArduPilot Mount
//------------------------------------------------------

// init - performs any required initialisation for this instance
void BP_Mount_STorM32::init(const AP_SerialManager& serial_manager)
{
    //from instance we can determine its type, we keep it here since that's easier/shorter
    _mount_type = _frontend.get_mount_type(_instance);
    // it should never happen that it's not one of the two, but let's enforce it, to not depend on the outside
    if ((_mount_type != AP_Mount::Mount_Type_STorM32_UAVCAN) && (_mount_type != AP_Mount::Mount_Type_STorM32_Native)) {
        _mount_type = AP_Mount::Mount_Type_None;
    }

    if (_mount_type == AP_Mount::Mount_Type_STorM32_Native) {
        _uart = serial_manager.find_serial(AP_SerialManager::SerialProtocol_STorM32_Native, 0);
        if (_uart) {
            //_initialised = true; //don't do this yet, we first need to pass find_gimbal()
            _serial_is_initialised = true; //tell the BP_STorM32 class
        } else {
            _serial_is_initialised = false; //tell the BP_STorM32 class, should not be needed, just to play it safe
            _mount_type = AP_Mount::Mount_Type_None; //this prevents many things from happening, safety guard
        }
    }
    if (_mount_type == AP_Mount::Mount_Type_STorM32_UAVCAN) {
        // I don't know if hal.can_mgr and get_UAVCAN() is fully done at this point already
        // so we don't do anything here, but do it in the update loop
    }

    set_mode((enum MAV_MOUNT_MODE)_state._default_mode.get()); //set mode to default value set by user via parameter
    _target_mode_last = _state._mode;
}


// update mount position - should be called periodically
// this function must be defined in any case
void BP_Mount_STorM32::update()
{
    if (!_initialised) {
        find_CAN(); //this only checks for the CAN to be ok, not if there is a gimbal
        find_gimbal(); //this searches for a gimbal on serial
        return;
    }

    send_startupbanner();
}


// 400 Hz loop
void BP_Mount_STorM32::update_fast()
{
    if (!_initialised) {
        return;
    }

    //slow down everything to 100Hz
    // we can't use update(), since 50 Hz isn't compatible with the desired 20 Hz STorM32Link rate
    // this is not totally correct, it seems the loop is slower than 100Hz, but just a bit?
    // should I use 9 ms, or better use micros64(), and something like 9900us? (with 10ms it might be easy to miss a 400Hz tick)
    uint64_t current_time_ms = AP_HAL::millis64();
    if ((current_time_ms - _task_time_last) >= 10) { //each message is send at 20Hz   50ms for 5 task slots => 10ms per task slot
        _task_time_last = current_time_ms;

        const uint16_t LIVEDATA_FLAGS = LIVEDATA_STATUS_V2|LIVEDATA_ATTITUDE_RELATIVE;

        switch (_task_counter) {
            case TASK_SLOT0:
                if (_bitmask & SEND_STORM32LINK_V2) {
                    send_storm32link_v2(_frontend._ahrs); //2.3ms
                }

                break;
            case TASK_SLOT1: { //bracket to avoid compile error: jump to case label
                // trigger live data
                if (_mount_type == AP_Mount::Mount_Type_STorM32_Native) {
                    receive_reset_wflush(); //we are brutal and kill all incoming bytes
                    send_cmd_getdatafields(LIVEDATA_FLAGS); //0.6ms
                }

                AP_Notify *notify = AP_Notify::instance();
                if (notify && (notify->flags.camera_trigger_pic)) {
                    notify->flags.camera_trigger_pic = false;
                    if (_bitmask & SEND_CMD_DOCAMERA) send_cmd_docamera(1); //1.0ms
                }

                }break;
            case TASK_SLOT2:
                set_target_angles_bymountmode();
                send_target_angles(); //1.7 ms or 1.0ms

                break;
            case TASK_SLOT3:
                if (_bitmask & SEND_CMD_SETINPUTS) {
                    send_cmd_setinputs(); //2.4ms
                }

                break;
            case TASK_SLOT4:
                // receive live data
                if (_mount_type == AP_Mount::Mount_Type_STorM32_Native) {
                    do_receive(); //we had now 4/5*50ms = 40ms time, this should be more than enough
                    if (message_received() && (_serial_in.cmd == 0x06) && (_serial_in.getdatafields.flags == LIVEDATA_FLAGS)) {
                        // attitude angles are in STorM32 convention
                        // convert from STorM32 to ArduPilot convention, this need correction p:-1,r:+1,y:-1
                        handle_storm32status_msg(
                            -_serial_in.getdatafields.livedata_attitude.pitch_deg,
                             _serial_in.getdatafields.livedata_attitude.roll_deg,
                            -_serial_in.getdatafields.livedata_attitude.yaw_deg );
                        // we also can check if gimbal is in normal operation mode
                        _armed = is_normal_state(_serial_in.getdatafields.livedata_status.state);
                    }
                }

                break;
        }

        _task_counter++;
        if (_task_counter >= TASK_SLOTNUMBER) _task_counter = 0;
    }
}


// set_mode - sets mount's mode
void BP_Mount_STorM32::set_mode(enum MAV_MOUNT_MODE mode)
{
    if (!_initialised) {
        return;
    }

    // record the mode change
    _state._mode = mode;
}


// status_msg - called to allow mounts to send their status to GCS using the MOUNT_STATUS message
void BP_Mount_STorM32::status_msg(mavlink_channel_t chan)
{
    //it doesn't matter if not _initalised
    // will then send out zeros

    float pitch_deg, roll_deg, yaw_deg;

    get_status_angles_deg(&pitch_deg, &roll_deg, &yaw_deg);

    // MAVLink MOUNT_STATUS: int32_t pitch(deg*100), int32_t roll(deg*100), int32_t yaw(deg*100)
    mavlink_msg_mount_status_send(chan, 0, 0, pitch_deg*100.0f, roll_deg*100.0f, yaw_deg*100.0f);

    // return target angles as gimbal's actual attitude.  To-Do: retrieve actual gimbal attitude and send these instead
//    mavlink_msg_mount_status_send(chan, 0, 0, ToDeg(_angle_ef_target_rad.y)*100, ToDeg(_angle_ef_target_rad.x)*100, ToDeg(_angle_ef_target_rad.z)*100);
}


//------------------------------------------------------
// BP_Mount_STorM32 private function
//------------------------------------------------------

void BP_Mount_STorM32::set_target_angles_bymountmode(void)
{
    uint16_t pitch_pwm, roll_pwm, yaw_pwm;

    bool get_pwm_target_from_radio = (_bitmask & GET_PWM_TARGET_FROM_RADIO) ? true : false;

    // flag to trigger sending target angles to gimbal
    bool send_ef_target = false;
    bool send_pwm_target = false;

    // update based on mount mode
    enum MAV_MOUNT_MODE mount_mode = get_mode();

    switch (mount_mode) {
        // move mount to a "retracted" position.
        case MAV_MOUNT_MODE_RETRACT:
            {
                const Vector3f &target = _state._retract_angles.get();
                _angle_ef_target_rad.x = ToRad(target.x);
                _angle_ef_target_rad.y = ToRad(target.y);
                _angle_ef_target_rad.z = ToRad(target.z);
                send_ef_target = true;
            }
            break;

        // move mount to a neutral position, typically pointing forward
        case MAV_MOUNT_MODE_NEUTRAL:
            {
                const Vector3f &target = _state._neutral_angles.get();
                _angle_ef_target_rad.x = ToRad(target.x);
                _angle_ef_target_rad.y = ToRad(target.y);
                _angle_ef_target_rad.z = ToRad(target.z);
                send_ef_target = true;
            }
            break;

        // point to the angles given by a mavlink message
        case MAV_MOUNT_MODE_MAVLINK_TARGETING:
            // earth-frame angle targets (i.e. _angle_ef_target_rad) should have already been set by a MOUNT_CONTROL message from GCS
            send_ef_target = true;
            break;

        // RC radio manual angle control, but with stabilization from the AHRS
        case MAV_MOUNT_MODE_RC_TARGETING:
            // update targets using pilot's rc inputs
            if (get_pwm_target_from_radio) {
                get_pwm_target_angles_from_radio(&pitch_pwm, &roll_pwm, &yaw_pwm);
                send_pwm_target = true;
            } else {
                update_targets_from_rc();
                send_ef_target = true;
            }
            if( is_failsafe() ){
                pitch_pwm = roll_pwm = yaw_pwm = 1500;
                _angle_ef_target_rad.y = _angle_ef_target_rad.x = _angle_ef_target_rad.z = 0.0f;
            }
            break;

        // point mount to a GPS point given by the mission planner
        case MAV_MOUNT_MODE_GPS_POINT:
            if(AP::gps().status() >= AP_GPS::GPS_OK_FIX_2D) {
                calc_angle_to_location(_state._roi_target, _angle_ef_target_rad, true, true);
                send_ef_target = true;
            }
            break;

        default:
            // we do not know this mode so do nothing
            break;
    }

    // send target angles
    if (send_ef_target) {
        set_target_angles_rad(_angle_ef_target_rad.y, _angle_ef_target_rad.x, _angle_ef_target_rad.z, mount_mode);
    }

    if (send_pwm_target) {
        set_target_angles_pwm(pitch_pwm, roll_pwm, yaw_pwm, mount_mode);
    }
}


void BP_Mount_STorM32::get_pwm_target_angles_from_radio(uint16_t* pitch_pwm, uint16_t* roll_pwm, uint16_t* yaw_pwm)
{
    get_valid_pwm_from_channel(_state._tilt_rc_in, pitch_pwm);
    get_valid_pwm_from_channel(_state._roll_rc_in, roll_pwm);
    get_valid_pwm_from_channel(_state._pan_rc_in, yaw_pwm);
}


void BP_Mount_STorM32::get_valid_pwm_from_channel(uint8_t rc_in, uint16_t* pwm)
{
    #define rc_ch(i) RC_Channels::rc_channel(i-1)

    if (rc_in && (rc_ch(rc_in))) {
        *pwm = rc_ch(rc_in)->get_radio_in();
    } else {
        *pwm = 1500;
    }
}


void BP_Mount_STorM32::set_target_angles_deg(float pitch_deg, float roll_deg, float yaw_deg, enum MAV_MOUNT_MODE mount_mode)
{
    _target.deg.pitch = pitch_deg;
    _target.deg.roll = roll_deg;
    _target.deg.yaw = yaw_deg;
    _target.type = angles_deg;
    _target.mode = mount_mode;
    _target_to_send = true; //do last, should not matter, but who knows
}


void BP_Mount_STorM32::set_target_angles_rad(float pitch_rad, float roll_rad, float yaw_rad, enum MAV_MOUNT_MODE mount_mode)
{
    _target.deg.pitch = ToDeg(pitch_rad);
    _target.deg.roll = ToDeg(roll_rad);
    _target.deg.yaw = ToDeg(yaw_rad);
    _target.type = angles_deg;
    _target.mode = mount_mode;
    _target_to_send = true; //do last, should not matter, but who knows
}


void BP_Mount_STorM32::set_target_angles_pwm(uint16_t pitch_pwm, uint16_t roll_pwm, uint16_t yaw_pwm, enum MAV_MOUNT_MODE mount_mode)
{
    _target.pwm.pitch = pitch_pwm;
    _target.pwm.roll = roll_pwm;
    _target.pwm.yaw = yaw_pwm;
    _target.type = angles_pwm;
    _target.mode = mount_mode;
    _target_to_send = true; //do last, should not matter, but who knows
}


void BP_Mount_STorM32::send_target_angles(void)
{
    if (_target.mode <= MAV_MOUNT_MODE_NEUTRAL) { //RETRACT and NEUTRAL
        if (_target_mode_last == _target.mode) {
            // has been done already, skip
        } else {
            // trigger a recenter camera, this clears all internal Remote states
            // the camera does not need to be recentered explicitly, thus break;
            send_cmd_recentercamera();
            _target_mode_last = _target.mode;
        }
        return;
    }

    // update to current mode, to avoid repeated actions on some mount mode changes
    _target_mode_last = _target.mode;

    if (_target.type == angles_pwm) {
        uint16_t pitch_pwm = _target.pwm.pitch;
        uint16_t roll_pwm = _target.pwm.roll;
        uint16_t yaw_pwm = _target.pwm.yaw;

        uint16_t DZ = 10; //_rc_target_pwm_deadzone;

        if (pitch_pwm < 10) pitch_pwm = 1500;
        if (pitch_pwm < 1500-DZ) pitch_pwm += DZ; else if (pitch_pwm > 1500+DZ) pitch_pwm -= DZ; else pitch_pwm = 1500;

        if (roll_pwm < 10) roll_pwm = 1500;
        if (roll_pwm < 1500-DZ) roll_pwm += DZ; else if (roll_pwm > 1500+DZ) roll_pwm -= DZ; else roll_pwm = 1500;

        if (yaw_pwm < 10) yaw_pwm = 1500;
        if (yaw_pwm < 1500-DZ) yaw_pwm += DZ; else if (yaw_pwm > 1500+DZ) yaw_pwm -= DZ; else yaw_pwm = 1500;

        send_cmd_setpitchrollyaw(pitch_pwm, roll_pwm, yaw_pwm);
    }else {
        float pitch_deg = _target.deg.pitch;
        float roll_deg = _target.deg.roll;
        float yaw_deg = _target.deg.yaw;

        //convert from ArduPilot to STorM32 convention
        // this need correction p:-1,r:+1,y:-1
        send_cmd_setangles(-pitch_deg, -roll_deg, -yaw_deg, 0);
    }
}


void BP_Mount_STorM32::get_status_angles_deg(float* pitch_deg, float* roll_deg, float* yaw_deg)
{
    *pitch_deg = _status.pitch_deg;
    *roll_deg = _status.roll_deg;
    *yaw_deg = _status.yaw_deg;
}


//------------------------------------------------------
// interface to AP_UAVCAN, for receiving a UAVCAN message
//------------------------------------------------------

void BP_Mount_STorM32::handle_storm32status_msg(float pitch_deg, float roll_deg, float yaw_deg)
{
    //the storm32.Status message sends angles in ArduPilot convention, no conversion needed
    _status.pitch_deg = pitch_deg;
    _status.roll_deg = roll_deg;
    _status.yaw_deg = yaw_deg;

    _status_updated = true;
}


//------------------------------------------------------
// discovery functions
//------------------------------------------------------

void BP_Mount_STorM32::find_CAN(void)
{
    return; //XX

    if (_mount_type != AP_Mount::Mount_Type_STorM32_UAVCAN) {
        return;
    }

    //is this allowed, or can the fields change to the negative with time ??
    // exit if not initialised, but check for validity of CAN interfaces
    //TODO: this is flawed, it stops searching for further CAN interfaces once one has been detected
    // I really would need to know more details of the underlying procedures

    if (hal.can_mgr == nullptr) {
        return;
    }

    for (uint8_t i = 0; i < MAX_NUMBER_OF_CAN_DRIVERS; i++) {
        if (hal.can_mgr[i] != nullptr) {
            AP_UAVCAN *ap_uavcan = hal.can_mgr[i]->get_UAVCAN();
            if (ap_uavcan != nullptr) {
                _ap_uavcan[i] = ap_uavcan;
                _initialised = true; //at least one CAN interface is initialized
                _serial_is_initialised = true; //inform the BP_STorM32 class

//XX                ap_uavcan->register_storm32status_listener_to_node(this, STORM32_UAVCAN_NODEID); //register listener
            }
        }
    }
}


//this also could be done for CAN, by expecting e.g. NodeInfo to arrive, or to get a response to asking for the version
// this would need an storm32nodespecificack listener
void BP_Mount_STorM32::find_gimbal(void)
{
    if (_mount_type != AP_Mount::Mount_Type_STorM32_Native) {
        return;
    }

    uint64_t current_time_ms = AP_HAL::millis64();
//XX
/*
    if (current_time_ms > FIND_GIMBAL_MAX_SEARCH_TIME_MS) {
        _initialised = false; //should be already false, but it can't hurt to ensure that
       _serial_is_initialised = false; //switch off BP_STorM32
       _mount_type = AP_Mount::Mount_Type_None; //switch off finally, also makes find_gimbal() to stop searching
        return;
    }
*/
    if ((current_time_ms - _task_time_last) > 100) { //try it every 100ms
        _task_time_last = current_time_ms;

        switch (_task_counter) {
            case TASK_SLOT0:
                // send GETVERSIONSTR
                receive_reset_wflush(); //we are brutal and kill all incoming bytes
                send_cmd_getversionstr();
                break;
            case TASK_SLOT1:
                // receive GETVERSIONSTR response
                do_receive();
                if (message_received() && (_serial_in.cmd == 0x02)) {
                    for (uint16_t n=0;n<16;n++) versionstr[n] = _serial_in.getversionstr.versionstr[n];
                    versionstr[16] = '\0';
                    for (uint16_t n=0;n<16;n++) boardstr[n] = _serial_in.getversionstr.boardstr[n];
                    boardstr[16] = '\0';
                    _task_counter = TASK_SLOT0;
                    _initialised = true;
                }
                break;
        }
        _task_counter++;
        if( _task_counter >= 3 ) _task_counter = 0;
    }
}


void BP_Mount_STorM32::send_startupbanner(void)
{
    // if no gimbal was found yet, we skip
    //  a set gcs_send_banner flag will not be cleared, so that the banner is send in any case at some point
    if( !_initialised ) {
        return;
    }

    AP_Notify *notify = AP_Notify::instance();

    if (notify && notify->flags.gcs_send_banner) {
        notify->flags.gcs_send_banner = false;
        gcs().send_text(MAV_SEVERITY_INFO, "  STorM32: found and initialized");
        char s[64];
        strcpy(s, "  STorM32: " ); strcat(s, versionstr ); strcat(s, ", " );  strcat(s, boardstr );
        gcs().send_text(MAV_SEVERITY_INFO, s);
    }
}


//------------------------------------------------------
// interfaces to BP_STorM32
//------------------------------------------------------

size_t BP_Mount_STorM32::_serial_txspace(void)
{
    if (_mount_type == AP_Mount::Mount_Type_STorM32_UAVCAN) {
        return 1000;
    }
    if (_mount_type == AP_Mount::Mount_Type_STorM32_Native) {
        return (size_t)_uart->txspace();
    }
    return 0;
}


size_t BP_Mount_STorM32::_serial_write(const uint8_t *buffer, size_t size, uint8_t priority)
{
    if (_mount_type == AP_Mount::Mount_Type_STorM32_UAVCAN) {

        for (uint8_t i = 0; i < MAX_NUMBER_OF_CAN_DRIVERS; i++) {
            if (_ap_uavcan[i] != nullptr) {
//XX                _ap_uavcan[i]->storm32_nodespecific_send( (uint8_t*)buffer, size, priority );
            }
        }

        return size; //technically it should be set to zero if no transmission happened, i == MAX_NUMBER_OF_CAN_DRIVERS

    }
    if (_mount_type == AP_Mount::Mount_Type_STorM32_Native) {

        if (_uart != nullptr) {
            return _uart->write(buffer, size);
        }

        return 0;

    }
    return 0;
}


uint32_t BP_Mount_STorM32::_serial_available(void)
{
    if (_mount_type == AP_Mount::Mount_Type_STorM32_UAVCAN) {
        return 0;
    }
    if (_mount_type == AP_Mount::Mount_Type_STorM32_Native) {
        return _uart->available();
    }
    return 0;
}


int16_t BP_Mount_STorM32::_serial_read(void)
{
    if (_mount_type == AP_Mount::Mount_Type_STorM32_UAVCAN) {
        return 0;
    }
    if (_mount_type == AP_Mount::Mount_Type_STorM32_Native) {
        return _uart->read();
    }
    return 0;
}


uint16_t BP_Mount_STorM32::_rcin_read(uint8_t ch)
{
//    if( hal.rcin->in_failsafe() )
//    if( copter.in_failsafe_radio() )
//        return 0;
//    else
// should/can one use also the field ap.rc_receiver_present in addition to failsafe.radio ?
    //this seems to be zero from startup without transmitter, and failsafe didn't helped at all, so leave it as it is
    return hal.rcin->read(ch);
}


//------------------------------------------------------
// helper
//------------------------------------------------------

bool BP_Mount_STorM32::is_failsafe(void)
{
    #define rc_ch(i) RC_Channels::rc_channel(i-1)

    uint8_t roll_rc_in = _state._roll_rc_in;
    uint8_t tilt_rc_in = _state._tilt_rc_in;
    uint8_t pan_rc_in = _state._pan_rc_in;

    if (roll_rc_in && (rc_ch(roll_rc_in)) && (rc_ch(roll_rc_in)->get_radio_in() < 700)) return true;
    if (tilt_rc_in && (rc_ch(tilt_rc_in)) && (rc_ch(tilt_rc_in)->get_radio_in() < 700)) return true;
    if (pan_rc_in && (rc_ch(pan_rc_in)) && (rc_ch(pan_rc_in)->get_radio_in() < 700)) return true;

    return false;
}





