#include "jsvarStore.hpp"

#include "esp_timer.h"

namespace {

//milliseconds since boot (mirrors the Arduino millis() this code was written against)
inline uint64_t millis()
{
    return static_cast<uint64_t>(esp_timer_get_time() / 1000);
}

} // namespace

JsvarStore::JsvarStore()
    : mState(0)
    , mCounter(0)
{
    mMutex = xSemaphoreCreateMutex();
}

JsvarStore::~JsvarStore()
{
    vSemaphoreDelete(mMutex);
}

std::string JsvarStore::handleChar(const char &c)
{

    if(mState == 0) //search for "var " with the space
    {
        const char var[] = "var ";
        if(mCounter <= 4 && var[mCounter] == c)
        {
            mCounter ++;

            if(mCounter == 4) //token found sucessfully
            {
                mCounter = 0;
                mState = 1;
            }
        }
        else //error case
        {
            reset();
        }

    }
    else if(mState == 1) //parse variable name until '='
    {
        if(c == '=') //end of var name
        {
            mState = 2;
            mCounter = 0;
        }
        else if(mCounter < 10) //max var name length is 10
        {
             //filter for reasonable characters
            if((c >= 'a' && c <= 'z') //lower case letters are most common, check first
                || (c >= 'A' && c <= 'Z')
                || (c >= '0' && c <= '9'))
            {
                mVarName += c;
                mCounter ++;
            }
            else if(c == ' ') //allow one space character at the end of the variable name
            {
                mCounter = 10;
            }
            else
            {
                reset();
            }
        }
        else //error case
        {
            reset();
        }
    }
    else if(mState == 2) //expect a quotation mark or a [
    {
        if(c == '\"' || c == '[')
        {
            mState = 3;
            mVarContent += c;
        }
        else //error case
        {
            reset();
        }
    }
    else if(mState == 3) //parse content until '\"' or ']'
    {
        const char first = mVarContent.empty() ? '\0' : mVarContent[0];
        if((c == '\"' && first == '\"') || (c == ']' && first == '[')) //end of content
        {
            mState = 4;
            mCounter = 0;
            mVarContent += c;
        }
        else if(mCounter < 250) //max content length is 250
        {
            mVarContent += c;
            mCounter ++;
        }
        else //error case
        {
            reset();
        }
    }
    else if(mState == 4) //expect semicolon
    {
        if(c == ';') //line was valid, commit
        {
            const char nameFirst = mVarName.empty() ? '\0' : mVarName[0];
            if(nameFirst != 'h') //special treatment of history download
            {
                uint64_t time = millis();
                if( xSemaphoreTake( mMutex, (TickType_t) 5 ) )
                {
                    auto var = mVars.find(mVarName);
                    if(var == mVars.end())
                    {
                        mVars.insert(std::pair<std::string,SVar>(mVarName, {mVarContent, time})); //insert in our stored data
                    }
                    else
                    {
                        var->second.data = mVarContent;
                        var->second.writeTime = time;
                    }
                    xSemaphoreGive(mMutex);
                }
            }

            std::string varName = mVarName;
            reset();

            return varName;
        }
        else //error case
        {
            reset();
        }
    }

    return std::string();
}


std::string JsvarStore::dumpVars()
{
    std::string dump;
    dump.reserve(2000); //reserve a huge block, this should fit everything

    uint64_t time = millis();

    if( xSemaphoreTake( mMutex, (TickType_t) 50 ) )
    {
        auto it = mVars.begin();
        while(it != mVars.end())
        {
            if(time - it->second.writeTime > DATA_TIMEOUT_MS)
            {
                it = mVars.erase(it); // erase stale variable
            }
            else
            {
                //reconstruct the original line syntax
                dump += "var ";
                dump += it->first;
                dump += "=";
                dump += it->second.data;
                dump += ";\r\n";

                ++it;
            }
        }
        xSemaphoreGive(mMutex);
    }

    return dump;
}


std::string JsvarStore::getVar(const std::string &varName) const
{
    std::string res;
    if( xSemaphoreTake( mMutex, (TickType_t) 50 ) )
    {
        auto var = mVars.find(varName);
        if(var != mVars.cend())
        {
            res = var->second.data; //return a copy of the data
        }
        xSemaphoreGive(mMutex);
    }
    return res;
}

void JsvarStore::reset()
{
    mState = 0;
    mCounter = 0;
    mVarName.clear();
    mVarContent.clear();
    timeTaken = 0;
}
