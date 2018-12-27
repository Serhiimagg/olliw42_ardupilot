#pragma once

#include <AP_UAVCAN/AP_UAVCAN.h>
#include "AP_BattMonitor.h"
#include "AP_BattMonitor_Backend.h"

#define AP_BATTMONITOR_UAVCAN_TIMEOUT_MICROS         5000000 // sensor becomes unhealthy if no successful readings for 5 seconds

class AP_BattMonitor_UAVCAN : public AP_BattMonitor_Backend
{
public:

    enum BattMonitor_UAVCAN_Type {
        UAVCAN_BATTERY_INFO = 0,
//OW
        UAVCAN_UC4HGENERICBATTERY_INFO = 83,
        UAVCAN_ESCSTATUS = 84,
//OWEND
    };

    /// Constructor
    AP_BattMonitor_UAVCAN(AP_BattMonitor &mon, AP_BattMonitor::BattMonitor_State &mon_state, BattMonitor_UAVCAN_Type type, AP_BattMonitor_Params &params);

    /// Read the battery voltage and current.  Should be called at 10hz
    void read() override;

    void init() override;

    bool has_current() const override {
        return true;
    }

    void handle_bi_msg(float voltage, float current, float temperature) override;
//OW
    void handle_uc4hgenericbatteryinfo_msg(float voltage, float current, float charge, float energy, uint16_t cells_num, float* cells) override;
    void handle_escstatus_msg(uint16_t esc_index, float voltage, float current) override;

    //see AP_BattMonitorSMBus_Maxell as example for cell handling
    bool has_cell_voltages() const override { return _has_cell_voltages; }
//OWEND

protected:
    BattMonitor_UAVCAN_Type _type;

//OW
    bool _has_cell_voltages; //backends flag this as true once they have received a valid cell voltage report

    struct escstatus_data { //this is as received from uavcan.equipment.esc.Status
        float voltage;
        float current;
        //private
        uint32_t time_micros;
        float consumed_charge_mah;
        float consumed_energy_wh;
        bool healthy;
        bool detected;
    };
    struct escstatus_data _escstatus[8];
    uint16_t _escstatus_maxindex;
//OWEND
};
