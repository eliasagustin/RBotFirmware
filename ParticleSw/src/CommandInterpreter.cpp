// RBotFirmware
// Rob Dobson 2016

#include "WorkflowManager.h"
#include "CommandInterpreter.h"
#include "CommandExtender.h"
#include "RobotController.h"
#include "GCodeInterpreter.h"

CommandInterpreter::CommandInterpreter(WorkflowManager* pWorkflowManager, RobotController* pRobotController)
{
    _pWorkflowManager = pWorkflowManager;
    _pRobotController = pRobotController;
    _pCommandExtender = new CommandExtender(this);
}

void CommandInterpreter::setSequences(const char* configStr)
{
    Log.trace("CmdInterp setSequences %s", configStr);

    // Simply pass the whole config to the extender at present
    _pCommandExtender->setSequences(configStr);
}

const char* CommandInterpreter::getSequences()
{
    return _pCommandExtender->getSequences();
}

void CommandInterpreter::setPatterns(const char* configStr)
{
    Log.trace("CmdInterp setPatterns %s", configStr);

    // Simply pass the whole config to the extender at present
    _pCommandExtender->setPatterns(configStr);
}

const char* CommandInterpreter::getPatterns()
{
    return _pCommandExtender->getPatterns();
}

bool CommandInterpreter::canAcceptCommand()
{
    if (_pWorkflowManager)
        return !_pWorkflowManager->isFull();
    return false;
}

bool CommandInterpreter::queueIsEmpty()
{
    if (_pWorkflowManager)
        return _pWorkflowManager->isEmpty();
    return false;
}

bool CommandInterpreter::setWifi(const char* pCmdStr)
{
    // Get args
    const char* pArgsPos = strstr(pCmdStr, " ");
    if (pArgsPos == 0)
        return false;
    // SSID
    const char* pSSIDPos = pArgsPos + 1;
    pArgsPos = strstr(pSSIDPos, " ");
    if (pArgsPos == 0)
        return false;
    int stLen = pArgsPos - pSSIDPos;
    char* pSsidStr = new char[stLen + 1];
    for (int i = 0; i < stLen; i++)
        pSsidStr[i] = pSSIDPos[i];
    pSsidStr[stLen] = 0;
    // password
    const char* pPword = pArgsPos + 1;
    // Set WiFi info
    WiFi.setCredentials(pSsidStr, pPword);
    Log.info("CmdInterp: WiFi SSID %s pwLen %d", pSsidStr, strlen(pPword));
    delete [] pSsidStr;
    return true;
}

bool CommandInterpreter::processSingle(const char* pCmdStr)
{
    bool rslt = false;
    // Check if this is an immediate command
    if (strcasecmp(pCmdStr, "pause") == 0)
    {
        if (_pRobotController)
            _pRobotController->pause(true);
        rslt = true;
    }
    else if (strcasecmp(pCmdStr, "resume") == 0)
    {
        if (_pRobotController)
            _pRobotController->pause(false);
        rslt = true;
    }
    else if (strcasecmp(pCmdStr, "stop") == 0)
    {
        if (_pRobotController)
            _pRobotController->stop();
        if (_pWorkflowManager)
            _pWorkflowManager->clear();
        if (_pCommandExtender)
            _pCommandExtender->stop();
        rslt = true;
    }
    else if (strstr(pCmdStr, "setwifi") == pCmdStr)
    {
        rslt = setWifi(pCmdStr);
    }
    else if (strstr(pCmdStr, "clearwifi") == pCmdStr)
    {
        WiFi.clearCredentials();
        Log.info("CmdInterp: WiFi Credentials Cleared");
        rslt = true;
    }
    else if (_pWorkflowManager)
    {
        // Send the line to the workflow manager
        if (strlen(pCmdStr) != 0)
            rslt = _pWorkflowManager->add(pCmdStr);
    }
    Log.trace("CmdInterp procSingle rslt %s", rslt ? "OK" : "Fail");

    return rslt;
}

bool CommandInterpreter::process(const char* pCmdStr, int cmdIdx)
{
    // Handle the case of a single string
    if (strstr(pCmdStr, ";") == NULL)
    {
        return processSingle(pCmdStr);
    }

    // Handle multiple commands (semicolon delimited)
    /*Log.trace("CmdInterp process %s", pCmdStr);*/
    const int MAX_TEMP_CMD_STR_LEN = 1000;
    const char* pCurStr = pCmdStr;
    const char* pCurStrEnd = pCmdStr;
    int curCmdIdx = 0;
    while (true)
    {
        // Find line end
        if ((*pCurStrEnd == ';') || (*pCurStrEnd == '\0'))
        {
            // Extract the line
            int stLen = pCurStrEnd-pCurStr;
            if ((stLen == 0) || (stLen > MAX_TEMP_CMD_STR_LEN))
                break;

            // Alloc
            char* pCurCmd = new char [stLen+1];
            if (!pCurCmd)
                break;
            for (int i = 0; i < stLen; i++)
                pCurCmd[i] = *pCurStr++;
            pCurCmd[stLen] = 0;

            // process
            if (cmdIdx == -1 || cmdIdx == curCmdIdx)
            {
                /*Log.trace("cmdProc single %d %s", stLen, pCurCmd);*/
                processSingle(pCurCmd);
            }
            delete [] pCurCmd;

            // Move on
            curCmdIdx++;
            if (*pCurStrEnd == '\0')
                break;
            pCurStr = pCurStrEnd + 1;
        }
        pCurStrEnd++;
    }
    return true;
}

void CommandInterpreter::service()
{
    // Pump the workflow here
    // Check if the RobotController can accept more
    if (_pRobotController->canAcceptCommand())
    {
        CommandElem cmdElem;
        bool rslt = _pWorkflowManager->get(cmdElem);
        if (rslt)
        {
            Log.trace("CmdInterp getWorkflow rlst=%d (waiting %d), %s", rslt,
                            _pWorkflowManager->numWaiting(),
                            cmdElem.getString().c_str());

            // Check for extended commands
            if (_pCommandExtender)
                rslt = _pCommandExtender->procCommand(cmdElem.getString().c_str());

            // Check for GCode
            if (!rslt)
                GCodeInterpreter::interpretGcode(cmdElem, _pRobotController, true);
        }
    }

    // Service command extender (which pumps the state machines associated with extended commands)
    _pCommandExtender->service();
}