//
// Copyright (c) Microsoft Corporation.  All rights reserved.
//
//
// This source code is licensed under Microsoft Shared Source License
// Version 1.0 for Windows CE.
// For a copy of the license visit http://go.microsoft.com/fwlink/?LinkId=3223.
//
#include <winsock2.h>
#include <iptypes.h>
#include <Iphlpapi.h>
#include <CReg.hxx>
#include <notify.h>
#include <ncb.h>
#include <windev.h>

#include "SMB_Globals.h"
#include "NetbiosTransport.h"
#include "Utils.h"
#include "CriticalSection.h"
#include "nb.h"




//do any explicit inits for globals setup in the NETBIOS_TRANSPORT
//  namespace
//
//
//  Running     Accepting     State
//     X                        During shutdown
//                  X           Error
//     X            X           Normal Use
//                              Stopped
//
//
LONG NETBIOS_TRANSPORT::g_fIsRunning = FALSE;
LONG NETBIOS_TRANSPORT::g_fIsAccepting = FALSE;

using namespace ce;
using namespace NETBIOS_TRANSPORT;

struct RecvNode {
    NetBIOSAdapter *pAdapter;
    HANDLE MyHandle;
    USHORT usLSN;
    CHAR LANA;
};

#define DELETE_LANA_FL      0x01

#define NETBIOS_CONNECTION_ALLOC     pool_allocator<5, RecvNode>
pool_allocator<10, ncb>       SMB_Globals::g_NCBAllocator;

//
// This is a linked list of netbios adapters that have been found
ce::list<NetBIOSAdapter *> NBAdapterDeleteStack;
ce::list<NetBIOSAdapter *> NBAdapterStack;

CRITICAL_SECTION csAdapterDiscoverList;
CRITICAL_SECTION csSendLock;
ce::list<char>   AdapterDiscoverList;
ce::list<RecvNode, NETBIOS_CONNECTION_ALLOC > ActiveRecvList;
CRITICAL_SECTION csActiveRecvListLock;

extern CRITICAL_SECTION g_csDriverLock;
extern DWORD SMB_Deinit(DWORD dwClientContext);
extern DWORD SMB_Init(DWORD dwClientContext);



//
// Forward declare any functions
HRESULT InitLana(UCHAR ulLANIdx);


NetBIOSAdapter::NetBIOSAdapter(BYTE _bIDX) : bIDX(_bIDX), nameNum(0xFF), fStopped(FALSE)
{  
    HRESULT hr = E_FAIL;
    RETAILMSG(1, (TEXT("SMBSRV:InitLana: Starting NetBios at index: %d"), bIDX));

    //
    // Init state by zeroing out threads
    hListenThread = NULL;
                                   
    //
    //  Spin threads to handle listening and recving
    hListenThread  = CreateThread(NULL, 0, SMBSRVR_NetbiosListenThread, (LPVOID)this, CREATE_SUSPENDED, NULL);
    if(NULL == hListenThread) {
        TRACEMSG(ZONE_ERROR, (TEXT("SMBSRV:InitLana: CreateThread failed starting NB Listen:%d"), GetLastError()));
        goto Done;
    }
       
    
    ASSERT(FAILED(hr));
    hr = S_OK;
    
    Done:
        //
        // If we failed, set the shutdown flag
        if(FAILED(hr)) {
            InterlockedExchange(&fStopped,TRUE);
        }
        
        //  
        // Now crank up all threads (if we error'ed they will all just return quickly)
        if(NULL != hListenThread) {
            if(0xFFFFFFFF == ResumeThread(hListenThread)) {
                    TRACEMSG(ZONE_ERROR, (L"SMBSRV:Resuming  listen thread %d FAILED!"));
                    CloseHandle(hListenThread);
                    hListenThread = NULL; //Createthread returns null, not invalid handle value!
            }
        }
}

NetBIOSAdapter::~NetBIOSAdapter() 
{
   //
   // If the adapter is running, kill it off 
   if(!fStopped && FAILED(HaltAdapter())) {
      TRACEMSG(ZONE_ERROR, (L"SMBSRV: NetBIOSAdapter halting failed! (idx:%d)", this->bIDX));   
      ASSERT(FALSE);
   }    
}


HRESULT 
NetBIOSAdapter::HaltAdapter()
{
    ncb tNcb;
    BYTE *pCName;
    UCHAR retVal;
    HRESULT hr = S_OK;
    ce::list<RecvNode, NETBIOS_CONNECTION_ALLOC >::iterator itARList;
    CCritSection csLock(&csActiveRecvListLock);
    
    if(TRUE == fStopped) {
        return S_OK;
    }
    
    //
    // Render the sending thread worthless
    InterlockedExchange(&fStopped, TRUE);   
    
    //
    // Get registered CName
    if(FAILED(hr = this->GetCName(&pCName))) {
        TRACEMSG(ZONE_ERROR, (TEXT("SMBSRV:InitLana: couldnt get CName")));
        goto Done;    
    }
  
    // 
    // Hangup anything thats on our LANA;    
    csLock.Lock();    
    for(itARList=ActiveRecvList.begin(); itARList!=ActiveRecvList.end(); itARList++) {
        //
        // If this is on our LANA, hang it up (this should stop any active connections)
        if((*itARList).LANA == bIDX) {
            tNcb.ncb_command = NCBHANGUP;
            tNcb.ncb_lana_num = bIDX;  
            tNcb.ncb_lsn = (*itARList).usLSN;  
            memcpy(tNcb.ncb_name, pCName, 16);
            
            retVal = Netbios(&tNcb);
            if (retVal != 0 || (retVal = tNcb.ncb_retcode) != 0) {
                TRACEMSG(ZONE_ERROR, (TEXT("SMBSRV:InitLana: NCBHANGUPANY returned %s"), NETBIOS_TRANSPORT::NCBError(retVal)));
                ASSERT(FALSE); 
            }    
        }        
    } 
    csLock.UnLock();
    
    // 
    // Un-register the name (this will cause blocked f()'s to stop and cleanup)
    tNcb.ncb_command = NCBDELNAME;
    tNcb.ncb_lana_num = bIDX;  
    memcpy(tNcb.ncb_name, pCName, 16);   
    retVal = Netbios(&tNcb);
    if (retVal != 0 || (retVal = tNcb.ncb_retcode) != 0) {
        TRACEMSG(ZONE_ERROR, (TEXT("SMBSRV:InitLana: NCBDELNAME returned %s"), NETBIOS_TRANSPORT::NCBError(retVal)));
        ASSERT(FALSE);
    }
       
    //
    // Wait for the listening thread to stop
    if(WAIT_FAILED == WaitForSingleObject(hListenThread, INFINITE)) {
        TRACEMSG(ZONE_INIT, (L"SMBSRV:Waiting for LISTEN thread FAILED!"));
        ASSERT(FALSE);
        hr = E_FAIL;
    }    
    TRACEMSG(ZONE_INIT, (L"SMBSRV:Listen thread has been stopped!"));
    
    //
    // Wait for the recving threads to stop
    for(;;) {
        csLock.Lock();              
        if(0 == ActiveRecvList.size()) {
            break;
        }
        RecvNode *myNode = &(ActiveRecvList.front());
        HANDLE h = myNode->MyHandle;
        ActiveRecvList.pop_front();
        csLock.UnLock();

        if(WAIT_FAILED == WaitForSingleObject(h, INFINITE)) {
            TRACEMSG(ZONE_INIT, (L"SMBSRV:Waiting for RECV thread FAILED!"));
            ASSERT(FALSE);  
        }         
    }  
    TRACEMSG(ZONE_INIT, (L"SMBSRV:Recv threads have been stopped!"));     
    
    Done:
        return hr;
}


BOOL
NetBIOSAdapter::DuringShutDown()
{
    //
    // We are in the process of shutting down when we are running
    //   but not accepting 
    return fStopped;
}


//
// When an adapter shows up init it
static void
NetBIOSNotifyFunc(uchar lananum, int flags, int unused)
{
    CCritSection csLock(&csAdapterDiscoverList);
    csLock.Lock();
        
    while(0 != NBAdapterDeleteStack.size()) {    
        NetBIOSAdapter *pAdapter = NBAdapterDeleteStack.front();
        NBAdapterDeleteStack.pop_front();
        ASSERT(NULL != pAdapter);
        
        //
        // Check out the list of adapters on the deleted list and purge it now        
        ce::list<char>::iterator itDiscover;
        ce::list<char>::iterator itDiscoverEnd;
        ce::list<NetBIOSAdapter *>::iterator itAdap;
        ce::list<NetBIOSAdapter *>::iterator itAdapEnd;
        BOOL fFound = FALSE;
        char LANA = (char)0xFF;
             
        //
        // Seek our our adapter from the adapter stack and terminate it
        for(itAdap = NBAdapterStack.begin(), itAdapEnd = NBAdapterStack.end(); itAdap != itAdapEnd; ++itAdap) {
            if(pAdapter == *itAdap) {
                LANA = pAdapter->GetLANA();
                fFound = TRUE;
                NetBIOSAdapter *pDel =  (*itAdap);
                NBAdapterStack.erase(itAdap++);
                ASSERT(NULL != pDel);
                fFound = TRUE;
                
                TRACEMSG(ZONE_INIT, (L"SMBSRV:NetBIOSNotifyFunc: Lana %u being terminated",lananum));
                
                //
                // Destroy our adapter and its threads (which prob are dead by now anyway)
                if(pDel && FAILED(pDel->HaltAdapter())) {
                    TRACEMSG(ZONE_INIT, (L"SMBSRV:NetBIOSNotifyFunc: Lana %u termination failure!",lananum));
                    ASSERT(FALSE);
                } 
                if(pDel) {
                    delete pDel;
                }                
                break;
            }
        }
        
        //
        // Remove the adapter from the active list
        if(fFound) {
            for(itDiscover = AdapterDiscoverList.begin(), itDiscoverEnd = AdapterDiscoverList.end(); itDiscover != itDiscoverEnd; ++itDiscover) {
                if(LANA == (*itDiscover)) {
                    AdapterDiscoverList.erase(itDiscover++);
                    break;
                }
            } 
        }     
    }
    
    

    //
    // Init a lan adapter
    if (flags & LANA_UP_FL) {
        if(g_fIsRunning) {
            if(FAILED(InitLana(lananum))) {
                TRACEMSG(ZONE_ERROR, (L"SMBSRV:Error initing lana(%d)!",lananum));         
            }
        } else {
            TRACEMSG(ZONE_NETBIOS, (L"SMBSRV: notification routine turned off... caching result"));
        }        
        AdapterDiscoverList.push_back(lananum);
    }               
    TRACEMSG(ZONE_INIT, (L"SMBSRV:NetBIOSNotifyFunc: Lana %u",lananum));
}



DWORD 
NETBIOS_TRANSPORT::SMBSRVR_NameNotifyThread(LPVOID unused)
{
    HANDLE h = NULL;
    StringConverter EventName;
    CCritSection csLock(&g_csDriverLock);
    
    //
    // Make sure we have the WMGR API -- if not return out
    for(UINT i=0; i<20; i++) {        
        if (FALSE == IsAPIReady(SH_WMGR)) {
            TRACEMSG(ZONE_ERROR, (L"SMBSRV: SH_WMGR API not ready! -- waiting %d/20", i));
            Sleep(1000);
        } else {
            break;
        }
    }
    if(FALSE == IsAPIReady(SH_WMGR)) {
        TRACEMSG(ZONE_ERROR, (L"SMBSRV: SH_WMGR API not ready!")); 
        goto Done;
    }    
    
    EventName.append(NAMED_EVENT_PREFIX_TEXT);
    EventName.append("SMB_NameNotifyEvent");
          
    //
    // Create a listen event
    if(NULL == (h = CreateEvent(NULL, FALSE, FALSE, L"SMB_NameNotifyEvent"))) {
        TRACEMSG(ZONE_ERROR, (L"SMBSRV: Cant create named notify event!"));        
        goto Done;
    }
    
    //
    // Loop forever
    for(;;) {  
        if(FALSE == CeRunAppAtEvent(EventName.GetUnsafeString(), NOTIFICATION_EVENT_MACHINE_NAME_CHANGE)) {
            TRACEMSG(ZONE_ERROR, (L"SMBSRV: Waiting for event SMB_NameNotifyEvent failed!")); 
            UINT j;
            //
            // I've seen IsAPIReady() return when CeRunAppAtEvent isnt ready -- try a few times before giving up
            for(j=0; j<20; j++) {   
                if(FALSE != CeRunAppAtEvent(EventName.GetUnsafeString(), NOTIFICATION_EVENT_MACHINE_NAME_CHANGE)) {
                    break;
                }
                Sleep(1000);
            }
            if(j == 20) {
                RETAILMSG(1, (L"SMBSRV: ERROR Could not get notification for name notification!"));   
                goto Done;
            }
        }
        
        if(WAIT_OBJECT_0 != WaitForSingleObject(h, INFINITE)) {
            TRACEMSG(ZONE_ERROR, (L"SMBSRV: Error waiting for event SMB_NameNotifyEvent failed!"));        
            goto Done;
        }
        RETAILMSG(1, (L"SMBSRV: Got notification that hostname changed -- restarting server!"));   
        csLock.Lock();
        SMB_Deinit(0);          
        SMB_Init(0);
        csLock.UnLock();
    }

    Done:    
        if(NULL != h) {
            CloseHandle(h);
        }
        ASSERT(FALSE); //should NEVER exit!
        return 0;
}



//
// Listen on name changes -- if/when one occurs restart the server
HRESULT StartListenOnNameChange()
{
    HRESULT hr = E_FAIL;
    static BOOL fInited = FALSE;    
    
    //
    // Note we dont keep the thread handle around since it lives forever
    if(FALSE == fInited) {
        HANDLE h = CreateThread(NULL, 0, SMBSRVR_NameNotifyThread, (LPVOID)0, 0, NULL);
        CloseHandle(h);
        fInited = TRUE;
    }
    
    return 0;
}

//
// Register with netbios for adapters (it will fire with already known adapters
//   first)
HRESULT StartEnumeratingNetbios() 
{    
    HRESULT hr = E_FAIL;
    static BOOL fInited = FALSE;
    ASSERT(TRUE == g_fIsRunning);
    
    //
    // We can only init this ONCE -- yeah, its bogus.. but its the way it
    //   is -- so what we do is cache what netbios has sent us and mimic the 
    //   behavior
    if(FALSE == fInited) 
    {   
        InitializeCriticalSection(&csAdapterDiscoverList);        
        ASSERT(0 == AdapterDiscoverList.size());
        
        NetBIOSNotifyFunc(0, LANA_UP_FL, 0); 
        
        //
        // Register with Netbios for notifications of adapters coming up
        if (!NETbiosThunk(0,NB_NET_NOTIFY,(LPVOID)NetBIOSNotifyFunc,0,NULL,0,NULL)) {
            TRACEMSG(ZONE_ERROR,(L"SMBSRV:InitNetBIOS: Error registering callback, is netbios.dll present?\n"));
            hr = E_FAIL;
            goto Done;
        }
        
        fInited = TRUE;
    }  else {
        CCritSection csLock(&csAdapterDiscoverList);
        csLock.Lock();
        
        ce::list<char>::iterator it;
        ce::list<char>::iterator itEnd;

        for(it = AdapterDiscoverList.begin(), itEnd = AdapterDiscoverList.end(); it != itEnd; ++it) {
            if(FAILED(InitLana(*it))) {
                TRACEMSG(ZONE_ERROR, (TEXT("SMBSRV:Error initing lana(%d)!"),*it));          
            }
        }        
    }
    hr = S_OK;
    
    Done:
        return hr;
}


//
// UnRegister with netbios for adapters 
//
HRESULT StopEnumeratingNetbios() 
{    
    ASSERT(FALSE == g_fIsRunning);
    return S_OK;
}


//this is a utility function that is used by NETBIOS to make CE behave a bit
//  more like NT
BYTE
NETBIOS_TRANSPORT::Netbios(ncb * pncb)
{    
    NCB newncb;
    for(;;)
    {                
        //
        // Convert real NCB to WinCE NCB
        //
        switch (pncb->ncb_command) {
            case NCBCALL:       newncb.Command = NB_CALL;              break;
            case NCBLISTEN:     newncb.Command = NB_LISTEN;            break;
            case NCBHANGUP:     newncb.Command = NB_HANGUP;            break;
            case NCBSEND:       newncb.Command = NB_SEND;              break;
            case NCBRECV:       newncb.Command = NB_RECEIVE;           break;
            case NCBRECVANY:    newncb.Command = NB_RECEIVE_ANY;       break;
            case NCBDGSEND:     newncb.Command = NB_SEND_DG;           break;
            case NCBDGRECV:     newncb.Command = NB_RECEIVE_DG;        break;
            case NCBDGSENDBC:   newncb.Command = NB_SEND_BCAST_DG;     break;
            case NCBDGRECVBC:   newncb.Command = NB_RECEIVE_BCAST_DG;  break;
            case NCBADDNAME:    newncb.Command = NB_ADD_NAME;          break;
            case NCBADDMHNAME:  newncb.Command = NB_ADD_NAME;          break;
            case NCBDELNAME:    newncb.Command = NB_DELETE_NAME;       break;
            case NCBHANGUPANY:  newncb.Command = NB_HANGUP_ANY;        break;
            case NCBCANCEL:     newncb.Command = NB_CANCEL;            break;
            case NCBQUERYLANA:  newncb.Command = NB_QUERY_LANA;        break;
            default:
                TRACEMSG(ZONE_ERROR, (L"SMBSRV:Netbios - Illegal NCB command %B!!!\n", pncb->ncb_command));
                pncb->ncb_retcode = NRC_ILLCMD;
                return NRC_ILLCMD;
        }

        newncb.ReturnCode = 0;
        newncb.cTotal =     pncb->ncb_length;
        newncb.LSN =        pncb->ncb_lsn;
        newncb.LanaNum =    pncb->ncb_lana_num;
        memcpy(newncb.CallName, pncb->ncb_callname, sizeof(newncb.CallName));
        memcpy(newncb.Name, pncb->ncb_name, sizeof(newncb.Name));
            
        if (!NETbiosThunk(0,newncb.Command,&newncb, pncb->ncb_length, pncb->ncb_buffer,0, NULL)) 
        {        
            switch (newncb.ReturnCode) 
            {
                case NB_CANCELED:   
                    pncb->ncb_retcode = NRC_CMDCAN;         
                    break;
                case NB_NOMEM:      
                    pncb->ncb_retcode = NRC_NORESOURCES;    
                    break;
                case NB_BADNAME:    
                    pncb->ncb_retcode = NRC_INUSE;          
                    break;
                case NB_DUPNAME:    
                    pncb->ncb_retcode = NRC_DUPNAME;        
                    break;
                case NB_NAME_CFT:   
                    pncb->ncb_retcode = NRC_NAMCONF;        
                    break;
                case NB_NO_SSN:     
                    pncb->ncb_retcode = NRC_SNUMOUT;       
                    break;
                case NB_SSN_CLOSED: 
                    pncb->ncb_retcode = NRC_SCLOSED;        
                    break;
                case NB_SSN_ABORT: 
                    pncb->ncb_retcode = NRC_SABORT;         
                    break;
                case NB_IN_RECV:    
                    pncb->ncb_retcode = NRC_TOOMANY;        
                    break;
                case NB_MORE_DATA:  
                    pncb->ncb_retcode = NRC_INCOMP;         
                    break;
                case NB_IN_SELECT:  
                    pncb->ncb_retcode = NRC_IFBUSY;         
                    break;
                case NB_BAD_COMMAND:
                    pncb->ncb_retcode = NRC_ILLCMD;         
                    break;
                case NB_BAD_LANA:   
                    pncb->ncb_retcode = NRC_ADPTMALFN;      
                    break;
                case NB_KEEP_ALIVE:             
                    // If we given keepalive, just hide this from our caller and thunk again
                    TRACEMSG(ZONE_NETBIOS, (L"SMBSRV:Netbios - got keepalive, retrying packet: %d",newncb.ReturnCode));
                    continue;                             
                case NB_FAILURE:
                default:
                    pncb->ncb_retcode = NRC_IFAIL;
                    break;
            }
        #ifdef DEBUG
                if(10058 != newncb.ReturnCode ) {
                    TRACEMSG(ZONE_ERROR, (L"SMBSRV:Netbios - NCB Error %d\n",newncb.ReturnCode));
                }
        #endif        
            pncb->ncb_length = (WORD) newncb.cTotal;
            pncb->ncb_lsn = newncb.LSN;
            pncb->ncb_lana_num = newncb.LanaNum;
            return pncb->ncb_retcode;
        }
        else 
        {
        
            //
            // Convert back
            //
            pncb->ncb_retcode = 0;
            pncb->ncb_length = (WORD) newncb.cTotal;
            pncb->ncb_lsn = newncb.LSN;
            pncb->ncb_lana_num = newncb.LanaNum;
            memcpy(pncb->ncb_callname, newncb.CallName, sizeof(newncb.CallName));
            memcpy(pncb->ncb_name, newncb.Name, sizeof(newncb.Name));
            return 0;
        }
    }
    ASSERT(FALSE);//should never be here
    return 0;
}   // Netbios


BOOL AllowLana(LANA_INFO *pLanInfo)
{
    //
    // set AdapInfo to a reasonable number of adapters 
    //   if we need more, we will request that later
    BOOL fAllowed = FALSE;
    IP_ADAPTER_INFO AdapInfo[10];    
    PIP_ADAPTER_INFO pAdapInfo = &AdapInfo[0];
    PIP_ADAPTER_INFO pAdapTemp;
    ULONG ulAdapSize = sizeof(AdapInfo);

    CReg CRAdapterList; 
    WCHAR wAdapterList[MAX_PATH];
    CHAR  AdapterList[MAX_PATH];
    
    //
    // Load the adapterlist from the registry
    if(FALSE == CRAdapterList.Open(HKEY_LOCAL_MACHINE, L"Services\\SMBServer")) {        
        RETAILMSG(1, (L"No registy key under HKLM\\Services\\SMBServer\n"));
        goto Done;
    }
    if(FALSE == CRAdapterList.ValueSZ(L"AdapterList", wAdapterList, sizeof(WCHAR) * MAX_PATH)) {
        RETAILMSG(1, (L"Name value under HKLM\\Services\\AdapterList is not there\n"));        
        goto Done;
    }
    if(0x0100007f == pLanInfo->IPAddr && wcsstr(wAdapterList, L"localhost")) {
        RETAILMSG(1, (L"Allowing localhost!\n"));      
        fAllowed = TRUE;
        goto Done;        
    }
    if(0 == WideCharToMultiByte(CP_ACP, 0, wAdapterList, -1, AdapterList, MAX_PATH,NULL,NULL)) {
        TRACEMSG(ZONE_ERROR, (L"Conversion of AdapterList (%s) failed!!!", wAdapterList));
        ASSERT(FALSE);
        goto Done;
    }

    if(ERROR_BUFFER_OVERFLOW == GetAdaptersInfo(pAdapInfo, &ulAdapSize)) {
        pAdapInfo = (IP_ADAPTER_INFO *) new char[ulAdapSize];
        if(NO_ERROR != GetAdaptersInfo(pAdapInfo, &ulAdapSize)) {
            ASSERT(FALSE);
            fAllowed = FALSE;
            goto Done;
        }
    }
    
    // 
    // Now loop through all of the adapters looking for IP addresses
    pAdapTemp = pAdapInfo;
    while(pAdapTemp) {
        // 
        // Loop through all of the ipaddresses in the address list
        IP_ADDR_STRING *pAddr = &(pAdapTemp->IpAddressList);
        while(pAddr) {
            if((ULONG)pLanInfo->IPAddr == inet_addr(pAddr->IpAddress.String)) {
                char *pAdapAllowedTemp = AdapterList;
                  
                //
                // Loop through all the allowed interfaces
                while(pAdapAllowedTemp && NULL != *pAdapAllowedTemp) {
                    char *pAdapEnd = strchr(pAdapAllowedTemp, ';');                
                    ASSERT(FALSE == fAllowed); //if it were allowed we shouldnt be searching!
                    
                    //
                    // Replace the ; with a null
                    if(pAdapEnd) {                      
                        ASSERT(';' == *pAdapEnd);
                        *pAdapEnd = NULL;
                    }
                    
                    //
                    // See if the adapters are equal
                    if(0 != strstr(pAdapTemp->AdapterName, pAdapAllowedTemp)) {
                        TRACEMSG(ZONE_NETBIOS, (L"NETBIOS: found ip addr for lana!"));
                        fAllowed = TRUE;                      
                    }
                    
                    //
                    // Fix the search string back up
                    if(pAdapEnd)
                        *pAdapEnd = ';';
                    
                    //
                    // Quit searching if its allowed otherwise, skip to the next
                    //   string
                    if(fAllowed)
                        goto Done;
                    else {
                        pAdapAllowedTemp = pAdapEnd;                        
                    }                   
                }
                
                
              
               
            }
            pAddr = pAddr->Next;
        }
        
        pAdapTemp = pAdapTemp->Next;
    }
    
    Done:
    
#ifdef DEBUG    
        if(FALSE == fAllowed) {
            TRACEMSG(ZONE_ERROR, (L"NETBIOS: not an allowed interface -- this is okay, but might mean the adapter should be configured -- see docs"));
        }       
#endif 

        if(NULL != pAdapInfo && &AdapInfo[0] != pAdapInfo) {
            delete [] pAdapInfo;
        }
        return fAllowed;
}


//
//  Init a NetBIOS lana -- this involves 
//     registering for our name on the interface
//     and spinning transport threads to queue
//     our CIFS packets
HRESULT InitLana(UCHAR ulLANIdx) 
{
    TRACEMSG(ZONE_INIT, (TEXT("SMBSRV:Initing NETBIOS LANA(%d)"),ulLANIdx));
    HRESULT hr = E_FAIL;
    BYTE *pCName;
    UCHAR retVal;
    UINT uiCNameLen;
    ncb tNcb;
    NetBIOSAdapter *pNewAdapter = NULL;   
    LANA_INFO LanaInfo;

#ifdef DEBUG
    memset(&tNcb, 0xAB, sizeof(ncb));
#endif  

    //
    // Get the CName from globals
    if(FAILED(hr = SMB_Globals::GetCName(&pCName, &uiCNameLen)) ||
       NULL == pCName ||
       0 == uiCNameLen ||
       uiCNameLen > 15)
    {
        TRACEMSG(ZONE_ERROR, (TEXT("SMBSRV:Invalid CName returned from globals!")));       
        if(SUCCEEDED(hr))  //preserve any failure HR we might have
            hr = E_FAIL;
        goto Done;
    }
       
    //
    // Query into netbios for the ipaddress of the new adapter 
    tNcb.ncb_command = NCBQUERYLANA;
    tNcb.ncb_length = sizeof(LANA_INFO);
    tNcb.ncb_buffer = (BYTE *)&LanaInfo;
    tNcb.ncb_lana_num = ulLANIdx;
    
    retVal = Netbios(&tNcb);
    if (0 != (retVal = tNcb.ncb_retcode)) {
        TRACEMSG(ZONE_ERROR, (TEXT("SMBSRV:InitLana: NCBQUERYLANA returned %s"), NETBIOS_TRANSPORT::NCBError(retVal)));
        if(SUCCEEDED(hr))  //preserve any failure HR we might have
            hr = E_FAIL;
        goto Done;        
    }

    //
    // See if we want to listen on this adapter
    if(FALSE == AllowLana(&LanaInfo)) {
        TRACEMSG(ZONE_NETBIOS, (L"SMBSRV:InitLana: we dont allow this LANA (%d)", ulLANIdx));
        hr = S_OK;
        goto Done;
    }
     
    RETAILMSG(1, (TEXT("SMBSRV:InitLana: staring lana: %d"), ulLANIdx));

    //
    // Setup addname and claim our name
    tNcb.ncb_command = NCBADDNAME;
    tNcb.ncb_lana_num = ulLANIdx;
    memset(tNcb.ncb_name, ' ', 16);
    memcpy(tNcb.ncb_name, pCName, uiCNameLen);
   
    retVal = Netbios(&tNcb);
    if (0 != (retVal = tNcb.ncb_retcode)) {
        RETAILMSG(1, (TEXT("SMBSRV:InitLana: NCBADDNAME returned %s"), NETBIOS_TRANSPORT::NCBError(retVal)));
        if(SUCCEEDED(hr))  //preserve any failure HR we might have
            hr = E_FAIL;
        goto Done;        
    }


    //fire up the interface (by creating a new NetBIOSAdapter object) --
    //  inside that will spin any threads it needs (it will be self contained)
    pNewAdapter = new NetBIOSAdapter(ulLANIdx);
    pNewAdapter->SetCName(tNcb.ncb_name);  
    
    if(NULL == pNewAdapter) {
        hr = E_OUTOFMEMORY;
        goto Done;
    }
    
    NBAdapterStack.push_back(pNewAdapter);
    pNewAdapter = NULL;
    hr = S_OK;
    
    Done: 
        //
        // NOTE: this is nulled on init/success... only on failure would
        //    it be non-null
        if(pNewAdapter)
            delete pNewAdapter;           
         
        return hr;
}



//
//  Start the netbios transport -- this includes 
//     initing any global variables before threads get spun 
//     etc
HRESULT StartNetbiosTransport() 
{
    TRACEMSG(ZONE_INIT, (TEXT("SMBSRV:Starting NETBIOS transport")));
    HRESULT hr;  
    
    //
    // running must be true for the enumeration code to operate properly
    InterlockedExchange(&g_fIsRunning, TRUE);    
    
    //
    // Start up a thread to listen for name change events (this thread never goes away)
    if(FAILED(hr = StartListenOnNameChange())) {
        TRACEMSG(ZONE_ERROR, (TEXT("SMBSRV:Failed to start the netbios name listen thread!")));
        goto Done;
    }
    
    //
    // Start enuming netbios calls -- this registers with netbios to recieve lan up/down events
    if(FAILED(hr = StartEnumeratingNetbios())) {
        TRACEMSG(ZONE_ERROR, (TEXT("SMBSRV:Failed getting number of interfaces!")));
        goto Done;
    }
    
    hr = S_OK;
    
    InterlockedExchange(&g_fIsAccepting, TRUE);   
    Done:
        if(FAILED(hr)) {
            InterlockedExchange(&g_fIsRunning, FALSE);
        } else {
            InitializeCriticalSection(&csSendLock);
            InitializeCriticalSection(&csActiveRecvListLock);
        }
        return hr;
}


HRESULT StopNetbiosTransport(void)
{
    TRACEMSG(ZONE_INIT, (TEXT("SMBSRV:Stopping NETBIOS transport")));
    
    HRESULT hr = S_OK;
    BOOL fHaldAdapterFailed = FALSE;
    if(FALSE == g_fIsRunning) {
        hr = S_OK;
        goto Done;
    }    
    
    
    InterlockedExchange(&g_fIsRunning, FALSE);
    
    //
    // Stop enumerating netbios devices
    if(FAILED(hr = StopEnumeratingNetbios())) {
        TRACEMSG(ZONE_ERROR, (TEXT("SMBSRV:error stoping netbios adapter")));
        ASSERT(FALSE);
        goto Done;
    }
    
    //
    // Flush out any remaining adapters
    while(NBAdapterStack.size()) {
        NetBIOSAdapter *pAdapter = NBAdapterStack.front();
        
        if(NULL == pAdapter) {
            ASSERT(FALSE);
        } else {
            if(FAILED(hr = pAdapter->HaltAdapter())) {
                fHaldAdapterFailed = TRUE;
                TRACEMSG(ZONE_ERROR, (TEXT("SMBSRV:error stoping netbios adapter")));
            }
            NBAdapterStack.pop_front();
            delete pAdapter;
        }
    }
    
    //
    // Just empty out the list -- the values inside it are worthless now
    //     as the NBAdapterStack has been flushed
    while(NBAdapterDeleteStack.size()) {
        NBAdapterDeleteStack.pop_front();
    }
    
    InterlockedExchange(&g_fIsAccepting, FALSE);
   
    Done:
        if(fHaldAdapterFailed || FAILED(hr)) {
            hr = E_FAIL;
            InterlockedExchange(&g_fIsAccepting, TRUE);
            InterlockedExchange(&g_fIsRunning, TRUE);
        } else {
            DeleteCriticalSection(&csSendLock);
            DeleteCriticalSection(&csActiveRecvListLock);
        }
        return hr;
}




DWORD 
NETBIOS_TRANSPORT::SMBSRVR_NetbiosListenThread(LPVOID _pMyAdapter)
{    
    HRESULT hr = E_FAIL;   
    NetBIOSAdapter *pMyAdapter = (NetBIOSAdapter *)_pMyAdapter;
    ASSERT(pMyAdapter);
    UINT uiConsecFails = 0;
    
    // See if we have been shut down -- (note we were created suspended so 
    //   errors can be checked for
    if(pMyAdapter->DuringShutDown()) {
        TRACEMSG(ZONE_ERROR, (TEXT("SMBSRV-LSTNTHREAD:Listen thread not starting -- error before launch")));
        hr = E_FAIL;
        goto Done;
    }    
       
    //
    // Loop as long as we are not shutting down
    while (FALSE == pMyAdapter->DuringShutDown()) {
        ncb ncb;
        BYTE *pCName;    
        UINT uiCNameLen;
        UCHAR ncb_err;
                
#ifdef DEBUG
        memset(&ncb, 0xAB, sizeof(ncb));
#endif
        
        ncb.ncb_lana_num = pMyAdapter->GetLANA();
        ncb.ncb_command = NCBLISTEN;
        ncb.ncb_callname[0] = '*';
        
        //
        // Get the CName from globals
        if(FAILED(hr = SMB_Globals::GetCName(&pCName, &uiCNameLen)) ||
           NULL == pCName ||
           0 == uiCNameLen ||
           uiCNameLen > 15)
        {
            //
            // If getting the CName fails, there really isnt much we can do
            //    just abort out
            TRACEMSG(ZONE_ERROR, (TEXT("SMBSRV-LSTNTHREAD:Invalid CName returned from globals!")));
            if(SUCCEEDED(hr))  //preserve any failure HR we might have
                hr = E_FAIL;
            goto Done;
        }
        memset(ncb.ncb_name, ' ', 16);
        memcpy(ncb.ncb_name, pCName, uiCNameLen);    
        
        ncb_err = Netbios(&ncb);

        //
        // Make sure we didnt return from the blocked state into a shutdown
        if(TRUE == pMyAdapter->DuringShutDown()) {
            TRACEMSG(ZONE_ERROR, (TEXT("SMBSRV-LSTNTHREAD:ListenThread -- recved shutdown")));
            break;            
        }

        //
        // Check for errors on the return of NETBIOS
        if(NRC_ADPTMALFN == ncb_err || NRC_IFAIL == ncb_err) {
            RETAILMSG(1, (TEXT("SMBSRV-LSTNTHREAD:ListenThread stopping because of adapter malfunction -- prob was pulled?")));
            break;
        } else if(0 != ncb_err) {
            TRACEMSG(ZONE_ERROR, (TEXT("SMBSRV-LSTNTHREAD:ListenThread NCBLISTEN(%d) returned %s"), pMyAdapter->GetLANA(), NETBIOS_TRANSPORT::NCBError(ncb_err)));
            ASSERT(FALSE);
            uiConsecFails ++;
        } else { 
            TRACEMSG(ZONE_ERROR, (L"NETBIOS Listen got -- LSN:%d on LANA:%d", ncb.ncb_lsn, ncb.ncb_lana_num));
            
            HANDLE hRecvThread; 
            RecvNode NewNode;
            RecvNode *pNodePtr = NULL;             

            CCritSection csLock(&csActiveRecvListLock);
            csLock.Lock();
            ActiveRecvList.push_front(NewNode);
            pNodePtr = &(ActiveRecvList.front());
            
            hRecvThread   = CreateThread(NULL, 0, SMBSRVR_NetbiosRecvThread,  (LPVOID)pNodePtr, CREATE_SUSPENDED, NULL);   
            pNodePtr->pAdapter = pMyAdapter;
            pNodePtr->LANA = ncb.ncb_lana_num;
            pNodePtr->usLSN = ncb.ncb_lsn;  
            pNodePtr->MyHandle = hRecvThread;
            
            if(NULL == hRecvThread) {
                TRACEMSG(ZONE_ERROR, (TEXT("SMBSRV:InitLana: CreateThread failed starting NB Recv:%d"), GetLastError()));
                ActiveRecvList.pop_front(); 
                goto Done;
            }  
            if(0xFFFFFFFF == ResumeThread(hRecvThread)) {
                TRACEMSG(ZONE_ERROR, (L"SMBSRV:Resuming  recv thread %d FAILED!"));
                CloseHandle(hRecvThread);                    
            }              
            uiConsecFails = 0;
        }
        
        //
        // If we have failed a bunch of times in a row, abort... something is wrong
        if(uiConsecFails >= 20) { 
            TRACEMSG(ZONE_ERROR, (TEXT("SMBSRV-LSTNTHREAD:ListenThread too many back to back fails... quitting thread\n")));            
            break;
        }    
    } 
    
    Done:
        TRACEMSG(ZONE_DETAIL, (TEXT("SMBSRV-LSTNTHREAD:Exiting thread!")));
        if(FAILED(hr))
            return -1;
        else 
            return 0;
}




DWORD 
NETBIOS_TRANSPORT::SMBSRVR_NetbiosRecvThread(LPVOID _pAdapter)
{
    DWORD dwRet = -1;
    HRESULT hr = E_FAIL; 
    BYTE *pCName;    
    UINT uiCNameLen;
    ncb ncbRequest;
    BYTE ncb_err; 
    RecvNode *pNode = (RecvNode *)_pAdapter;
    NetBIOSAdapter *pAdapter = pNode->pAdapter;      
    SMB_PACKET *pNewPacket = NULL;
    ncb *pSendNCB = NULL;
    ce::list<ULONG> OutStandingConnectionIDs;
        
    ASSERT(pAdapter);

    TRACEMSG(ZONE_ERROR, (L"SMBSRV-NBRECV:RecvThread listening on LANA: %d  LSN:%d", pNode->LANA, pNode->usLSN));

    //check to be sure we are in the correct state -- get any persistent data before 
    //  going into main processing loop
    if(pAdapter->DuringShutDown()) {
        TRACEMSG(ZONE_ERROR, (TEXT("SMBSRV-NBRECV:RecvThread thread not starting -- error before launch")));
        hr = E_ABORT;
        goto Done;
    }
    if(NULL == pAdapter) {
        TRACEMSG(ZONE_ERROR, (TEXT("SMBSRV-NBRECV:RecvThread thread could not get global adapter object")));
        hr = E_UNEXPECTED;
        goto Done;
    }
    if(FAILED(hr = SMB_Globals::GetCName(&pCName, &uiCNameLen)) ||
       NULL == pCName ||
       0 == uiCNameLen ||
       uiCNameLen > 15)
    {      
        TRACEMSG(ZONE_ERROR, (TEXT("SMBSRV-NBRECV:Invalid CName returned from globals!")));
        if(SUCCEEDED(hr)) //preserve any failure hr
            hr = E_FAIL;
        goto Done;
    }
    
       
    while(!pAdapter->DuringShutDown())
    {
        //PERFPERF:  put this under profiler... but we might be want to make a
        //  copy of this (rather than filling out for each packet)
        #ifdef DEBUG
            memset(&ncbRequest, 0xAB, sizeof(ncb));
        #endif
        ncbRequest.ncb_command = NCBRECV;
        ncbRequest.ncb_lana_num = pNode->LANA;
        ncbRequest.ncb_lsn = pNode->usLSN;;
        memset(ncbRequest.ncb_name, ' ', 16);
        memcpy(ncbRequest.ncb_name, pCName, uiCNameLen);    
   
        //
        // Get some memory out of our free pool
        if(NULL == (pNewPacket = SMB_Globals::g_SMB_Pool.Alloc())) {
            hr = E_OUTOFMEMORY;            
            goto Done;
        }  
      
        pNewPacket->pInSMB = (SMB_HEADER *)pNewPacket->InSMB;
        ncbRequest.ncb_buffer = pNewPacket->InSMB;
        ncbRequest.ncb_length = SMB_Globals::MAX_PACKET_SIZE;
        
        (pNewPacket->InSMB)[0] = 0;
        (pNewPacket->InSMB)[1] = 0;
        (pNewPacket->InSMB)[2] = 0;
        (pNewPacket->InSMB)[3] = 0;
        
        //read a packet
        if(0 == (ncb_err = Netbios(&ncbRequest)) && FALSE == pAdapter->DuringShutDown() &&
            (pNewPacket->InSMB)[0] == 0xFF &&
            (pNewPacket->InSMB)[1] == 'S' &&
            (pNewPacket->InSMB)[2] == 'M' &&
            (pNewPacket->InSMB)[3] == 'B') {     
           TRACEMSG(ZONE_NETBIOS, (L"SMBSRV-NBRECV: got packet on LSN: %d   LANA: %d", ncbRequest.ncb_lsn, ncbRequest.ncb_lana_num));
      
            //PERFPERF: pull this off a free list!
            if(NULL == (pSendNCB = new ncb())) {
                hr = E_OUTOFMEMORY;
                goto Done;
            }
            memcpy(pSendNCB, (void *)&ncbRequest, sizeof(ncb));
            pNewPacket->pToken = (void *)pSendNCB;
            pNewPacket->uiInSize = ncbRequest.ncb_length;
            pNewPacket->pOutSMB = NULL;
            pNewPacket->uiOutSize = 0;
            pNewPacket->pfnQueueFunction = QueueNBPacketForSend;     
            pNewPacket->uiPacketType = SMB_NORMAL_PACKET;
            pNewPacket->ulConnectionID = (ULONG)(SMB_Globals::NB_TRANSPORT << 16) | ncbRequest.ncb_lsn;
            pNewPacket->dwDelayBeforeSending = 0;
#ifdef DEBUG        
            pNewPacket->uiPacketNumber = InterlockedIncrement(&SMB_Globals::g_PacketID);
#endif        
            
            //
            // If this connection isnt on our outstanding list put it there
            ce::list<ULONG>::iterator it = OutStandingConnectionIDs.begin();
            BOOL fOnList = FALSE;
            while(it != OutStandingConnectionIDs.end()) {
                if(pNewPacket->ulConnectionID == (*it)) {
                    fOnList = TRUE;
                    break;
                }
                it ++;
            }
            if(FALSE == fOnList) {
                OutStandingConnectionIDs.push_front(pNewPacket->ulConnectionID);
            }
            
            //
            // Hand off the packet to the SMB cracker
            if(FAILED(CrackPacket(pNewPacket))) {
                //this should *NEVER* happen... Crack should handle its own errors
                TRACEMSG(ZONE_NETBIOS, (TEXT("SMBSRV-NBRECV: UNEXPECTED ERROR IN CRACK()!")));                               
                ASSERT(FALSE);
                hr = E_UNEXPECTED;
                goto Done;
            } else {
                // if it DID work, zero out all our pointers as the cracker owns
                //   our memory
                pNewPacket = NULL;
                pSendNCB = NULL;
            }    
        } else {
            TRACEMSG(ZONE_NETBIOS, (L"SMBSRV-LSTNTHREAD: Hangup LSN(%d)\n", ncbRequest.ncb_lsn));  
            BOOL fBadLana = (NRC_ADPTMALFN == ncb_err);
                        
            //
            // Clean up any memory we might have allocated (that would normally
            //   be send through the cracker
            ASSERT(NULL == pSendNCB); 
            
            if(pSendNCB)
                delete pSendNCB;           
            pSendNCB = NULL;
            
            
            // Hang up this session!           
            ncbRequest.ncb_command = NCBHANGUP;
            
            //
            // if we are here AND the previous error WASNT BAD_LANA issue a hangup
            //    request
            if(FALSE == fBadLana && (ncb_err = Netbios(&ncbRequest))) {
                TRACEMSG(ZONE_ERROR, (L"SMBSRV-NBRECV: Error hanging up LSN(%d)!!! (%s) -- maybe we are shutting down -- if so we will catch that on next iteration of loop?",ncbRequest.ncb_lsn, NETBIOS_TRANSPORT::NCBError(ncb_err)));                 
            } else if(TRUE == fBadLana) {
                TRACEMSG(ZONE_ERROR, (L"SMBSRV-NBRECV: bad LANA on LSN(%d)!!!",ncbRequest.ncb_lsn));   
            }
            
            //
            // Purge out the cracker  
            if(NULL != pNewPacket) {
                memset(pNewPacket, 0, sizeof(SMB_PACKET));                
                pNewPacket->uiPacketType = SMB_CONNECTION_DROPPED;
                pNewPacket->ulConnectionID = (ULONG)(SMB_Globals::NB_TRANSPORT << 16) | ncbRequest.ncb_lsn;;
                pNewPacket->pfnQueueFunction = QueueNBPacketForSend;
                pNewPacket->dwDelayBeforeSending = 0;
        #ifdef DEBUG        
                pNewPacket->uiPacketNumber = InterlockedIncrement(&SMB_Globals::g_PacketID);
        #endif        
                
                TRACEMSG(ZONE_TCPIP, (L"SMBSRV-NBRECV: sending out connection dropped for %d", pNewPacket->ulConnectionID));

                //
                // And remove this guy from out outstanding list                
                ce::list<ULONG>::iterator it = OutStandingConnectionIDs.begin();
                while(it != OutStandingConnectionIDs.end()) {
                    if(pNewPacket->ulConnectionID == (*it)) {                        
                        OutStandingConnectionIDs.erase(it);
                        break;
                    }
                    it++;
                }                                               

                //hand off the packet to the SMB cracker
                if(FAILED(CrackPacket(pNewPacket))) {
                    //this should *NEVER* happen... Crack should handle its own errors
                    //  and when there is one it should return back an error code to the client
                    TRACEMSG(ZONE_ERROR, (L"SMBSRV-NBRECV: UNEXPECTED ERROR IN CRACK()!"));                               
                    ASSERT(FALSE);
                    hr = E_UNEXPECTED;
                    goto Done;
                } else {
                    pNewPacket = NULL;
                }
            }
            
            //
            // If the lana is bad, exit out to prevent spining
            if(TRUE == fBadLana) {
                CCritSection csLock(&csAdapterDiscoverList);
                csLock.Lock();
                NBAdapterDeleteStack.push_back(pAdapter);
                goto Done;
            }


			//
			// We need to go away now
			break;
        }    
    }
           
    hr = S_OK;    
    Done:
        TRACEMSG(ZONE_NETBIOS, (L"SMBSRV-NBRECV:Exiting thread!"));
        
        //
        // Drop any connections that might be outstanding
        if(0 != OutStandingConnectionIDs.size()) {
            
            while(OutStandingConnectionIDs.size()) {
                //
                // Purge out the cracker        
                if(NULL == (pNewPacket = SMB_Globals::g_SMB_Pool.Alloc())) {
                    hr = E_OUTOFMEMORY;            
                    goto Done;
                }  
                
                if(NULL != pNewPacket) {
                    memset(pNewPacket, 0, sizeof(SMB_PACKET));                
                    pNewPacket->uiPacketType = SMB_CONNECTION_DROPPED;
                    pNewPacket->ulConnectionID = (OutStandingConnectionIDs.front());                    
                    pNewPacket->pfnQueueFunction = QueueNBPacketForSend;
                    pNewPacket->dwDelayBeforeSending = 0;
            #ifdef DEBUG        
                    pNewPacket->uiPacketNumber = InterlockedIncrement(&SMB_Globals::g_PacketID);
            #endif        
                    
                    TRACEMSG(ZONE_TCPIP, (L"SMBSRV-NBRECV: sending out connection dropped for %d", pNewPacket->ulConnectionID));
                    
                    //hand off the packet to the SMB cracker
                    if(FAILED(CrackPacket(pNewPacket))) {
                        //this should *NEVER* happen... Crack should handle its own errors
                        //  and when there is one it should return back an error code to the client
                        TRACEMSG(ZONE_ERROR, (L"SMBSRV-NBRECV: UNEXPECTED ERROR IN CRACK()!"));                               
                        ASSERT(FALSE);
                        hr = E_UNEXPECTED;
                        break;
                    } else {
                        pNewPacket = NULL;
                   }
                }
                OutStandingConnectionIDs.pop_front();
            }
        }       
       
        if(NULL != pNewPacket) {
            SMB_Globals::g_SMB_Pool.Free(pNewPacket);
        } 
        if(NULL != pSendNCB) {
            delete pSendNCB;
        }  


        //
        // Remove ourself from the active list
        CCritSection csLock(&csActiveRecvListLock);
        csLock.Lock();
        ce::list<RecvNode, NETBIOS_CONNECTION_ALLOC >::iterator it;
        ce::list<RecvNode, NETBIOS_CONNECTION_ALLOC >::iterator itEnd = ActiveRecvList.end();
        for(it = ActiveRecvList.begin(); it != itEnd; it++) {
            if((*it).MyHandle == pNode->MyHandle) {               
               ActiveRecvList.erase(it); 
               break;
            }
        }        
        if(FAILED(hr)) {
            return -1;
        }    
        else {
            return 0;  
        }
}




HRESULT 
QueueNBPacketForSend(SMB_PACKET *pPacket, BOOL fDestruct)
{
    ncb *pNCB = NULL; 
    HRESULT hr = E_FAIL;
    UCHAR ncb_err;
    CCritSection csLock(&csSendLock);
    csLock.Lock();
    
    //
    // If the connection has dropped delete memory
    if(SMB_CONNECTION_DROPPED == pPacket->uiPacketType) {
        TRACEMSG(ZONE_SMB, (L"NETBIOS: connection dropped packet -- deleting memory"));
        hr = S_OK;
        goto Done;
    }
   
    //
    // They (the cracker)must send us a packet!
    if(NULL == pPacket) {
        ASSERT(FALSE);
        hr = E_UNEXPECTED;
        goto Done;
    }
    
    //
    //  get a pointer to the NCB... this is our 'token'
    if(NULL == (pNCB = (ncb *)(pPacket->pToken))) {
        //
        // If this happens, we were given a bad token... memory corruption?
        ASSERT(FALSE);
        hr = E_UNEXPECTED;
        goto Done;
    }
    
    pNCB->ncb_command = NCBSEND;
    pNCB->ncb_buffer = (BYTE *)pPacket->pOutSMB;
    pNCB->ncb_length = pPacket->uiOutSize;
    
    //
    // If we have been shut down dont send out the packet (see table at top for
    //    meanings for g_fIsAccepting
    if(FALSE == g_fIsAccepting) {
        TRACEMSG(ZONE_ERROR, (L"SMBSRV-NBSEND: we have shut down -- not sending packet"));
        hr = E_FAIL;
        goto Done;
    }
    
    ncb_err = Netbios(pNCB);
    if(!ncb_err) {
        TRACEMSG(ZONE_NETBIOS, (TEXT("SMBSRV-NBSEND: sent packet!")));
    } else {
       TRACEMSG(ZONE_ERROR, (TEXT("SMBSRV-NBRECV: Error sending packet! (%s)"),NETBIOS_TRANSPORT::NCBError(ncb_err)));        
    }
    
    hr = S_OK;   
    Done:    
        ASSERT(pPacket);
#ifdef DEBUG   
         ASSERT(TRUE == Cracker::g_fIsRunning);
         if(TRUE == Cracker::g_fIsRunning && SMB_CONNECTION_DROPPED != pPacket->uiPacketType) {
             UCHAR SMB = pPacket->pInSMB->Command;
             pPacket->PerfStopTimer(SMB);
             DWORD dwProcessing = pPacket->TimeProcessing();
             CCritSection csLockPerf(&Cracker::g_csPerfLock); 
             csLockPerf.Lock();
                double dwNew = (Cracker::g_dblPerfAvePacketTime[SMB] * Cracker::g_dwPerfPacketsProcessed[SMB]);
                Cracker::g_dwPerfPacketsProcessed[SMB] ++;
                dwNew += dwProcessing;
                dwNew /= Cracker::g_dwPerfPacketsProcessed[SMB];
                Cracker::g_dblPerfAvePacketTime[SMB] = dwNew; 
             csLockPerf.UnLock();
         }
#endif  
        if(TRUE == fDestruct) {
            if(pNCB) {
                delete pNCB;   
            }
            if(NULL != pPacket) {
                SMB_Globals::g_SMB_Pool.Free(pPacket);
            }
            
        }
        return hr;
}
