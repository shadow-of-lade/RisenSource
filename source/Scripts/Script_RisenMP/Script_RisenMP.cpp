#include <winsock2.h>
#include <windows.h>
#include "Script.h"
#include "Network.h"

#pragma comment(lib, "ws2_32.lib")

GEInt GE_STDCALL CON_risenmp(gCScriptProcessingUnit *, GELPVoid, GELPVoid, GEInt a_iArgs)
{
    gSConScriptArgs & Args = *reinterpret_cast<gSConScriptArgs *>(a_iArgs);

    switch (Args.m_enumEvent)
    {
    case gEConScriptEvent_Help:
        Args.m_strResult =
            L"RisenMP - LAN multiplayer prototype\n"
            L"Usage:\n"
            L"  risenmp host [port]\n"
            L"  risenmp join <IPv4> [port]\n"
            L"  risenmp status\n"
            L"  risenmp stop\n"
            L"Example: risenmp join 192.168.1.20 28000";
        return GETrue;

    case gEConScriptEvent_Execute:
        {
            GEInt Count = Args.m_arrParams.GetCount();
            bCUnicodeString Command;
            char Status[256];

            if (Count < 1)
            {
                Args.m_strResult = L"Usage: risenmp <host|join|status|stop>";
                return GEFalse;
            }

            Command = Args.m_arrParams.GetAt(0);

            if (Command.CompareNoCase(L"host") == 0)
            {
                GEInt Port = RisenMP::DefaultPort;
                if (Count >= 2)
                    Port = static_cast<GEInt>(Args.m_arrParams.GetAt(1).GetDouble(0));

                if (Port < 1 || Port > 65535)
                {
                    Args.m_strResult = L"Invalid UDP port.";
                    return GEFalse;
                }

                if (RisenMP::StartHost(static_cast<unsigned short>(Port)))
                    Args.m_strResult = L"RisenMP: LAN host started. Give your IPv4 address and UDP port to other players.";
                else
                    Args.m_strResult = L"RisenMP: failed to start host. Check whether the UDP port is already in use.";
                return GETrue;
            }

            if (Command.CompareNoCase(L"join") == 0)
            {
                GEInt Port = RisenMP::DefaultPort;
                bCUnicodeString Address;
                bCString AnsiAddress;

                if (Count < 2)
                {
                    Args.m_strResult = L"Usage: risenmp join <IPv4> [port]";
                    return GEFalse;
                }

                Address = Args.m_arrParams.GetAt(1);
                AnsiAddress = Address.GetAnsiText();
                if (Count >= 3)
                    Port = static_cast<GEInt>(Args.m_arrParams.GetAt(2).GetDouble(0));

                if (Port < 1 || Port > 65535)
                {
                    Args.m_strResult = L"Invalid UDP port.";
                    return GEFalse;
                }

                if (RisenMP::StartJoin(AnsiAddress.GetBuffer(0), static_cast<unsigned short>(Port)))
                    Args.m_strResult = L"RisenMP: connecting to host...";
                else
                    Args.m_strResult = L"RisenMP: could not start connection.";
                return GETrue;
            }

            if (Command.CompareNoCase(L"status") == 0)
            {
                RisenMP::GetStatus(Status, sizeof(Status));
                Args.m_strResult = bCString(Status).GetUnicodeText();
                return GETrue;
            }

            if (Command.CompareNoCase(L"stop") == 0)
            {
                RisenMP::Stop();
                Args.m_strResult = L"RisenMP: session stopped.";
                return GETrue;
            }

            Args.m_strResult = L"Unknown command. Type 'risenmp' for help.";
            return GEFalse;
        }

    case gEConScriptEvent_Complete:
    default:
        return GEFalse;
    }
}

extern "C" __declspec(dllexport) gSScriptInit const * GE_STDCALL ScriptInit(void)
{
    static gSScriptInit ScriptData;
    ScriptData.m_arrScriptAIStates.Clear();
    ScriptData.m_arrScriptAIFunctions.Clear();
    ScriptData.m_arrScriptAICallbacks.Clear();
    ScriptData.m_arrScripts.Clear();
    ScriptData.m_arrScripts.Add(gSScriptInitScript("CON_risenmp", __FILE__, CON_risenmp));
    return &ScriptData;
}

BOOL APIENTRY DllMain(HMODULE Module, DWORD Reason, LPVOID)
{
    if (Reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(Module);
    else if (Reason == DLL_PROCESS_DETACH)
        RisenMP::Stop();
    return TRUE;
}
