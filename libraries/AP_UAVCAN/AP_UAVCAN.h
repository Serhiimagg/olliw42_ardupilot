/*
 *
 *      Author: Eugene Shamaev
 */
#ifndef AP_UAVCAN_H_
#define AP_UAVCAN_H_

#include <uavcan/uavcan.hpp>

#include <AP_HAL/CAN.h>
#include <AP_HAL/Semaphores.h>
#include <AP_GPS/AP_GPS.h>
#include <AP_Param/AP_Param.h>

#include <AP_GPS/GPS_Backend.h>
#include <AP_Baro/AP_Baro_Backend.h>
#include <AP_Compass/AP_Compass.h>
#include <AP_BattMonitor/AP_BattMonitor_Backend.h>

#include <uavcan/helpers/heap_based_pool_allocator.hpp>
#include <uavcan/equipment/indication/RGB565.hpp>
//OW
#include <AP_RangeFinder/RangeFinder_Backend.h>
#include "bp_dsdl_generated/olliw/uc4h/Notify.hpp"
#include <uavcan/tunnel/Broadcast.hpp>
#include "BP_UavcanTunnelManager.h"
#include "BP_UavcanEscStatusManager.h"
//OWEND

#ifndef UAVCAN_NODE_POOL_SIZE
#define UAVCAN_NODE_POOL_SIZE 8192
#endif

#ifndef UAVCAN_NODE_POOL_BLOCK_SIZE
#define UAVCAN_NODE_POOL_BLOCK_SIZE 256
#endif

#ifndef UAVCAN_SRV_NUMBER
#define UAVCAN_SRV_NUMBER 18
#endif

#define AP_UAVCAN_MAX_LISTENERS 4
#define AP_UAVCAN_MAX_GPS_NODES 4
#define AP_UAVCAN_MAX_MAG_NODES 4
#define AP_UAVCAN_MAX_BARO_NODES 4
#define AP_UAVCAN_MAX_BI_NUMBER 4
//OW
#define AP_UAVCAN_UC4HGENERICBATTERYINFO_MAX_NUMBER 4
#define AP_UAVCAN_UC4HDISTANCE_MAX_NUMBER 4 //we may want up to 10!
#define AP_UAVCAN_ESCSTATUS_MAX_NUMBER 8
//OWEND

#define AP_UAVCAN_SW_VERS_MAJOR 1
#define AP_UAVCAN_SW_VERS_MINOR 0

#define AP_UAVCAN_HW_VERS_MAJOR 1
#define AP_UAVCAN_HW_VERS_MINOR 0

#define AP_UAVCAN_MAX_LED_DEVICES 4
#define AP_UAVCAN_LED_DELAY_MILLISECONDS 50

class AP_UAVCAN {
public:
    AP_UAVCAN();
    ~AP_UAVCAN();

    static const struct AP_Param::GroupInfo var_info[];

    // Return uavcan from @iface or nullptr if it's not ready or doesn't exist
    static AP_UAVCAN *get_uavcan(uint8_t iface);

    // this function will register the listening class on a first free channel or on the specified channel
    // if preferred_channel = 0 then free channel will be searched for
    // if preferred_channel > 0 then listener will be added to specific channel
    // return value is the number of assigned channel or 0 if fault
    // channel numbering starts from 1
    uint8_t register_gps_listener(AP_GPS_Backend* new_listener, uint8_t preferred_channel);

    uint8_t register_gps_listener_to_node(AP_GPS_Backend* new_listener, uint8_t node);

    uint8_t find_gps_without_listener(void);

    // Removes specified listener from all nodes
    void remove_gps_listener(AP_GPS_Backend* rem_listener);

    // Returns pointer to GPS state connected with specified node.
    // If node is not found and there are free space, locate a new one
    AP_GPS::GPS_State *find_gps_node(uint8_t node);

    // Updates all listeners of specified node
    void update_gps_state(uint8_t node);

    struct Baro_Info {
        float pressure;
        float pressure_variance;
        float temperature;
        float temperature_variance;
    };

    uint8_t register_baro_listener(AP_Baro_Backend* new_listener, uint8_t preferred_channel);
    uint8_t register_baro_listener_to_node(AP_Baro_Backend* new_listener, uint8_t node);
    void remove_baro_listener(AP_Baro_Backend* rem_listener);
    Baro_Info *find_baro_node(uint8_t node);
    uint8_t find_smallest_free_baro_node();
    void update_baro_state(uint8_t node);

    struct Mag_Info {
        Vector3f mag_vector;
    };

    uint8_t register_mag_listener(AP_Compass_Backend* new_listener, uint8_t preferred_channel);
    void remove_mag_listener(AP_Compass_Backend* rem_listener);
    Mag_Info *find_mag_node(uint8_t node, uint8_t sensor_id);
    uint8_t find_smallest_free_mag_node();
    uint8_t register_mag_listener_to_node(AP_Compass_Backend* new_listener, uint8_t node);
    void update_mag_state(uint8_t node, uint8_t sensor_id);

    struct BatteryInfo_Info {
        float temperature;
        float voltage;
        float current;
        float remaining_capacity_wh;
        float full_charge_capacity_wh;
        uint8_t status_flags;
    };

    uint8_t register_BM_bi_listener_to_id(AP_BattMonitor_Backend* new_listener, uint8_t id);
    void remove_BM_bi_listener(AP_BattMonitor_Backend* rem_listener);
    BatteryInfo_Info *find_bi_id(uint8_t id);
    uint8_t find_smallest_free_bi_id();
    void update_bi_state(uint8_t id);

    // synchronization for RC output
    void SRV_sem_take();
    void SRV_sem_give();
    
    // synchronization for LED output
    bool led_out_sem_take();
    void led_out_sem_give();
    void led_out_send();

    // output from do_cyclic
    void SRV_send_servos();
    void SRV_send_esc();

private:
    // ------------------------- GPS
    // 255 - means free node
    uint8_t _gps_nodes[AP_UAVCAN_MAX_GPS_NODES];
    // Counter of how many listeners are connected to this source
    uint8_t _gps_node_taken[AP_UAVCAN_MAX_GPS_NODES];
    // GPS data of the sources
    AP_GPS::GPS_State _gps_node_state[AP_UAVCAN_MAX_GPS_NODES];

    // 255 - means no connection
    uint8_t _gps_listener_to_node[AP_UAVCAN_MAX_LISTENERS];
    // Listeners with callbacks to be updated
    AP_GPS_Backend* _gps_listeners[AP_UAVCAN_MAX_LISTENERS];

    // ------------------------- BARO
    uint8_t _baro_nodes[AP_UAVCAN_MAX_BARO_NODES];
    uint8_t _baro_node_taken[AP_UAVCAN_MAX_BARO_NODES];
    Baro_Info _baro_node_state[AP_UAVCAN_MAX_BARO_NODES];
    uint8_t _baro_listener_to_node[AP_UAVCAN_MAX_LISTENERS];
    AP_Baro_Backend* _baro_listeners[AP_UAVCAN_MAX_LISTENERS];

    // ------------------------- MAG
    uint8_t _mag_nodes[AP_UAVCAN_MAX_MAG_NODES];
    uint8_t _mag_node_taken[AP_UAVCAN_MAX_MAG_NODES];
    Mag_Info _mag_node_state[AP_UAVCAN_MAX_MAG_NODES];
    uint8_t _mag_node_max_sensorid_count[AP_UAVCAN_MAX_MAG_NODES];
    uint8_t _mag_listener_to_node[AP_UAVCAN_MAX_LISTENERS];
    AP_Compass_Backend* _mag_listeners[AP_UAVCAN_MAX_LISTENERS];
    uint8_t _mag_listener_sensor_ids[AP_UAVCAN_MAX_LISTENERS];

    // ------------------------- BatteryInfo
    uint16_t _bi_id[AP_UAVCAN_MAX_BI_NUMBER];
    uint16_t _bi_id_taken[AP_UAVCAN_MAX_BI_NUMBER];
    BatteryInfo_Info _bi_id_state[AP_UAVCAN_MAX_BI_NUMBER];
    uint16_t _bi_BM_listener_to_id[AP_UAVCAN_MAX_LISTENERS];
    AP_BattMonitor_Backend* _bi_BM_listeners[AP_UAVCAN_MAX_LISTENERS];

    struct {
        uint16_t pulse;
        uint16_t safety_pulse;
        uint16_t failsafe_pulse;
        bool esc_pending;
        bool servo_pending;
    } _SRV_conf[UAVCAN_SRV_NUMBER];

    bool _initialized;
    uint8_t _SRV_armed;
    uint8_t _SRV_safety;
    uint32_t _SRV_last_send_us;

    typedef struct {
        bool enabled;
        uint8_t led_index;
        uavcan::equipment::indication::RGB565 rgb565_color;
    } _led_device;

    struct {
        bool broadcast_enabled;
        _led_device devices[AP_UAVCAN_MAX_LED_DEVICES];
        uint8_t devices_count;
        uint64_t last_update;
    } _led_conf;

    AP_HAL::Semaphore *SRV_sem;
    AP_HAL::Semaphore *_led_out_sem;

    class SystemClock: public uavcan::ISystemClock, uavcan::Noncopyable {
        SystemClock()
        {
        }

        uavcan::UtcDuration utc_adjustment;
        virtual void adjustUtc(uavcan::UtcDuration adjustment)
        {
            utc_adjustment = adjustment;
        }

    public:
        virtual uavcan::MonotonicTime getMonotonic() const
        {
            uavcan::uint64_t usec = 0;
            usec = AP_HAL::micros64();
            return uavcan::MonotonicTime::fromUSec(usec);
        }
        virtual uavcan::UtcTime getUtc() const
        {
            uavcan::UtcTime utc;
            uavcan::uint64_t usec = 0;
            usec = AP_HAL::micros64();
            utc.fromUSec(usec);
            utc += utc_adjustment;
            return utc;
        }

        static SystemClock& instance()
        {
            static SystemClock inst;
            return inst;
        }
    };

    uavcan::Node<0> *_node = nullptr;

    uavcan::ISystemClock& get_system_clock();
    uavcan::ICanDriver* get_can_driver();
    uavcan::Node<0>* get_node();

    // This will be needed to implement if UAVCAN is used with multithreading
    // Such cases will be firmware update, etc.
    class RaiiSynchronizer {
    public:
        RaiiSynchronizer()
        {
        }

        ~RaiiSynchronizer()
        {
        }
    };

    uavcan::HeapBasedPoolAllocator<UAVCAN_NODE_POOL_BLOCK_SIZE, AP_UAVCAN::RaiiSynchronizer> _node_allocator;

    AP_Int8 _uavcan_node;
    AP_Int32 _servo_bm;
    AP_Int32 _esc_bm;
    AP_Int16 _servo_rate_hz;

    uint8_t _uavcan_i;

    AP_HAL::CANManager* _parent_can_mgr;

public:
    void do_cyclic(void);
    bool try_init(void);

    void SRV_set_safety_pwm(uint32_t chmask, uint16_t pulse_len);
    void SRV_set_failsafe_pwm(uint32_t chmask, uint16_t pulse_len);
    void SRV_force_safety_on(void);
    void SRV_force_safety_off(void);
    void SRV_arm_actuators(bool arm);
    void SRV_write(uint16_t pulse_len, uint8_t ch);
    void SRV_push_servos(void);
    bool led_write(uint8_t led_index, uint8_t red, uint8_t green, uint8_t blue);

    void set_parent_can_mgr(AP_HAL::CANManager* parent_can_mgr)
    {
        _parent_can_mgr = parent_can_mgr;
    }

//OW
// --- uc4h.GenericBatteryInfo ---
    // incoming message, by device id
public:
    struct Uc4hGenericBatteryInfo_Data {
        float voltage; //float16
        float current; //float16
        float charge_consumed_mAh; //float16
        float energy_consumed_Wh; //float16
        float cell_voltages[12]; //float16[<=12] cell_voltages
        uint8_t status_flags;
        //auxiliary meta data
        uint8_t i; //this avoids needing a 2nd loop in update_i(), must be set by getptrto_data()
        uint8_t cell_voltages_num; //this counts the number of received cell voltages
    };

    uint8_t uc4hgenericbatteryinfo_register_listener(AP_BattMonitor_Backend* new_listener, uint8_t id);
    void uc4hgenericbatteryinfo_remove_listener(AP_BattMonitor_Backend* rem_listener);
    Uc4hGenericBatteryInfo_Data* uc4hgenericbatteryinfo_getptrto_data(uint8_t id);
    void uc4hgenericbatteryinfo_update_i(uint8_t i);

private:
    struct {
        uint16_t id[AP_UAVCAN_UC4HGENERICBATTERYINFO_MAX_NUMBER];
        uint16_t id_taken[AP_UAVCAN_UC4HGENERICBATTERYINFO_MAX_NUMBER];
        uint16_t listener_to_id[AP_UAVCAN_MAX_LISTENERS];
        AP_BattMonitor_Backend* listeners[AP_UAVCAN_MAX_LISTENERS];
        Uc4hGenericBatteryInfo_Data data[AP_UAVCAN_UC4HGENERICBATTERYINFO_MAX_NUMBER];
    } _uc4hgenericbatteryinfo;

// --- uc4h.Distance ---
// incoming message, by orientation id
    //we id it by the orientation and sub_id: pitch + (yaw << 8) + (sub_id << 16) => uint32_t
    public:
        struct Uc4hDistance_Data {
            int8_t fixed_axis_pitch; // int4 fixed_axis_pitch         # -PI/2 ... +PI/2 or -6 ... 6
            int8_t fixed_axis_yaw;   // int5 fixed_axis_yaw           # -PI ... +PI or -12 ... 12
            uint8_t sensor_sub_id;   // uint4 sensor_sub_id           # Allow up to 16 sensors per orientation
            uint8_t range_flag;      // uint3 range_flag
            float range;             // float16 range                 # Meters
            float range_min;         // float16 range_min             # Meters. Can be NAN if unknown.
            float range_max;         // float16 range_max             # Meters. Can be NAN if unknown.
            float vertical_field_of_view;   // float16 vertical_field_of_view       # Radians. Can be NAN if unknown.
            float horizontal_field_of_view; // float16 horizontal_field_of_view     # Radians. Can be NAN if unknown.
            //auxiliary meta data
            bool sensor_properties_available;
            uint8_t node_id;
        };

        uint8_t uc4hdistance_register_listener(AP_RangeFinder_Backend* new_listener, uint32_t id, uint8_t* node_id);
        void uc4hdistance_remove_listener(AP_RangeFinder_Backend* rem_listener);
        Uc4hDistance_Data* uc4hdistance_getptrto_data(uint8_t* data_i, uint32_t id);
        void uc4hdistance_update_i(uint8_t data_i);

    private:
        struct {
            uint32_t id[AP_UAVCAN_UC4HDISTANCE_MAX_NUMBER];
            uint8_t id_taken[AP_UAVCAN_UC4HDISTANCE_MAX_NUMBER];
            uint8_t listener_to_id[AP_UAVCAN_MAX_LISTENERS];
            AP_RangeFinder_Backend* listeners[AP_UAVCAN_MAX_LISTENERS];
            Uc4hDistance_Data data[AP_UAVCAN_UC4HDISTANCE_MAX_NUMBER];
        } _uc4hdistance;

// --- EscStatus ---
    // incoming message, by device id
    // some of the handling is done by the BP_UavcanEscStatusManager class!
    // we write directly to this class, and write directly to the DataFlash, so just the BattMonitor=84 stuff to keep here
    // since EscStatus can come from different nodes and different esc_index, we allow just one BattMonitor listener
public:
    struct EscStatus_Data {
        uint32_t error_count;
        float voltage;
        float current;
        float temperature;
        int32_t rpm;
        uint8_t power_rating_pct;
        //auxiliary meta data
        uint8_t i; //this avoids needing a 2nd loop in update_i(), must be set by getptrto_data()
    };

    uint8_t escstatus_register_listener(AP_BattMonitor_Backend* new_listener, uint8_t id);
    EscStatus_Data* escstatus_getptrto_data(uint8_t id);
    void escstatus_update_i(uint8_t i);

private:
    struct {
        uint16_t id[AP_UAVCAN_ESCSTATUS_MAX_NUMBER];
        AP_BattMonitor_Backend* listener; //there is only one listener for all EscStatus messages
        EscStatus_Data data[AP_UAVCAN_ESCSTATUS_MAX_NUMBER];
    } _escstatus;

// --- uc4h.Notify ---
    // outgoing message
public:
    bool uc4hnotify_out_sem_take();
    void uc4hnotify_out_sem_give();
    void uc4hnotify_send(uint8_t type, uint8_t subtype, uint8_t* payload, uint8_t payload_len);

private:
    struct {
        uavcan::olliw::uc4h::Notify msg;
        uavcan::TransferPriority priority;
        bool to_send;
        AP_HAL::Semaphore* sem;
    } _uc4hnotify_out;

    // --- outgoing message handler ---
    void uc4hnotify_out_do_cyclic(void);

// --- tunnel.Broadcast ---
    // incoming message, by channel_id
    // the handling of the channel_id is done by the BP_UavcanTunnelManager class!
    // we write directly to this class, so nothing to keep here

// --- tunnel.Broadcast ---
    // outgoing message
    // the handling is simple since most is done in the BP_UavcanTunnelManager class! we just fetch a ptr to data
public:
    bool tunnelbroadcast_out_sem_take();
    void tunnelbroadcast_out_sem_give();
    bool tunnelbroadcast_is_to_send(uint8_t tunnel_index);
    void tunnelbroadcast_send(uint8_t tunnel_index, uint8_t protocol, uint8_t channel_id, uint8_t* buffer, uint8_t buffer_len, uint8_t priority);

private:
    struct {
        uavcan::tunnel::Broadcast msg[TUNNELMANAGER_NUM_CHANNELS];
        uavcan::TransferPriority priority[TUNNELMANAGER_NUM_CHANNELS];
        bool to_send[TUNNELMANAGER_NUM_CHANNELS];
        AP_HAL::Semaphore* sem;
    } _tunnelbroadcast_out;

    // --- outgoing message handler ---
    void tunnelbroadcast_out_do_cyclic(void);
//OWEND
};

#endif /* AP_UAVCAN_H_ */




/*
about priorities:

uc_transfer.cpp:
const TransferPriority TransferPriority::Default((1U << BitLen) / 2);
const TransferPriority TransferPriority::MiddleLower((1U << BitLen) / 2 + (1U << BitLen) / 4);
const TransferPriority TransferPriority::OneHigherThanLowest(NumericallyMax - 1);
const TransferPriority TransferPriority::OneLowerThanHighest(NumericallyMin + 1);
const TransferPriority TransferPriority::Lowest(NumericallyMax);

transfer.hpp:
static const uint8_t BitLen = 5U;
static const uint8_t NumericallyMax = (1U << BitLen) - 1;
static const uint8_t NumericallyMin = 0;

me:
const uavcan::TransferPriority TwoLowerThanHighest(uavcan::TransferPriority::NumericallyMin + 2);
const uavcan::TransferPriority OneHigherThanDefault((1U << uavcan::TransferPriority::BitLen) / 2 - 1);

=>

OneLowerThanHighest     = 1
Default                 = 16
MiddleLower             = 24
OneHigherThanLowest     = 30
Lowest                  = 31

TwoLowerThanHighest     = 2
OneHigherThanDefault    = 15

usage:
act_out_array[_uavcan_i]->setPriority(uavcan::TransferPriority::OneLowerThanHighest);   = 1
esc_raw[_uavcan_i]->setPriority(uavcan::TransferPriority::OneLowerThanHighest);         = 1
rgb_led[_uavcan_i]->setPriority(uavcan::TransferPriority::OneHigherThanLowest);         = 30

my "old" usage:
_uc4hnotify.priority = uavcan::TransferPriority::MiddleLower;                           = 24
_tunnelbroadcast_out.priority[tunnel_index] = uavcan::TransferPriority::MiddleLower;    = 24

notifications can be low, but tunnels should be higher, so set

_tunnelbroadcast_out.priority[tunnel_index] = uavcan::TransferPriority::OneHigherThanDefault;  = 15


*/

