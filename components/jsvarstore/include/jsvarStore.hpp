#ifndef JSVARSTORE_H
#define JSVARSTORE_H

#include <cstdint>
#include <map>
#include <string>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"


class JsvarStore {

public:
    JsvarStore();
    ~JsvarStore();

    //updates with new data from stream. Parses at most one variable before returning. Returns the name of the parsed variable.
    std::string handleChar(const char &c);

    //returns all vars in the source formatting and checks for stale vars
    std::string dumpVars();

    //return the content of a specific var. Returns empty string if error or var not found.
    std::string getVar(const std::string &varName) const;

    //reset the parser
    void reset();

protected:

private:

    struct SVar{
        std::string data;
        uint64_t writeTime;
    };

    //semaphore for data access
    SemaphoreHandle_t mMutex;

    //hold all the data as strings in a map for easy access
    std::map<std::string,SVar> mVars;

    //holds the current state of the parser
    uint8_t mState;

    //holds the chars parsed in the current state
    uint8_t mCounter;

    //holds the currently parsed variable name
    std::string mVarName;

    //holds the content parsed so far
    std::string mVarContent;

    //debug
    unsigned long timeTaken;

    static const uint32_t DATA_TIMEOUT_MS = 5000;
};



#endif
