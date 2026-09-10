#include "sbms_json.hpp"

#include "app_settings.hpp"

void toJsonSBMS(const SbmsData &sbms, const CellMinMax &hist, JsonDocument &doc)
{
    doc.clear();

    doc["time"]["year"]   = sbms.year;
    doc["time"]["month"]  = sbms.month;
    doc["time"]["day"]    = sbms.day;
    doc["time"]["hour"]   = sbms.hour;
    doc["time"]["minute"] = sbms.minute;
    doc["time"]["second"] = sbms.second;

    doc["soc"] = sbms.stateOfChargePercent;

    JsonArray volt = doc["cellsMV"].to<JsonArray>();
    JsonArray vlo  = doc["cellsMinMV"].to<JsonArray>();
    JsonArray vhi  = doc["cellsMaxMV"].to<JsonArray>();
    for(uint8_t i = 0; i < 8; i++)
    {
        volt.add(sbms.cellVoltageMV[i]);
        vlo.add(hist.low(i));   // rolling 24 h low, 0 = no data yet
        vhi.add(hist.high(i));  // rolling 24 h high, 0 = no data yet
    }

    doc["tempInt"] = sbms.temperatureInternalTenthC / 10.0;
    doc["tempExt"] = sbms.temperatureExternalTenthC / 10.0;

    JsonObject curr = doc["currentMA"].to<JsonObject>();
    curr["battery"] = sbms.batteryCurrentMA;
    curr["pv1"]     = sbms.pv1CurrentMA;
    curr["pv2"]     = sbms.pv2CurrentMA;
    curr["extLoad"] = sbms.extLoadCurrentMA;

    doc["ad2"] = sbms.ad2;
    doc["ad3"] = sbms.ad3;
    doc["ad4"] = sbms.ad4;

    doc["heat1"] = sbms.heat1;
    doc["heat2"] = sbms.heat2;

    JsonObject flags = doc["flags"].to<JsonObject>();
    flags["OV"]   = sbms.getFlag(SbmsData::FlagBit::OV);
    flags["OVLK"] = sbms.getFlag(SbmsData::FlagBit::OVLK);
    flags["UV"]   = sbms.getFlag(SbmsData::FlagBit::UV);
    flags["UVLK"] = sbms.getFlag(SbmsData::FlagBit::UVLK);
    flags["IOT"]  = sbms.getFlag(SbmsData::FlagBit::IOT);
    flags["COC"]  = sbms.getFlag(SbmsData::FlagBit::COC);
    flags["DOC"]  = sbms.getFlag(SbmsData::FlagBit::DOC);
    flags["DSC"]  = sbms.getFlag(SbmsData::FlagBit::DSC);
    flags["CELF"] = sbms.getFlag(SbmsData::FlagBit::CELF);
    flags["OPEN"] = sbms.getFlag(SbmsData::FlagBit::OPEN);
    flags["LVC"]  = sbms.getFlag(SbmsData::FlagBit::LVC);
    flags["ECCF"] = sbms.getFlag(SbmsData::FlagBit::ECCF);
    flags["CFET"] = sbms.getFlag(SbmsData::FlagBit::CFET);
    flags["EOC"]  = sbms.getFlag(SbmsData::FlagBit::EOC);
    flags["DFET"] = sbms.getFlag(SbmsData::FlagBit::DFET);

    //optional calculated fields
    if(g_settings.data_sbms_diff)
    {
        uint16_t min = static_cast<uint16_t>(-1);
        uint16_t max = 0;

        for(uint8_t i = 0; i < 8; i++)
        {
            uint16_t v = sbms.cellVoltageMV[i];
            if(v > 0 && v < min) min = v;
            if(v > 0 && v > max) max = v;
        }

        flags["delta"] = max - min;
    }
}
