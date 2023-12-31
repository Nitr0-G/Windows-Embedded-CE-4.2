//
// Copyright (c) Microsoft Corporation.  All rights reserved.
//
//
// This source code is licensed under Microsoft Shared Source License
// Version 1.0 for Windows CE.
// For a copy of the license visit http://go.microsoft.com/fwlink/?LinkId=3223.
//
#include "SMB_Globals.h"
#include "FileServer.h"
#include "Utils.h"
#include "SMBErrors.h"
#include "ShareInfo.h"
#include "RAPI.h"
#include "Cracker.h"
#include "ConnectionManager.h"
#include "SMBCommands.h"
#include "SMBPackets.h"

using namespace SMB_FILE;

ClassPoolAllocator<10, FileObject> FileObjectAllocator;


WORD
Win32AttributeToDos(DWORD attributes)
{
    WORD attr = 0;

    if (attributes & FILE_ATTRIBUTE_READONLY)
        attr |= 1;
    if (attributes & FILE_ATTRIBUTE_HIDDEN)
        attr |= 2;
    if (attributes & FILE_ATTRIBUTE_SYSTEM)
        attr |= 4;
    if (attributes & FILE_ATTRIBUTE_DIRECTORY)
        attr |= 16;
    if (attributes & FILE_ATTRIBUTE_ARCHIVE)
        attr |= 32;
    
    return attr;
}

WORD
DosAttributeToWin32(DWORD attributes)
{
    WORD attr = 0;    
    
    if (attributes & 1)
        attr |= FILE_ATTRIBUTE_READONLY;
    if (attributes & 2)
        attr |= FILE_ATTRIBUTE_HIDDEN;
    if (attributes & 4)
        attr |= FILE_ATTRIBUTE_SYSTEM;
    if (attributes & 16)
        attr |= FILE_ATTRIBUTE_DIRECTORY;
    if (attributes & 32)
        attr |= FILE_ATTRIBUTE_ARCHIVE;
    
    return attr;
}



//
// Util to return TRUE if the passed thing is a directory, FALSE if not
HRESULT IsDirectory(const WCHAR *pFile, BOOL *fStatus) 
{
    WIN32_FIND_DATA dta;
    BOOL fIsDir = FALSE;
    HRESULT hr = E_FAIL;    
    HANDLE h = FindFirstFile(pFile, &dta);
    
    if(INVALID_HANDLE_VALUE == h) {
        hr = S_OK;
        goto Done;
    }
    FindClose(h);
    
    //
    // Figure out what the file/dir is
    if(dta.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        fIsDir = TRUE;
    }
    //
    // Success
    hr = S_OK;
    
    Done:
        *fStatus = fIsDir;
        return hr;
}

//
// Util to get a file time w/o having a handle
HRESULT GetFileTimesFromFileName(const WCHAR *pFile, FILETIME *pCreation, FILETIME *pAccess, FILETIME *pWrite) 
{
    WIN32_FIND_DATA dta;
    BOOL fIsDir = FALSE;
    HRESULT hr = E_FAIL;    
    HANDLE h = FindFirstFile(pFile, &dta);
    
    if(INVALID_HANDLE_VALUE == h) {
        goto Done;
    }
    FindClose(h);
    
    if(NULL != pCreation) {
        *pCreation = dta.ftCreationTime;
    }
    if(NULL != pAccess) {
        *pAccess = dta.ftLastAccessTime;
    }
    if(NULL != pWrite) {
        *pWrite = dta.ftLastWriteTime;
    }      
    //
    // Success
    hr = S_OK;
    
    Done:
        
        return hr;
}



DWORD SMB_Trans2_Query_FS_Information(SMB_COM_TRANSACTION2_CLIENT_REQUEST *pRequest,                         
                                   StringTokenizer *pTokenizer,
                                   RAPIBuilder *pRAPIBuilder,
                                   SMB_COM_TRANSACTION2_SERVER_RESPONSE *pResponse,
                                   USHORT usTID,
                                   SMB_PACKET *pSMB) 
{
    WORD wLevel; 
    DWORD dwRet;
    ActiveConnection *pMyConnection = NULL;
    BYTE *pLabel =  NULL;
    
    //
    // Find our connection state        
    if(NULL == (pMyConnection = SMB_Globals::g_pConnectionManager->FindConnection(pSMB))) {
      TRACEMSG(ZONE_ERROR, (L"SMBSRV: SMB_INFO_ALLOCATION: -- cant find connection 0x%x!", pSMB->ulConnectionID));
      ASSERT(FALSE);
      dwRet = SMB_ERR(ERRSRV, ERRerror);
      goto Done;    
    }
        
    if(FAILED(pTokenizer->GetWORD(&wLevel))) {
        TRACEMSG(ZONE_ERROR, (L"SMBSRV: TRANACT -- not enough memory in request -- sending back request for more"));                              
        goto Error;
    } 
        
    switch(wLevel) {
        case SMB_INFO_ALLOCATION: //0x01
        {
            SMB_QUERY_INFO_ALLOCATION_SERVER_RESPONSE *pAllocResponse;               
                    
            TIDState *pTIDState = NULL;
            ULARGE_INTEGER FreeToCaller;
            ULARGE_INTEGER NumberBytes;
            ULARGE_INTEGER TotalFree;
            
            if(FAILED(pRAPIBuilder->ReserveParams(0, (BYTE**)&pAllocResponse))) 
              goto Error;            
            if(FAILED(pRAPIBuilder->ReserveBlock(sizeof(SMB_QUERY_INFO_ALLOCATION_SERVER_RESPONSE), (BYTE**)&pAllocResponse))) 
              goto Error;
              
            //
            // Find a share state 
            if(FAILED(pMyConnection->FindTIDState(usTID, &pTIDState, SEC_READ)) || NULL == pTIDState)
            {
                TRACEMSG(ZONE_ERROR, (L"SMBSRV: SMB_INFO_ALLOCATION -- couldnt find share state!!"));              
                goto Error;     
            }                
            
            //
            // Get the amount of free disk space
            if(0 == GetDiskFreeSpaceEx(pTIDState->GetShare()->GetDirectoryName(),
                                       &FreeToCaller,
                                       &NumberBytes,
                                       &TotalFree)) {
                TRACEMSG(ZONE_ERROR, (L"SMBSRV: SMB_INFO_ALLOCATION -- couldnt get free disk space!!"));              
                goto Error;     
                                       
            }
             
            //
            // Fill in parameters
            pAllocResponse->idFileSystem = 0; //we dont have a filesysem ID
            pAllocResponse->cSectorUnit = 32768;
            pAllocResponse->cUnit = (ULONG)(NumberBytes.QuadPart / 32768);
            pAllocResponse->cUnitAvail = (ULONG)(FreeToCaller.QuadPart / 32768);
            pAllocResponse->cbSector = 1;  
            break;
        }
            break;
        case SMB_QUERY_FS_ATTRIBUTE_INFO: //0x105
        {
            SMB_QUERY_FS_ATTRIBUTE_INFO_SERVER_RESPONSE *pAttrResponse;

            if(FAILED(pRAPIBuilder->ReserveParams(0, (BYTE**)&pAttrResponse)))  {
                goto Error;
            }  
            
            if(FAILED(pRAPIBuilder->ReserveBlock(sizeof(SMB_QUERY_FS_ATTRIBUTE_INFO_SERVER_RESPONSE), (BYTE**)&pAttrResponse))) {
                goto Error;
            }  
       
            pAttrResponse->FileSystemAttributes = 0x20; 
            pAttrResponse->MaxFileNameComponent = 0xFF;
            pAttrResponse->NumCharsInLabel = 0;
            break;
        }
        case SMB_INFO_VOLUME: //0x02
        {
            SMB_INFO_VOLUME_SERVER_RESPONSE *pVolResponse;
                   
            if(FAILED(pRAPIBuilder->ReserveParams(0, (BYTE**)&pVolResponse))) {               
                goto Error;
            }  
            if(FAILED(pRAPIBuilder->ReserveBlock(sizeof(SMB_INFO_VOLUME_SERVER_RESPONSE), (BYTE**)&pVolResponse))) {               
                goto Error;
            }  
        
            pVolResponse->ulVolumeSerialNumber = 0x00000000;
            pVolResponse->NumCharsInLabel= 0;        
            break;
        }
        case SMB_QUERY_FS_VOLUME_INFO: //0x0102
        {
            SMB_QUERY_FS_VOLUME_INFO_SERVER_RESPONSE *pVolResponse = NULL;
            SMB_QUERY_FS_VOLUME_INFO_SERVER_RESPONSE VolResponse;
            BYTE *pLabelInPacket = NULL;
            UINT uiLabelLen = 0;
            StringConverter Label;
            
            if(FAILED(Label.append(L""))) {
                goto Error;
            }
            if(NULL == (pLabel = Label.NewSTRING(&uiLabelLen, pMyConnection->SupportsUnicode(pSMB->pInSMB)))) {
                goto Error;
            }            
            if(FAILED(pRAPIBuilder->ReserveParams(0, (BYTE**)&pVolResponse))) {               
                goto Error;
            }  
            if(FAILED(pRAPIBuilder->ReserveBlock(sizeof(SMB_QUERY_FS_VOLUME_INFO_SERVER_RESPONSE), (BYTE**)&pVolResponse))) {               
                goto Error;
            }  
            if(FAILED(pRAPIBuilder->ReserveBlock(uiLabelLen, (BYTE**)&pLabelInPacket))) {               
                goto Error;
            }  
            VolResponse.VolumeCreationTime.QuadPart = 0;
            VolResponse.VolumeSerialNumber = 0x00000000;
            VolResponse.LengthOfLabel = Label.Length();
            VolResponse.Reserved1 = 0;
            VolResponse.Reserved2 = 0;
			memcpy(pVolResponse, &VolResponse, sizeof(SMB_QUERY_FS_VOLUME_INFO_SERVER_RESPONSE));
            memcpy(pLabelInPacket, pLabel, uiLabelLen);            
            break;
        }        
        default:
            TRACEMSG(ZONE_ERROR, (L"SMBSRV: TRANACT -- unknown Trans2 query for FS INFO %d", wLevel)); 
            ASSERT(FALSE);
            goto Error;
    }
    dwRet = 0;
    goto Done;
    Error:
        dwRet = SMB_ERR(ERRHRD, ERRnosupport);
    Done:
        if(NULL != pLabel) {
            LocalFree(pLabel);
        }
        return dwRet;
}

DWORD SMB_Trans2_Query_File_Information(USHORT usTid, 
                                    SMB_COM_TRANSACTION2_CLIENT_REQUEST *pRequest,                         
                                    StringTokenizer *pTokenizer, 
                                    SMB_PROCESS_CMD *_pRawResponse,
                                    SMB_COM_TRANSACTION2_SERVER_RESPONSE *pResponse,
                                    SMB_PACKET *pSMB,
                                    UINT *puiUsed) 
{
    WORD wLevel;
    USHORT usFID;
    DWORD dwRet;
    ActiveConnection *pMyConnection = NULL;
    UINT uiFileNameLen = 0;
    BYTE *pFileName = NULL;
    RAPIBuilder RAPI((BYTE *)(pResponse+1), 
                     _pRawResponse->uiDataSize-sizeof(SMB_COM_TRANSACTION2_SERVER_RESPONSE), 
                     pRequest->MaxParameterCount, 
                     pRequest->MaxDataCount);
    
    //
    // Find our connection state        
    if(NULL == (pMyConnection = SMB_Globals::g_pConnectionManager->FindConnection(pSMB))) {
      TRACEMSG(ZONE_ERROR, (L"SMBSRV: SMB_Trans2_Query_File_Information: -- cant find connection 0x%x!", pSMB->ulConnectionID));
      ASSERT(FALSE);
      dwRet = SMB_ERR(ERRSRV, ERRerror);
      goto Done;    
    }
        
    //
    // Fetch the FID and info level
    if(FAILED(pTokenizer->GetWORD(&usFID))) {
        TRACEMSG(ZONE_ERROR, (L"SMBSRV: SMB_Trans2_Query_File_Information -- not enough memory in request -- sending back request for more"));                              
        goto Error;
    }     
    if(FAILED(pTokenizer->GetWORD(&wLevel))) {
        TRACEMSG(ZONE_ERROR, (L"SMBSRV: SMB_Trans2_Query_File_Information -- not enough memory in request -- sending back request for more"));                              
        goto Error;
    } 
        
    switch(wLevel) {
        case SMB_QUERY_FILE_ALL_INFO_ID: //0x0107
        {
            SMB_QUERY_FILE_ALL_INFO QueryResponse;
            SMB_QUERY_FILE_ALL_INFO *pQueryResponse = NULL;       
            WIN32_FIND_DATA w32FindData;
            WORD attributes;  
            TIDState *pTIDState = NULL;
            ULONG ulFileOffset = 0;             
            FileObject *pMyFile = NULL;
            
            if(FAILED(RAPI.ReserveParams(0, (BYTE**)&pQueryResponse))) 
              goto Error;            
            if(FAILED(RAPI.ReserveBlock(sizeof(SMB_QUERY_FILE_ALL_INFO), (BYTE**)&pQueryResponse))) 
              goto Error;
              
            //
            // Find a share state 
            if(FAILED(pMyConnection->FindTIDState(usTid, &pTIDState, SEC_READ)) || NULL == pTIDState) {
                TRACEMSG(ZONE_ERROR, (L"SMBSRV: SMB_Trans2_Query_File_Information -- couldnt find share state!!"));              
                goto Error;     
            }    
            
            if(FAILED(pTIDState->QueryFileInformation(usFID, &w32FindData))) {
                TRACEMSG(ZONE_ERROR, (L"SMBSRV: SMB_Trans2_Query_File_Information -- couldnt find FID!"));              
                goto Error;   
            }      
           
            if(FAILED(pTIDState->SetFilePointer(usFID, 0, 1, &ulFileOffset))) {
                TRACEMSG(ZONE_ERROR, (L"SMBSRV: SMB_Trans2_Query_File_Information -- couldnt get file offset for FID: %d!", usFID));              
                ulFileOffset = 0;
            }
            
            if(SUCCEEDED(SMB_Globals::g_pAbstractFileSystem->FindFile(usFID, &pMyFile)) && NULL != pMyFile) {
                StringConverter myString;
                myString.append(pMyFile->FileName());
                pFileName = myString.NewSTRING(&uiFileNameLen, pMyConnection->SupportsUnicode(pSMB->pInSMB));                
            }
            if(NULL == pFileName) {
                StringConverter myString;
                myString.append(L"");
                pFileName = myString.NewSTRING(&uiFileNameLen, pMyConnection->SupportsUnicode(pSMB->pInSMB));  
            }
            
            attributes = Win32AttributeToDos(w32FindData.dwFileAttributes);
            QueryResponse.CreationTime.LowPart = w32FindData.ftCreationTime.dwLowDateTime;
            QueryResponse.CreationTime.HighPart = w32FindData.ftCreationTime.dwHighDateTime;      
            QueryResponse.LastAccessTime.LowPart = w32FindData.ftLastAccessTime.dwLowDateTime;
            QueryResponse.LastAccessTime.HighPart = w32FindData.ftLastAccessTime.dwHighDateTime;     
            QueryResponse.LastWriteTime.LowPart = w32FindData.ftLastWriteTime.dwLowDateTime;
            QueryResponse.LastWriteTime.HighPart = w32FindData.ftLastWriteTime.dwHighDateTime;    
            QueryResponse.ChangeTime.LowPart = w32FindData.ftLastWriteTime.dwLowDateTime;
            QueryResponse.ChangeTime.HighPart = w32FindData.ftLastWriteTime.dwHighDateTime; 
            QueryResponse.Attributes = attributes;
            QueryResponse.AllocationSize.HighPart = w32FindData.nFileSizeHigh;
            QueryResponse.AllocationSize.LowPart = w32FindData.nFileSizeLow;
            QueryResponse.EndOfFile.QuadPart = QueryResponse.AllocationSize.QuadPart;            
            QueryResponse.NumberOfLinks = 0;
            //ULONG NumberOfLinks;
            
            
            QueryResponse.DeletePending = FALSE;
            QueryResponse.Directory = (attributes & FILE_ATTRIBUTE_DIRECTORY)?TRUE:FALSE;
            QueryResponse.Index_Num.QuadPart = 0;
            QueryResponse.EASize = 0; //we dont support extended attributes
            QueryResponse.AccessFlags = QFI_FILE_READ_DATA | 
                                          QFI_FILE_WRITE_DATA | 
                                          QFI_FILE_APPEND_DATA | 
                                          QFI_FILE_READ_ATTRIBUTES | 
                                          QFI_FILE_WRITE_ATTRIBUTES | 
                                          QFI_DELETE;
            
            QueryResponse.IndexNumber.QuadPart = 0;
            QueryResponse.CurrentByteOffset.QuadPart = ulFileOffset;
            QueryResponse.Mode = QFI_FILE_WRITE_THROUGH;
            QueryResponse.AlignmentRequirement = 0;
            QueryResponse.FileNameLength = uiFileNameLen;

            memcpy(pQueryResponse, &QueryResponse, sizeof(SMB_QUERY_FILE_ALL_INFO));
            
            BYTE *pFileInBlob = NULL;
            if(FAILED(RAPI.ReserveBlock(uiFileNameLen, (BYTE**)&pFileInBlob)))
              goto Error;

            ASSERT(pFileInBlob == (BYTE*)(pQueryResponse+1));
            memcpy(pFileInBlob, pFileName, uiFileNameLen);
            break;
        } 
        case SMB_QUERY_FILE_BASIC_INFO_ID: //0x0101
        {
            SMB_QUERY_FILE_BASIC_INFO *pQueryResponse = NULL;  
            SMB_QUERY_FILE_BASIC_INFO QueryResponse;  
            WIN32_FIND_DATA w32FindData;
            WORD attributes;  
            TIDState *pTIDState = NULL;
               
            FileObject *pMyFile = NULL;
            
            if(FAILED(RAPI.ReserveParams(0, (BYTE**)&pQueryResponse))) 
              goto Error;            
            if(FAILED(RAPI.ReserveBlock(sizeof(SMB_QUERY_FILE_BASIC_INFO), (BYTE**)&pQueryResponse))) 
              goto Error;
              
            //
            // Find a share state and then get the file info
            if(FAILED(pMyConnection->FindTIDState(usTid, &pTIDState, SEC_READ)) || NULL == pTIDState) {
                TRACEMSG(ZONE_ERROR, (L"SMBSRV: SMB_Trans2_Query_File_Information -- couldnt find share state!!"));              
                goto Error;     
            }  
            if(FAILED(pTIDState->QueryFileInformation(usFID, &w32FindData))) {
                TRACEMSG(ZONE_ERROR, (L"SMBSRV: SMB_Trans2_Query_File_Information -- couldnt find FID!"));              
                goto Error;   
            } 
            
            attributes = Win32AttributeToDos(w32FindData.dwFileAttributes);
            QueryResponse.CreationTime.LowPart = w32FindData.ftCreationTime.dwLowDateTime;
            QueryResponse.CreationTime.HighPart = w32FindData.ftCreationTime.dwHighDateTime;      
            QueryResponse.LastAccessTime.LowPart = w32FindData.ftLastAccessTime.dwLowDateTime;
            QueryResponse.LastAccessTime.HighPart = w32FindData.ftLastAccessTime.dwHighDateTime;     
            QueryResponse.LastWriteTime.LowPart = w32FindData.ftLastWriteTime.dwLowDateTime;
            QueryResponse.LastWriteTime.HighPart = w32FindData.ftLastWriteTime.dwHighDateTime;    
            QueryResponse.ChangeTime.LowPart = w32FindData.ftLastWriteTime.dwLowDateTime;
            QueryResponse.ChangeTime.HighPart = w32FindData.ftLastWriteTime.dwHighDateTime; 
            QueryResponse.Attributes = attributes; 
            memcpy(pQueryResponse, &QueryResponse, sizeof(SMB_QUERY_FILE_BASIC_INFO));
            dwRet = 0;
            break;
        }
        default:
            TRACEMSG(ZONE_ERROR, (L"SMBSRV: TRANACT -- unknown Trans2 query for FS INFO %d", wLevel)); 
            ASSERT(FALSE);
            goto Error;
    }
    dwRet = 0;
    goto Done;
    Error:
        dwRet = SMB_ERR(ERRHRD, ERRnosupport);
    Done:
    
        //
        // On error just return that error
        if(0 != dwRet) 
            return dwRet;
        
        //fill out response SMB 
        memset(pResponse, 0, sizeof(SMB_COM_TRANSACTION2_SERVER_RESPONSE));
        
        //word count is 3 bytes (1=WordCount 2=ByteCount) less than the response
        pResponse->WordCount = (sizeof(SMB_COM_TRANSACTION2_SERVER_RESPONSE) - 3) / sizeof(WORD);                              
        pResponse->TotalParameterCount = RAPI.ParamBytesUsed();
        pResponse->TotalDataCount = RAPI.DataBytesUsed();
        pResponse->ParameterCount = RAPI.ParamBytesUsed();
        pResponse->ParameterOffset = RAPI.ParamOffset((BYTE *)_pRawResponse->pSMBHeader); 
        pResponse->ParameterDisplacement = 0;
        pResponse->DataCount = RAPI.DataBytesUsed();
        pResponse->DataOffset = RAPI.DataOffset((BYTE *)_pRawResponse->pSMBHeader);
        pResponse->DataDisplacement = 0;
        pResponse->SetupCount = 0;
        pResponse->ByteCount = RAPI.TotalBytesUsed();               
                        
        ASSERT(10 == pResponse->WordCount);  
             
        //set the number of bytes we used         
        *puiUsed = RAPI.TotalBytesUsed() + sizeof(SMB_COM_TRANSACTION2_SERVER_RESPONSE);    

        if(NULL != pFileName) {
            LocalFree(pFileName);
        }
        return dwRet;
}


DWORD SMB_Trans2_Find_First(USHORT usTid, 
                         SMB_COM_TRANSACTION2_CLIENT_REQUEST *pRequest,                         
                         StringTokenizer *pTokenizer,
                         SMB_PROCESS_CMD *_pRawResponse,
                         SMB_COM_TRANSACTION2_SERVER_RESPONSE *pResponse,
                         SMB_PACKET *pSMB,
                         UINT *puiUsed)                          
{
    DWORD dwRet = 0;
    SMB_TRANS2_FIND_FIRST2_CLIENT_REQUEST  *pffRequest = NULL;
    SMB_TRANS2_FIND_FIRST2_SERVER_RESPONSE *pffResponse = NULL;
    
    StringConverter SearchString;
    TIDState *pTIDState;    
    USHORT usSID = 0xFFFF;
    SMB_FIND_FILE_BOTH_DIRECTORY_INFO_STRUCT *pPrevFile = NULL;
    ActiveConnection *pMyConnection = NULL;
    ULONG uiResumeKeySize = 0;
    BOOL fUsingUnicode = FALSE;
    HRESULT hr = E_FAIL;
        
    RAPIBuilder MYRAPIBuilder((BYTE *)(pResponse+1), 
               _pRawResponse->uiDataSize-sizeof(SMB_COM_TRANSACTION2_SERVER_RESPONSE), 
               pRequest->MaxParameterCount, 
               pRequest->MaxDataCount);
    
    
    //
    // Make sure we have some memory to give an answer in
    if(FAILED(MYRAPIBuilder.ReserveParams(sizeof(SMB_TRANS2_FIND_FIRST2_SERVER_RESPONSE), (BYTE**)&pffResponse))) {
        TRACEMSG(ZONE_ERROR, (L"SMBSRV: TRANACT -- not enough memory in request -- sending back request for more"));                    
        dwRet = SMB_ERR(ERRSRV, ERRsrverror);
        goto Done;    
    }
    
    //
    // Get the TRANS2 Find First 2 structure
    if(FAILED(pTokenizer->GetByteArray((BYTE **)&pffRequest, sizeof(SMB_TRANS2_FIND_FIRST2_CLIENT_REQUEST)))) {
        TRACEMSG(ZONE_ERROR, (L"SMBSRV: Error -- not enough memory for find first 2 in trans2"));
        dwRet = SMB_ERR(ERRSRV, ERRerror);      
        goto Done;
    }
        
    //
    // Find our connection state        
    if(NULL == (pMyConnection = SMB_Globals::g_pConnectionManager->FindConnection(pSMB))) {
       TRACEMSG(ZONE_ERROR, (L"SMBSRV: SMB_INFO_ALLOCATION: -- cant find connection 0x%x!", pSMB->ulConnectionID));
       ASSERT(FALSE);
        TRACEMSG(ZONE_ERROR, (L"SMBSRV: Error -- trans2 had name but it was invalid"));
        dwRet = SMB_ERR(ERRDOS, ERRaccess);  
        goto Done;
    }
        
    //
    // Find a share state 
    if(FAILED(hr = pMyConnection->FindTIDState(usTid, &pTIDState, SEC_READ)) || NULL == pTIDState)
    {
        if(E_ACCESSDENIED == hr) {
            TRACEMSG(ZONE_ERROR, (L"SMBSRV: TRANACT -- dont have permissions!!"));
            dwRet = SMB_ERR(ERRDOS, ERRnoaccess);
        } else {
            TRACEMSG(ZONE_ERROR, (L"SMBSRV: TRANACT -- couldnt find share state!!"));
            dwRet = SMB_ERR(ERRDOS, ERRbadfid);  
        }
        goto Done;     
    }    
    
    


    if(FAILED(SearchString.append(pTIDState->GetShare()->GetDirectoryName()))) {
        TRACEMSG(ZONE_ERROR, (L"SMBSRV: TRANACT -- couldnt build search string!!"));
        dwRet = SMB_ERR(ERRDOS, ERRbadfid);  
        goto Done;   
    }
    
    fUsingUnicode = pMyConnection->SupportsUnicode(pSMB->pInSMB, SMB_COM_TRANSACTION2, TRANS2_FIND_FIRST2);
    
    if(TRUE == fUsingUnicode) {
        //
        // Get the file name from the tokenizer
        WCHAR *pFileName;    
        if(FAILED(pTokenizer->GetUnicodeString(&pFileName))) {
            TRACEMSG(ZONE_ERROR, (L"SMBSRV: Error -- trans2 had name but it was invalid"));
            dwRet = SMB_ERR(ERRSRV, ERRerror);      
            goto Done;
        }
        
        //
        // Put in a \ if its not already there
        if('\\' != pFileName[0]) {
            if(FAILED(SearchString.append("\\"))) {
                TRACEMSG(ZONE_ERROR, (L"SMBSRV: Error -- trans2 had name but it was invalid"));
                dwRet = SMB_ERR(ERRSRV, ERRerror);      
                goto Done;                
            }
        }
        
        //
        // Append the filename
        if(FAILED(SearchString.append(pFileName))) {
            TRACEMSG(ZONE_ERROR, (L"SMBSRV: TRANACT -- couldnt build search string!!"));
            dwRet = SMB_ERR(ERRDOS, ERRbadfid);  
            goto Done;   
        }        
    } else {    
        //
        // Get the file name from the tokenizer
        CHAR *pFileName;    
        if(FAILED(pTokenizer->GetString(&pFileName))) {
            TRACEMSG(ZONE_ERROR, (L"SMBSRV: Error -- trans2 had name but it was invalid"));
            dwRet = SMB_ERR(ERRSRV, ERRerror);      
            goto Done;
        }
        
        //
        // Put in a \ if its not already there
        if('\\' != pFileName[0]) {
            if(FAILED(SearchString.append("\\"))) {
                TRACEMSG(ZONE_ERROR, (L"SMBSRV: Error -- trans2 had name but it was invalid"));
                dwRet = SMB_ERR(ERRSRV, ERRerror);      
                goto Done;                
            }
        }
        
        //
        // And append the filename
        if(FAILED(SearchString.append(pFileName))) {
            TRACEMSG(ZONE_ERROR, (L"SMBSRV: TRANACT -- couldnt build search string!!"));
            dwRet = SMB_ERR(ERRDOS, ERRbadfid);  
            goto Done;   
        }
    }
    
    //
    // Fill out the find first2 response structure with meaningful data
    pffResponse->Sid = 0; 
    pffResponse->SearchCount = 0;
    pffResponse->EndOfSearch = 1;  
    pffResponse->EaErrorOffset = 0;
    pffResponse->LastNameOffset = 0;
    
    //
    // Fill out the TRANS2 response structure
    pResponse->WordCount = 10;       
    
    //
    // We wont be alligned if we dont make a gap 
    MYRAPIBuilder.MakeParamByteGap(sizeof(DWORD) - ((UINT)MYRAPIBuilder.DataPtr() % sizeof(DWORD)));
    ASSERT(0 == (UINT)MYRAPIBuilder.DataPtr() % 4);
    
    //
    // See what flags are used
    if(0 != (pffRequest->Flags & FIND_NEXT_RETURN_RESUME_KEY)) {
        uiResumeKeySize = 0;//sizeof(ULONG);
    }    
    
    //
    // Start searching for files....        
    if (SUCCEEDED(pMyConnection->CreateNewFindHandle(SearchString.GetString(), &usSID, fUsingUnicode))) {
        TRACEMSG(ZONE_FILES, (L"SMB_SRV: FindFirstFile2 using SID: 0x%x", usSID));
        
        pffResponse->Sid = usSID; //dont do this in the CreateNewFindHandle b/c of alignment
        do {  
            WIN32_FIND_DATA *pwfd = NULL;            
            
            if(FAILED(pMyConnection->NextFile(pffResponse->Sid, &pwfd))) {
                TRACEMSG(ZONE_ERROR, (L"SMBSRV: TRANACT -- ActiveConnection::NextFile failed!"));       
                break;
            }
            
            //
            // See what information level the client wants
            switch(pffRequest->InformationLevel) {
                case SMB_INFO_STANDARD:
                {                    
                    StringConverter FileName;                   
                    UINT uiFileBlobSize = 0;
                    BYTE *pNewBlock = NULL;
                    BYTE *pFileBlob = NULL;                     
                    SMB_FIND_FILE_STANDARD_INFO_STRUCT *pFileStruct = NULL; 
                    if(FAILED(FileName.append(pwfd->cFileName))) {                       
                        TRACEMSG(ZONE_ERROR, (L"SMBSRV: TRANACT -- not enough memory (OOM) -- failing request"));                    
                        dwRet = SMB_ERR(ERRSRV, ERRsrverror);
                        goto Done; 
                    }    
                    
                    //
                    // Get back a STRING                    
                    if(NULL == (pFileBlob = FileName.NewSTRING(&uiFileBlobSize,  fUsingUnicode))) {
                        TRACEMSG(ZONE_ERROR, (L"SMBSRV: TRANACT -- not enough memory (OOM) -- failing request"));                    
                        dwRet = SMB_ERR(ERRSRV, ERRsrverror);
                        goto Done;                         
                    }
                    
                    //
                    // If we cant reserve enough memory for this block we need to start up a 
                    //   FindNext setup                     
                    if(FAILED(MYRAPIBuilder.ReserveBlock(uiResumeKeySize + uiFileBlobSize + sizeof(SMB_FIND_FILE_STANDARD_INFO_STRUCT), (BYTE**)&pFileStruct))) {
                        dwRet = 0;
                        pffResponse->EndOfSearch = 0;  
                        LocalFree(pFileBlob);
                        goto SendOK;                         
                    }       
                    
                    //
                    // Set a bogus resume key value
                    if(0 != uiResumeKeySize) {
                        ULONG *ulVal = (ULONG *)pFileStruct;
                        ASSERT(sizeof(ULONG) == uiResumeKeySize);
                        *ulVal = 0;
                        pFileStruct = (SMB_FIND_FILE_STANDARD_INFO_STRUCT *)(((BYTE *)pFileStruct) + uiResumeKeySize);
                    }
                    
                    pNewBlock = (BYTE *)(pFileStruct + 1);                                        
                                 
                    SMB_DATE smbDate;
                    SMB_TIME smbTime;                   
                    
                    if(FAILED(FileTimeToSMBTime(&pwfd->ftCreationTime, &smbTime, &smbDate))) {
                        ASSERT(FALSE);
                        dwRet = SMB_ERR(ERRSRV, ERRsrverror);
                        goto Done;   
                    }
                    pFileStruct->CreationDate = smbDate;
                    pFileStruct->CreationTime = smbTime;

                    if(FAILED(FileTimeToSMBTime(&pwfd->ftLastAccessTime, &smbTime, &smbDate))) {
                        ASSERT(FALSE);
                        dwRet = SMB_ERR(ERRSRV, ERRsrverror);
                        goto Done;   
                    }
                    pFileStruct->LastAccessDate = smbDate;
                    pFileStruct->LastAccessTime = smbTime;
                    
                    if(FAILED(FileTimeToSMBTime(&pwfd->ftLastWriteTime, &smbTime, &smbDate))) {
                        ASSERT(FALSE);
                        dwRet = SMB_ERR(ERRSRV, ERRsrverror);
                        goto Done;   
                    }
                    pFileStruct->LastWriteDate = smbDate;
                    pFileStruct->LastWriteTime = smbTime;
                       
                    pFileStruct->DataSize = pwfd->nFileSizeLow;  
                    pFileStruct->AllocationSize = 0;
                    pFileStruct->Attributes = Win32AttributeToDos(pwfd->dwFileAttributes);
                    pFileStruct->FileNameLen = uiFileBlobSize;
                    
                    memcpy(pNewBlock, pFileBlob, uiFileBlobSize);
                    LocalFree(pFileBlob); 
                    
                    //
                    // Since we've successfully made a record, increase the search
                    //   count
                    pffResponse->SearchCount ++; 
                    break;
                }
                case SMB_FIND_FILE_BOTH_DIRECTORY_INFO:
                {
                    StringConverter FileName;                   
                    UINT uiFileBlobSize = 0;
                    BYTE *pNewBlock = NULL;
                    BYTE *pFileBlob = NULL; 
                    SMB_FIND_FILE_BOTH_DIRECTORY_INFO_STRUCT *pFileStruct = NULL; 
                    
                    if(FAILED(FileName.append(pwfd->cFileName))) {                       
                        TRACEMSG(ZONE_ERROR, (L"SMBSRV: TRANACT -- not enough memory (OOM) -- failing request"));                    
                        dwRet = SMB_ERR(ERRSRV, ERRsrverror);
                        goto Done; 
                    }    
                    
                    //
                    // Get back a STRING                    
                    if(NULL == (pFileBlob = FileName.NewSTRING(&uiFileBlobSize,  fUsingUnicode))) {
                        TRACEMSG(ZONE_ERROR, (L"SMBSRV: TRANACT -- not enough memory (OOM) -- failing request"));                    
                        dwRet = SMB_ERR(ERRSRV, ERRsrverror);
                        goto Done;                         
                    }
                    
                    //
                    // If we cant reserve enough memory for this block we need to start up a 
                    //   FindNext setup      
                    if(FAILED(MYRAPIBuilder.ReserveBlock(uiFileBlobSize + sizeof(SMB_FIND_FILE_BOTH_DIRECTORY_INFO_STRUCT), (BYTE**)&pFileStruct, sizeof(DWORD)))) {
                        dwRet = 0;
                        pffResponse->EndOfSearch = 0;  
                        LocalFree(pFileBlob);
                        goto SendOK;                         
                    }       
                    pNewBlock = (BYTE *)(pFileStruct + 1);
                                                               
                    pFileStruct->FileIndex = 0;
                    pFileStruct->NextEntryOffset = 0;
                    if(pPrevFile)
                        pPrevFile->NextEntryOffset = (UINT)pFileStruct - (UINT)pPrevFile;  
                    pFileStruct->CreationTime.LowPart = pwfd->ftCreationTime.dwLowDateTime;
                    pFileStruct->CreationTime.HighPart = pwfd->ftCreationTime.dwHighDateTime;                    
                    pFileStruct->LastAccessTime.LowPart = pwfd->ftLastAccessTime.dwLowDateTime;
                    pFileStruct->LastAccessTime.HighPart = pwfd->ftLastAccessTime.dwHighDateTime;
                    pFileStruct->LastWriteTime.LowPart = pwfd->ftLastWriteTime.dwLowDateTime;
                    pFileStruct->LastWriteTime.HighPart = pwfd->ftLastWriteTime.dwHighDateTime;              
                    pFileStruct->ChangeTime.LowPart = pwfd->ftLastWriteTime.dwLowDateTime;
                    pFileStruct->ChangeTime.HighPart = pwfd->ftLastWriteTime.dwHighDateTime;                    
                    pFileStruct->EndOfFile.LowPart = pwfd->nFileSizeLow;
                    pFileStruct->EndOfFile.HighPart = pwfd->nFileSizeHigh;                    
                    pFileStruct->ExtFileAttributes = Win32AttributeToDos(pwfd->dwFileAttributes);
                    pFileStruct->FileNameLength = uiFileBlobSize;
                    pFileStruct->EaSize = 0;
                    pFileStruct->ShortNameLength = 0;
                    memset(pFileStruct->ShortName, 0, sizeof(pFileStruct->ShortName));                     
                    memcpy(pNewBlock, pFileBlob, uiFileBlobSize);
                    LocalFree(pFileBlob); 
                    
                    //
                    // Since we've successfully made a record, increase the search
                    //   count
                    pffResponse->SearchCount ++;
                    pPrevFile = pFileStruct;
                    break;
                }                        
                default:
                    ASSERT(FALSE); //not supported!! 
                    break;
            }
           
        } while (SUCCEEDED(pMyConnection->AdvanceToNextFile(pffResponse->Sid)));
        
        //
        // If we get here, yippie! we can fit the entire search into one packet!
        //    just close up shop
        pMyConnection->CloseFindHandle(pffResponse->Sid);
        pffResponse->Sid = 0;
    }else {
        TRACEMSG(ZONE_FILES, (L"SMB_FILE:  searching for %s failed! -- file not found",SearchString.GetString()));
        SMB_COM_GENERIC_RESPONSE *pGenricResponse = (SMB_COM_GENERIC_RESPONSE *)(pResponse);
        pGenricResponse->ByteCount = 0;
        pGenricResponse->WordCount = 0;
        *puiUsed = sizeof(SMB_COM_GENERIC_RESPONSE);
        dwRet = SMB_ERR(ERRDOS, ERRbadfile);
        goto Done;
    }
 
    SendOK:
    dwRet = 0;    
    pResponse->WordCount = (sizeof(SMB_COM_TRANSACTION2_SERVER_RESPONSE) - 3) / sizeof(WORD); 
    pResponse->TotalParameterCount = MYRAPIBuilder.ParamBytesUsed();
    pResponse->TotalDataCount = MYRAPIBuilder.DataBytesUsed();
    pResponse->Reserved = 0;            
    pResponse->ParameterCount = MYRAPIBuilder.ParamBytesUsed();
    pResponse->ParameterOffset = MYRAPIBuilder.ParamOffset((BYTE *)_pRawResponse->pSMBHeader); 
    pResponse->ParameterDisplacement = 0;
    pResponse->DataCount = MYRAPIBuilder.DataBytesUsed();
    pResponse->DataOffset = MYRAPIBuilder.DataOffset((BYTE *)_pRawResponse->pSMBHeader);
    pResponse->DataDisplacement = 0;
    pResponse->SetupCount = 0;
    pResponse->ByteCount = MYRAPIBuilder.TotalBytesUsed();               
    ASSERT(10 == pResponse->WordCount); 
    *puiUsed = MYRAPIBuilder.TotalBytesUsed() + sizeof(SMB_COM_TRANSACTION2_SERVER_RESPONSE);   
      
    Done:       
        return dwRet;                        
}


DWORD SMB_Trans2_Find_Next(USHORT usTid, 
                         SMB_COM_TRANSACTION2_CLIENT_REQUEST *pRequest,                         
                         StringTokenizer *pTokenizer,
                         SMB_PROCESS_CMD *_pRawResponse,
                         SMB_COM_TRANSACTION2_SERVER_RESPONSE *pResponse,
                         SMB_PACKET *pSMB,
                         UINT *puiUsed)                          
{
    DWORD dwRet = 0;
    SMB_TRANS2_FIND_NEXT2_CLIENT_REQUEST  *pffRequest = NULL;
    SMB_TRANS2_FIND_NEXT2_SERVER_RESPONSE *pffResponse = NULL;
  
    TIDState *pTIDState;    
    USHORT usSID = 0xFFFF;
    SMB_FIND_FILE_BOTH_DIRECTORY_INFO_STRUCT *pPrevFile = NULL;
    ActiveConnection *pMyConnection = NULL;
    BOOL fUsingUnicode = FALSE;
    
    RAPIBuilder MYRAPIBuilder((BYTE *)(pResponse+1), 
               _pRawResponse->uiDataSize-sizeof(SMB_COM_TRANSACTION2_SERVER_RESPONSE), 
               pRequest->MaxParameterCount, 
               pRequest->MaxDataCount);
    
    
    //
    // Make sure we have some memory to give an answer in
    if(FAILED(MYRAPIBuilder.ReserveParams(sizeof(SMB_TRANS2_FIND_NEXT2_SERVER_RESPONSE), (BYTE**)&pffResponse))) {
        TRACEMSG(ZONE_ERROR, (L"SMBSRV: TRANACT -- not enough memory in request -- sending back request for more"));                    
        dwRet = SMB_ERR(ERRSRV, ERRsrverror);
        goto JustExit;    
    }
    
    //
    // Get the TRANS2 Find First 2 structure
    if(FAILED(pTokenizer->GetByteArray((BYTE **)&pffRequest, sizeof(SMB_TRANS2_FIND_NEXT2_CLIENT_REQUEST))) || NULL == pffRequest) {
        TRACEMSG(ZONE_ERROR, (L"SMBSRV: Error -- not enough memory for find first 2 in trans2"));
        dwRet = SMB_ERR(ERRSRV, ERRerror);      
        goto JustExit;
    }
       
    //
    // Find our connection state        
    if(NULL == (pMyConnection = SMB_Globals::g_pConnectionManager->FindConnection(pSMB))) {
       TRACEMSG(ZONE_ERROR, (L"SMBSRV: SMB_INFO_ALLOCATION: -- cant find connection 0x%x!", pSMB->ulConnectionID));
       ASSERT(FALSE);
        TRACEMSG(ZONE_ERROR, (L"SMBSRV: Error -- trans2 had name but it was invalid"));
        dwRet = SMB_ERR(ERRSRV, ERRerror);      
        goto JustExit;
    }
        
    //
    // Find a share state 
    if(FAILED(pMyConnection->FindTIDState(usTid, &pTIDState, SEC_READ)) || NULL == pTIDState) {
        TRACEMSG(ZONE_ERROR, (L"SMBSRV: TRANACT -- couldnt find share state!!"));
        dwRet = SMB_ERR(ERRDOS, ERRbadfid);  
        goto Done;     
    }   
    
    fUsingUnicode = pMyConnection->SupportsUnicode(pSMB->pInSMB, SMB_COM_TRANSACTION2, TRANS2_FIND_NEXT2);
    
    //
    // Fill out the find first2 response structure with meaningful data    
    pffResponse->SearchCount = 0;
    pffResponse->EndOfSearch = 1;  
    pffResponse->EaErrorOffset = 0;
    pffResponse->LastNameOffset = 0;
       
    //
    // We wont be alligned if we dont make a gap 
    MYRAPIBuilder.MakeParamByteGap(sizeof(DWORD) - ((UINT)MYRAPIBuilder.DataPtr() % sizeof(DWORD)));
    ASSERT(0 == (UINT)MYRAPIBuilder.DataPtr() % 4);
    
    //
    // Start searching for files....      
    usSID = pffRequest->Sid;
    
    //
    // Handle any flags that need to be taken care of here
    if(0 != (pffRequest->Flags & FIND_NEXT_RESUME_FROM_PREV)) {
        TRACEMSG(ZONE_FILES, (L"SMB_SRV:  FindNext -- starting from the beginning!"));
        if(FAILED(pMyConnection->ResetSearch(usSID))) {
            ASSERT(FALSE);
            TRACEMSG(ZONE_ERROR, (L"SMB_SRV:  FindNext -- reseting failed!"));            
            dwRet = SMB_ERR(ERRSRV, ERRsrverror);
            goto Done;             
        }
    } else {
        TRACEMSG(ZONE_FILES, (L"SMB_SRV:  FindNext -- continuing from where we started!"));
    }

    TRACEMSG(ZONE_FILES, (L"SMB_SRV: FindNextFile2 using SID: 0x%x", usSID));
    do {             
        SMB_FIND_FILE_BOTH_DIRECTORY_INFO_STRUCT *pFileStruct = NULL; 
        WIN32_FIND_DATA *pwfd = NULL;        
        
        if(FAILED(pMyConnection->NextFile(usSID, &pwfd))) {
            TRACEMSG(ZONE_ERROR, (L"SMBSRV: TRANACT -- ActiveConnection::NextFile failed!"));       
            break;
        }        
           
        //
        // See what information level the client wants
        switch(pffRequest->InformationLevel) {
            case SMB_FIND_FILE_BOTH_DIRECTORY_INFO:
            {
                StringConverter FileName;                   
                UINT uiFileBlobSize = 0;
                BYTE *pNewBlock = NULL;
                BYTE *pFileBlob = NULL; 
                if(FAILED(FileName.append(pwfd->cFileName))) {                       
                    TRACEMSG(ZONE_ERROR, (L"SMBSRV: TRANACT -- not enough memory (OOM) -- failing request"));                    
                    dwRet = SMB_ERR(ERRSRV, ERRsrverror);
                    goto Done; 
                }    
                
                //
                // Get back a STRING                    
                if(NULL == (pFileBlob = FileName.NewSTRING(&uiFileBlobSize,  fUsingUnicode))) {
                    TRACEMSG(ZONE_ERROR, (L"SMBSRV: TRANACT -- not enough memory (OOM) -- failing request"));                    
                    dwRet = SMB_ERR(ERRSRV, ERRsrverror);
                    goto Done;                         
                }
                                                   
                //
                // If we cant reserve enough memory for this block we need to start up a 
                //   FindNext setup      
                if(FAILED(MYRAPIBuilder.ReserveBlock(uiFileBlobSize + sizeof(SMB_FIND_FILE_BOTH_DIRECTORY_INFO_STRUCT), (BYTE**)&pFileStruct, sizeof(DWORD)))) {
                    dwRet = 0;
                    pffResponse->EndOfSearch = 0;  
                    LocalFree(pFileBlob);
                    goto SendOK;                         
                }       
                pNewBlock = (BYTE *)(pFileStruct + 1);
                    
                    
                pFileStruct->FileIndex = 0;
                pFileStruct->NextEntryOffset = 0;
                if(pPrevFile)
                    pPrevFile->NextEntryOffset = (UINT)pFileStruct - (UINT)pPrevFile;  
                pFileStruct->CreationTime.LowPart = pwfd->ftCreationTime.dwLowDateTime;
                pFileStruct->CreationTime.HighPart = pwfd->ftCreationTime.dwHighDateTime;                    
                pFileStruct->LastAccessTime.LowPart = pwfd->ftLastAccessTime.dwLowDateTime;
                pFileStruct->LastAccessTime.HighPart = pwfd->ftLastAccessTime.dwHighDateTime;
                pFileStruct->LastWriteTime.LowPart = pwfd->ftLastWriteTime.dwLowDateTime;
                pFileStruct->LastWriteTime.HighPart = pwfd->ftLastWriteTime.dwHighDateTime;              
                pFileStruct->ChangeTime.LowPart = pwfd->ftLastWriteTime.dwLowDateTime;
                pFileStruct->ChangeTime.HighPart = pwfd->ftLastWriteTime.dwHighDateTime;                    
                pFileStruct->EndOfFile.LowPart = pwfd->nFileSizeLow;
                pFileStruct->EndOfFile.HighPart = pwfd->nFileSizeHigh;                    
                pFileStruct->ExtFileAttributes = Win32AttributeToDos(pwfd->dwFileAttributes);
                pFileStruct->FileNameLength = uiFileBlobSize;
                pFileStruct->EaSize = 0;
                pFileStruct->ShortNameLength = 0;
                memset(pFileStruct->ShortName, 0, sizeof(pFileStruct->ShortName));                                  
                memcpy(pNewBlock, pFileBlob, uiFileBlobSize);
                LocalFree(pFileBlob); 
                
                //
                // Since we've successfully made a record, increase the search
                //   count
                pffResponse->SearchCount ++;
            }    
            break;
            default:
                ASSERT(FALSE); //not supported!! 
                break;
        }
       pPrevFile = pFileStruct;
    } while (SUCCEEDED(pMyConnection->AdvanceToNextFile(usSID)));
      
    //
    // If get here, there arent any more files left -- see if we are supposed to close our handle
    if(0 != (pffRequest->Flags & FIND_NEXT_CLOSE_AT_END)) {
      TRACEMSG(ZONE_FILES, (L"SMB_SRV:  FindNext -- closing up because there are no more files!"));
      if(FAILED(pMyConnection->CloseFindHandle(usSID))) {
          ASSERT(FALSE);
          TRACEMSG(ZONE_ERROR, (L"SMB_SRV:  FindNext -- close failed!"));           
      }
    }     
        
    SendOK:
        dwRet = 0;    
        pResponse->WordCount = (sizeof(SMB_COM_TRANSACTION2_SERVER_RESPONSE) - 3) / sizeof(WORD); 
        pResponse->TotalParameterCount = MYRAPIBuilder.ParamBytesUsed();
        pResponse->TotalDataCount = MYRAPIBuilder.DataBytesUsed();
        pResponse->Reserved = 0;            
        pResponse->ParameterCount = MYRAPIBuilder.ParamBytesUsed();
        pResponse->ParameterOffset = MYRAPIBuilder.ParamOffset((BYTE *)_pRawResponse->pSMBHeader); 
        pResponse->ParameterDisplacement = 0;
        pResponse->DataCount = MYRAPIBuilder.DataBytesUsed();
        pResponse->DataOffset = MYRAPIBuilder.DataOffset((BYTE *)_pRawResponse->pSMBHeader);
        pResponse->DataDisplacement = 0;
        pResponse->SetupCount = 0;
        pResponse->ByteCount = MYRAPIBuilder.TotalBytesUsed();               
        ASSERT(10 == pResponse->WordCount); 
        *puiUsed = MYRAPIBuilder.TotalBytesUsed() + sizeof(SMB_COM_TRANSACTION2_SERVER_RESPONSE);   
      
    Done:  
        //
        // If they want us to close the key do it now
        if(0 != (pffRequest->Flags & FIND_NEXT_CLOSE_AFTER_REQUEST)) {
          TRACEMSG(ZONE_FILES, (L"SMB_SRV:  FindNext -- closing up due to request from client!"));
          if(FAILED(pMyConnection->CloseFindHandle(usSID))) {
              ASSERT(FALSE);
              TRACEMSG(ZONE_ERROR, (L"SMB_SRV:  FindNext -- close failed!"));           
          }
        }         
   JustExit: 
        return dwRet;                        
}



DWORD SMB_FILE::SMB_Com_FindClose2(SMB_PROCESS_CMD *_pRawRequest, 
                                   SMB_PROCESS_CMD *_pRawResponse, 
                                   UINT *puiUsed,
                                   SMB_PACKET *pSMB)
{
    DWORD dwRet = 0;
    SMB_TRANS2_FIND_CLOSE_CLIENT_REQUEST *pRequest =
            (SMB_TRANS2_FIND_CLOSE_CLIENT_REQUEST *)_pRawRequest->pDataPortion;
    SMB_COM_GENERIC_RESPONSE *pResponse = 
            (SMB_COM_GENERIC_RESPONSE *)_pRawResponse->pDataPortion;           
    
    ActiveConnection *pMyConnection = NULL;    
                      
    //
    // Verify that we have enough memory
    if(_pRawRequest->uiDataSize < sizeof(SMB_TRANS2_FIND_CLOSE_CLIENT_REQUEST)) {
       TRACEMSG(ZONE_ERROR, (L"SMBSRV-CRACKER: SMB_Com_FindClose2 -- not enough memory for request!"));
       dwRet = SMB_ERR(ERRSRV, ERRerror);
       goto Done;
    }
    if(_pRawResponse->uiDataSize < sizeof(SMB_COM_GENERIC_RESPONSE)) {
       TRACEMSG(ZONE_ERROR, (L"SMBSRV-CRACKER: SMB_Com_FindClose2 -- not enough memory for response!"));
       dwRet = SMB_ERR(ERRSRV, ERRerror);
       goto Done;
    }    
                                
    //
    // Find our connection state        
    if(NULL == (pMyConnection = SMB_Globals::g_pConnectionManager->FindConnection(pSMB))) {
       TRACEMSG(ZONE_ERROR, (L"SMBSRV: SMB_Com_FindClose2: -- cant find connection 0x%x!", pSMB->ulConnectionID));
       ASSERT(FALSE);
        TRACEMSG(ZONE_ERROR, (L"SMBSRV: Error -- trans2 had name but it was invalid"));
        dwRet = SMB_ERR(ERRSRV, ERRerror);      
        goto Done;
    }   
    
    //
    // Close up
    if(FAILED(pMyConnection->CloseFindHandle(pRequest->Sid))) {
          TRACEMSG(ZONE_ERROR, (L"SMB_SRV:  FindNext -- close failed! looking for SID:0x%x", pRequest->Sid));           
    }   
    pResponse->ByteCount = 0;
    pResponse->WordCount = 0;
    *puiUsed = sizeof(SMB_COM_GENERIC_RESPONSE);
    
    Done:
        return dwRet;                        
}





VOID
FileTimeToDosDateTime(
    FILETIME * ft,
    WORD    * ddate,
    WORD    * dtime
    )
{
    SYSTEMTIME st;
    WORD tmp;

    FileTimeToSystemTime(ft,&st);
    tmp = st.wYear - 1980;
    tmp <<= 7;
    tmp |= st.wMonth & 0x000f;
    tmp <<= 4;
    tmp |= st.wDay & 0x001f;
    *ddate = tmp;

    tmp = st.wHour;
    tmp <<= 5;
    tmp |= st.wMinute & 0x003f;
    tmp <<= 6;
    tmp |= (st.wSecond/2) & 0x001f;
    *dtime = tmp;
}

BOOL
DosDateTimeToFileTime(
    FILETIME * ft,
    WORD    ddate,
    WORD    dtime
    )
{
    SYSTEMTIME st;
    WORD Month, Day, Year;
    Day = ddate & 0x1F; //(first 5 bits)
    Month = (ddate >> 5) & 0xF; //(4 bits, after the Day)
    Year = ((ddate >> 9) & 0x7F) + 1980; //(7 bits)
    
    WORD Hour, Minute, Second;
    Second = ((dtime) & 0x1F) * 2;
    Minute = (dtime >> 5) & 0x3F;
    Hour = (dtime >> 11) & 0x1F;
    
    st.wYear = Year;
    st.wMonth = Month;
    st.wDayOfWeek = 0; //ignored
    st.wDay = Day;
    st.wHour = Hour;
    st.wMinute = Minute;
    st.wSecond = Second;
    st.wMilliseconds = 0;
    
    return SystemTimeToFileTime(&st, ft);    
}   



#define _70_to_80_bias    0x012CEA600L
#define SECS_IN_DAY (60L*60L*24L)
#define SEC2S_IN_DAY (30L*60L*24L)
#define FOURYEARS    (3*365+366)

WORD MonTotal[14] = { 0,        // dummy entry for month 0
    0,                                    // days before Jan 1
    31,                                    // days before Feb 1
    31+28,                                // days before Mar 1
    31+28+31,                            // days before Apr 1
    31+28+31+30,                        // days before May 1
    31+28+31+30+31,                        // days before Jun 1
    31+28+31+30+31+30,                    // days before Jul 1
    31+28+31+30+31+30+31,                // days before Aug 1
    31+28+31+30+31+30+31+31,             // days before Sep 1
    31+28+31+30+31+30+31+31+30,            // days before Oct 1
    31+28+31+30+31+30+31+31+30+31,        // days before Nov 1
    31+28+31+30+31+30+31+31+30+31+30,    // days before Dec 1
    31+28+31+30+31+30+31+31+30+31+30+31    // days before end of year
};

#define YR_MASK        0xFE00
#define LEAPYR_MASK    0x0600
#define YR_BITS        7
#define MON_MASK    0x01E0
#define MON_BITS    4
#define DAY_MASK    0x001F
#define DAY_BITS    5

#define HOUR_MASK    0xF800
#define HOUR_BITS    5
#define MIN_MASK    0x07E0
#define MIN_BITS    6
#define SEC2_MASK    0x001F
#define SEC2_BITS    5


DWORD
DosToNetTime(WORD time, WORD date)
{
    WORD days, secs2;

    days = date >> MON_BITS + DAY_BITS;
    days = days*365 + days/4;            // # of years in days
    days += (date & DAY_MASK) + MonTotal[(date&MON_MASK) >> DAY_BITS];
    if ((date&LEAPYR_MASK) == 0 && (date&MON_MASK) <= (2<<DAY_BITS))
        --days;                        // adjust days for early in leap year

    secs2 = ( ((time&HOUR_MASK) >> MIN_BITS+SEC2_BITS) * 60
                + ((time&MIN_MASK) >> SEC2_BITS) ) * 30
                + (time&SEC2_MASK);
    return (DWORD)days*SECS_IN_DAY + _70_to_80_bias + (DWORD)secs2*2;
}





DWORD SMB_FILE::SMB_Com_NT_TRASACT(SMB_PROCESS_CMD *_pRawRequest, SMB_PROCESS_CMD *_pRawResponse, UINT *puiUsed)
{
    DWORD dwRet = 0;
    SMB_COM_GENERIC_RESPONSE *pResponse = 
            (SMB_COM_GENERIC_RESPONSE *)_pRawResponse->pDataPortion;       
    
    //
    // Fill out return params and send back the data
    pResponse->ByteCount = 0;     
    pResponse->WordCount = 0;
    
    *puiUsed = sizeof(SMB_COM_GENERIC_RESPONSE);
    return 0x00020001;
}


//
// From cifs9f.doc
DWORD SMB_FILE::SMB_Com_LockX(SMB_PROCESS_CMD *_pRawRequest, 
                               SMB_PROCESS_CMD  *_pRawResponse, 
                               UINT *puiUsed,
                               SMB_PACKET *pSMB)
{
    DWORD dwRet = 0;
    ActiveConnection *pMyConnection = NULL;
    TIDState *pTIDState = NULL;
    
    SMB_LOCKX_CLIENT_REQUEST *pRequest =
            (SMB_LOCKX_CLIENT_REQUEST *)_pRawRequest->pDataPortion;
    SMB_LOCKX_SERVER_RESPONSE *pResponse = 
            (SMB_LOCKX_SERVER_RESPONSE *)_pRawResponse->pDataPortion;   
       
    StringTokenizer RequestTokenizer;
    UINT uiCnt = 0;
        
    //
    // Verify that we have enough memory
    if(_pRawRequest->uiDataSize < sizeof(SMB_LOCKX_CLIENT_REQUEST)) {
        TRACEMSG(ZONE_ERROR, (L"SMBSRV-CRACKER: SMB_Com_LockX -- not enough memory for request!"));
        dwRet = SMB_ERR(ERRSRV, ERRerror);
        goto Done;
    }
    if(_pRawResponse->uiDataSize < sizeof(SMB_LOCKX_SERVER_RESPONSE)) {
        TRACEMSG(ZONE_ERROR, (L"SMBSRV-CRACKER: SMB_Com_LockX -- not enough memory for response!"));
        dwRet = SMB_ERR(ERRSRV, ERRerror);
        goto Done;
    }        
    
    //
    // Find our connection state        
    if(NULL == (pMyConnection = SMB_Globals::g_pConnectionManager->FindConnection(pSMB))) {
       TRACEMSG(ZONE_ERROR, (L"SMBSRV: SMB_Com_LockX: -- cant find connection 0x%x!", pSMB->ulConnectionID));
       ASSERT(FALSE);
       dwRet = SMB_ERR(ERRSRV, ERRerror);
       goto Done;    
    }
        
    //
    // Find a share state 
    if(FAILED(pMyConnection->FindTIDState(_pRawRequest->pSMBHeader->Tid, &pTIDState, 0)) || NULL == pTIDState)
    {
        TRACEMSG(ZONE_ERROR, (L"SMBSRV-CRACKER: SMB_Com_LockX -- couldnt find share state!!"));
        dwRet = SMB_ERR(ERRSRV, ERRerror);
        goto Done;   
    }
    
    //
    // Make sure none of the locked ranges are locked 
    RequestTokenizer.Reset((BYTE *)(pRequest + 1), pRequest->ByteCount); 
    for(uiCnt = 0; uiCnt < pRequest->NumLocks; uiCnt++) {
         SMB_LOCK_RANGE *pRange = NULL;
         BOOL fLocked;
         
         if(FAILED(RequestTokenizer.GetByteArray((BYTE **)&pRange, sizeof(SMB_LOCK_RANGE)))) {
            TRACEMSG(ZONE_ERROR, (L"SMBSRV-CRACKER: SMB_Com_LockX -- problem getting range from packet!!"));
            dwRet = SMB_ERR(ERRSRV, ERRerror);
            goto Done;
         }
         
         if(FAILED(pTIDState->IsLocked(pRequest->FileID, pRange->PID, pRange->Offset, pRange->Length, &fLocked)) ||
            TRUE == fLocked) {
            TRACEMSG(ZONE_ERROR, (L"SMBSRV-CRACKER: SMB_Com_LockX -- range already locked!!!"));
            dwRet = SMB_ERR(ERRDOS, ERRlock);
            goto Done;
         }
    }
    
    //
    // Make sure the unlock ranges are locked locked
    for(uiCnt = 0; uiCnt < pRequest->NumUnlocks; uiCnt++) {
         SMB_LOCK_RANGE *pRange = NULL;
         BOOL fLocked;
         
         if(FAILED(RequestTokenizer.GetByteArray((BYTE **)&pRange, sizeof(SMB_LOCK_RANGE)))) {
            TRACEMSG(ZONE_ERROR, (L"SMBSRV-CRACKER: SMB_Com_LockX -- problem getting range from packet!!"));
            dwRet = SMB_ERR(ERRSRV, ERRerror);
            goto Done;
         }
         
         if(FAILED(pTIDState->IsLocked(pRequest->FileID, pRange->PID, pRange->Offset, pRange->Length, &fLocked)) ||
            FALSE == fLocked) {
            TRACEMSG(ZONE_ERROR, (L"SMBSRV-CRACKER: SMB_Com_LockX -- unlock requested on range not already locked: FID: %d   Offset: %d  Length: %d!!!",pRequest->FileID, pRange->Offset, pRange->Length));            
            ASSERT(FALSE);
         }
    }
    
    
    //
    // If we got here, perform the actual range locks
    RequestTokenizer.Reset((BYTE *)(pRequest + 1), pRequest->ByteCount); 
    for(uiCnt = 0; uiCnt < pRequest->NumLocks; uiCnt ++) {
         SMB_LOCK_RANGE *pRange = NULL;
         
         if(FAILED(RequestTokenizer.GetByteArray((BYTE **)&pRange, sizeof(SMB_LOCK_RANGE)))) {
            TRACEMSG(ZONE_ERROR, (L"SMBSRV-CRACKER: SMB_Com_LockX -- problem getting range from packet!!"));
            dwRet = SMB_ERR(ERRSRV, ERRerror);
            goto Done;
         }
         
         if(FAILED(pTIDState->Lock(pRequest->FileID, pRange->PID, pRange->Offset, pRange->Length))) {
            TRACEMSG(ZONE_ERROR, (L"SMBSRV-CRACKER: SMB_Com_LockX -- cant lock range!!!"));
            dwRet = SMB_ERR(ERRDOS, ERRlock);
            ASSERT(FALSE); //<--- we SHOULDNT be here unless something bad happened (b/c we verified above)
            goto Done;
         }
    }
    
    //
    // Also do any unlocks
    for(uiCnt = 0; uiCnt < pRequest->NumUnlocks; uiCnt ++) {
         SMB_LOCK_RANGE *pRange = NULL;
         
         if(FAILED(RequestTokenizer.GetByteArray((BYTE **)&pRange, sizeof(SMB_LOCK_RANGE)))) {
            TRACEMSG(ZONE_ERROR, (L"SMBSRV-CRACKER: SMB_Com_LockX -- problem getting range from packet!!"));
            dwRet = SMB_ERR(ERRSRV, ERRerror);
            goto Done;
         }
         
         if(FAILED(pTIDState->UnLock(pRequest->FileID, pRange->PID, pRange->Offset, pRange->Length))) {
            TRACEMSG(ZONE_ERROR, (L"SMBSRV-CRACKER: SMB_Com_LockX -- can't unlock range!!!"));
            ASSERT(FALSE); //<--- we SHOULDNT be here unless something bad happened (b/c we verified above)
         }
    }
    
    
    Done:
        //
        // Fill out return params and send back the data
        pResponse->ByteCount = 0;     
        pResponse->ANDX.AndXCommand = 0xFF; //assume we are the last command
        pResponse->ANDX.AndXReserved = 0;
        pResponse->ANDX.AndXOffset = 0;
        pResponse->ANDX.WordCount = 2; 


        *puiUsed = sizeof(SMB_LOCKX_SERVER_RESPONSE);
        return dwRet;
}



//
//  from SMBHLP.zip
DWORD SMB_FILE::SMB_Com_MakeDirectory(SMB_PROCESS_CMD *_pRawRequest, 
                                     SMB_PROCESS_CMD *_pRawResponse, 
                                     UINT *puiUsed,
                                     SMB_PACKET *pSMB)
{
    DWORD dwRet = 0;
    SMB_CREATE_DIRECTORY_CLIENT_REQUEST *pRequest =
            (SMB_CREATE_DIRECTORY_CLIENT_REQUEST *)_pRawRequest->pDataPortion;
    SMB_COM_GENERIC_RESPONSE *pResponse = 
            (SMB_COM_GENERIC_RESPONSE *)_pRawResponse->pDataPortion;   
    StringTokenizer RequestTokenizer; 
    BYTE *pSMBparam = NULL;    
    TIDState *pTIDState = NULL;    
    StringConverter FullPath;
    ActiveConnection *pMyConnection = NULL;
    
    //
    // Find our connection state        
    if(NULL == (pMyConnection = SMB_Globals::g_pConnectionManager->FindConnection(pSMB))) {
       TRACEMSG(ZONE_ERROR, (L"SMBSRV: SMB_INFO_ALLOCATION: -- cant find connection 0x%x!", pSMB->ulConnectionID));
       ASSERT(FALSE);
       dwRet = SMB_ERR(ERRSRV, ERRerror);
       goto Done;    
    }
        
    //
    // Find a share state 
    if(FAILED(pMyConnection->FindTIDState(_pRawRequest->pSMBHeader->Tid, &pTIDState, SEC_WRITE)) || NULL == pTIDState)
    {
        TRACEMSG(ZONE_ERROR, (L"SMBSRV-CRACKER: SMB_Com_MakeDirectory -- couldnt find share state!!"));
        dwRet = SMB_ERR(ERRSRV, ERRerror);
        goto Done;   
    }
        
    //
    // Verify that we have enough memory
    if(_pRawRequest->uiDataSize < sizeof(SMB_CREATE_DIRECTORY_CLIENT_REQUEST) + pRequest->ByteCount) {
        TRACEMSG(ZONE_ERROR, (L"SMBSRV-CRACKER: SMB_Com_MakeDirectory -- not enough memory for request!"));
        dwRet = SMB_ERR(ERRSRV, ERRerror);
        goto Done;
    }
    if(_pRawResponse->uiDataSize < sizeof(SMB_COM_GENERIC_RESPONSE)) {
        TRACEMSG(ZONE_ERROR, (L"SMBSRV-CRACKER: SMB_Com_MakeDirectory -- not enough memory for response!"));
        dwRet = SMB_ERR(ERRSRV, ERRerror);
        goto Done;
    }     
    
    //
    // build a tokenizer to get the filename
    pSMBparam = (BYTE *)(pRequest + 1);
    RequestTokenizer.Reset(pSMBparam, pRequest->ByteCount);      
    
    //
    // Build the full path name
    FullPath.append(pTIDState->GetShare()->GetDirectoryName());
    
  
    //
    // Get the directory name
    if(TRUE == pMyConnection->SupportsUnicode(pSMB->pInSMB)) {
        WCHAR *pDirectoryName = NULL;
            
        if(FAILED(RequestTokenizer.GetUnicodeString(&pDirectoryName))) {
            TRACEMSG(ZONE_ERROR, (L"SMBSRV-CRACKER: SMB_Com_MakeDirectory -- couldnt find filename!!"));
            dwRet = SMB_ERR(ERRSRV, ERRerror);
            goto Done;   
        }              
        FullPath.append(pDirectoryName);        
    } else {
        CHAR *pDirectoryName = NULL;
        
        if(FAILED(RequestTokenizer.GetString(&pDirectoryName))) {
            TRACEMSG(ZONE_ERROR, (L"SMBSRV-CRACKER: SMB_Com_MakeDirectory -- couldnt find filename!!"));
            dwRet = SMB_ERR(ERRSRV, ERRerror);
            goto Done;   
        }
        
        //
        // Check that we have the right string type
        if(0x04 != *pDirectoryName) {
            TRACEMSG(ZONE_ERROR, (L"SMBSRV-CRACKER:  SMB_Com_MakeDirectory -- invalid string type!"));
            dwRet = SMB_ERR(ERRSRV, ERRerror);
            goto Done;
        }
        
        //
        // Advance beyond the 0x04 header
        pDirectoryName ++;  
        
        FullPath.append(pDirectoryName);
    }
            
            
    //
    // make sure the request doesnt have any unexpected characters
    if(FAILED(pTIDState->GetShare()->IsValidPath(FullPath.GetString()))) {
        TRACEMSG(ZONE_ERROR, (L"SMBSRV-CRACKER: SMB_Com_MakeDirectory -- error filename has invalid character!"));
        dwRet = SMB_ERR(ERRSRV, ERRbadpath);
        goto Done;   
    }   
    
    //
    // Actually create the directory
    TRACEMSG(ZONE_FILES, (L"SMBSRV-CRACKER: SMB_Com_MakeDirectory -- creating directory %s", FullPath.GetString()));
    if(0 == CreateDirectory(FullPath.GetString(), 0)) {        
        if(SMB_ERR(ERRSRV, ERRerror) == (dwRet = ConvertGLEToSMB(GetLastError()))) {
            dwRet = SMB_ERR(ERRDOS, ERRbadfile);
        }
        TRACEMSG(ZONE_ERROR, (L"SMBSRV-CRACKER: SMB_Com_MakeDirectory -- error CreateDirectory() on directory %s failed with %d!", FullPath.GetString(), GetLastError()));
        goto Done;
    }
     
    //
    // Success
    dwRet = 0;
   
    Done:
        pResponse->ByteCount = 0;
        pResponse->WordCount = 0;
        *puiUsed = sizeof(SMB_COM_GENERIC_RESPONSE);
        return dwRet;
}


//
//  from SMBHLP.zip
DWORD SMB_FILE::SMB_Com_DelDirectory(SMB_PROCESS_CMD *_pRawRequest, 
                                     SMB_PROCESS_CMD *_pRawResponse, 
                                     UINT *puiUsed,
                                     SMB_PACKET *pSMB)
{
    DWORD dwRet = 0;
    SMB_DELETE_DIRECTORY_CLIENT_REQUEST *pRequest =
            (SMB_DELETE_DIRECTORY_CLIENT_REQUEST *)_pRawRequest->pDataPortion;
    SMB_COM_GENERIC_RESPONSE *pResponse = 
            (SMB_COM_GENERIC_RESPONSE *)_pRawResponse->pDataPortion;   
    StringTokenizer RequestTokenizer; 
    BYTE *pSMBparam = NULL;
    TIDState *pTIDState = NULL;    
    StringConverter FullPath;
    ActiveConnection *pMyConnection = NULL;
    
    //
    // Find our connection state        
    if(NULL == (pMyConnection = SMB_Globals::g_pConnectionManager->FindConnection(pSMB))) {
       TRACEMSG(ZONE_ERROR, (L"SMBSRV: SMB_INFO_ALLOCATION: -- cant find connection 0x%x!", pSMB->ulConnectionID));
       ASSERT(FALSE);
       dwRet = SMB_ERR(ERRSRV, ERRerror);
       goto Done;    
    }
        
    //
    // Find a share state 
    if(FAILED(pMyConnection->FindTIDState(_pRawRequest->pSMBHeader->Tid, &pTIDState, SEC_WRITE)) || NULL == pTIDState)
    {
        TRACEMSG(ZONE_ERROR, (L"SMBSRV-CRACKER: SMB_Com_MakeDirectory -- couldnt find share state!!"));
        dwRet = SMB_ERR(ERRSRV, ERRerror);
        goto Done;   
    }
    
    
    //
    // Verify that we have enough memory
    if(_pRawRequest->uiDataSize < sizeof(SMB_DELETE_DIRECTORY_CLIENT_REQUEST) + pRequest->ByteCount) {
        TRACEMSG(ZONE_ERROR, (L"SMBSRV-CRACKER: SMB_Com_MakeDirectory -- not enough memory for request!"));
        dwRet = SMB_ERR(ERRSRV, ERRerror);
        goto Done;
    }
    if(_pRawResponse->uiDataSize < sizeof(SMB_COM_GENERIC_RESPONSE)) {
        TRACEMSG(ZONE_ERROR, (L"SMBSRV-CRACKER: SMB_Com_MakeDirectory -- not enough memory for response!"));
        dwRet = SMB_ERR(ERRSRV, ERRerror);
        goto Done;
    }     
    
    //
    // build a tokenizer to get the filename
    pSMBparam = (BYTE *)(pRequest + 1);
    RequestTokenizer.Reset(pSMBparam, pRequest->ByteCount);      
    
    //
    // Build the full path name
    FullPath.append(pTIDState->GetShare()->GetDirectoryName());    
  
    //
    // Get the directory name
    if(TRUE == pMyConnection->SupportsUnicode(pSMB->pInSMB)) {
        WCHAR *pDirectoryName = NULL;
            
        if(FAILED(RequestTokenizer.GetUnicodeString(&pDirectoryName))) {
            TRACEMSG(ZONE_ERROR, (L"SMBSRV-CRACKER: SMB_Com_MakeDirectory -- couldnt find filename!!"));
            dwRet = SMB_ERR(ERRSRV, ERRerror);
            goto Done;   
        }        
        FullPath.append(pDirectoryName);        
    } else {
        CHAR *pDirectoryName = NULL;
        
        if(FAILED(RequestTokenizer.GetString(&pDirectoryName))) {
            TRACEMSG(ZONE_ERROR, (L"SMBSRV-CRACKER: SMB_Com_MakeDirectory -- couldnt find filename!!"));
            dwRet = SMB_ERR(ERRSRV, ERRerror);
            goto Done;   
        }
        
        //
        // Check that we have the right string type
        if(0x04 != *pDirectoryName) {
            TRACEMSG(ZONE_ERROR, (L"SMBSRV-CRACKER:  SMB_Com_MakeDirectory -- invalid string type!"));
            dwRet = SMB_ERR(ERRSRV, ERRerror);
            goto Done;
        }
        
        //
        // Advance beyond the 0x04 header
        pDirectoryName ++;  
        
        FullPath.append(pDirectoryName);
    }
        
    //
    // make sure the request doesnt have any unexpected characters
    if(FAILED(pTIDState->GetShare()->IsValidPath(FullPath.GetString()))) {    
        TRACEMSG(ZONE_ERROR, (L"SMBSRV-CRACKER: SMB_Com_MakeDirectory -- error filename has invalid character!"));
        dwRet = SMB_ERR(ERRSRV, ERRbadpath);
        goto Done;   
    }   
    
    //
    // Actually remove the directory
    if(0 == RemoveDirectory(FullPath.GetString())) {
        TRACEMSG(ZONE_ERROR, (L"SMBSRV-CRACKER: SMB_Com_MakeDirectory -- error CreateDirectory() on directory %s failed with %d!", FullPath.GetString(), GetLastError()));
        if(SMB_ERR(ERRSRV, ERRerror) == (dwRet = ConvertGLEToSMB(GetLastError()))) {
            dwRet = SMB_ERR(ERRSRV, ERRbadpath);
        }
        goto Done;
    }
     
    //
    // Success
    dwRet = 0;
   
    Done:
        pResponse->ByteCount = 0;
        pResponse->WordCount = 0;
        *puiUsed = sizeof(SMB_COM_GENERIC_RESPONSE);
        return dwRet;
}





DWORD SMB_FILE::SMB_Com_DeleteFile(SMB_PROCESS_CMD *_pRawRequest, 
                                  SMB_PROCESS_CMD *_pRawResponse, 
                                  UINT *puiUsed,
                                  SMB_PACKET *pSMB)
{
    DWORD dwRet = 0;
    SMB_DELETE_FILE_CLIENT_REQUEST *pRequest =
            (SMB_DELETE_FILE_CLIENT_REQUEST *)_pRawRequest->pDataPortion;
    SMB_DELETE_FILE_SERVER_RESPONSE *pResponse = 
            (SMB_DELETE_FILE_SERVER_RESPONSE *)_pRawResponse->pDataPortion;   
    StringTokenizer RequestTokenizer; 
    BYTE *pSMBparam = NULL;
    TIDState *pTIDState = NULL;    
    StringConverter FullPath;
    ActiveConnection *pMyConnection;
    
    //
    // Find our connection state        
    if(NULL == (pMyConnection = SMB_Globals::g_pConnectionManager->FindConnection(pSMB))) {
       TRACEMSG(ZONE_ERROR, (L"SMBSRV: SMB_INFO_ALLOCATION: -- cant find connection 0x%x!", pSMB->ulConnectionID));
       ASSERT(FALSE);
       dwRet = SMB_ERR(ERRSRV, ERRerror);
       goto Done;    
    }
        
    //
    // Find a share state 
    if(FAILED(pMyConnection->FindTIDState(_pRawRequest->pSMBHeader->Tid, &pTIDState, SEC_WRITE)) || NULL == pTIDState) {
        TRACEMSG(ZONE_ERROR, (L"SMBSRV-CRACKER: SMB_Com_DeleteFile -- couldnt find share state!!"));
        dwRet = SMB_ERR(ERRSRV, ERRerror);
        goto Done;   
    }
        
    //
    // Verify that we have enough memory
    if(_pRawRequest->uiDataSize < sizeof(SMB_DELETE_FILE_CLIENT_REQUEST) + pRequest->ByteCount) {
        TRACEMSG(ZONE_ERROR, (L"SMBSRV-CRACKER: SMB_Com_DeleteFile -- not enough memory for request!"));
        dwRet = SMB_ERR(ERRSRV, ERRerror);
        goto Done;
    }
    if(_pRawResponse->uiDataSize < sizeof(SMB_DELETE_FILE_SERVER_RESPONSE)) {
        TRACEMSG(ZONE_ERROR, (L"SMBSRV-CRACKER: SMB_Com_DeleteFile -- not enough memory for response!"));
        dwRet = SMB_ERR(ERRSRV, ERRerror);
        goto Done;
    }     
    
    //
    // build a tokenizer to get the filename
    pSMBparam = (BYTE *)(pRequest + 1);
    RequestTokenizer.Reset(pSMBparam, pRequest->ByteCount);  
      
    //
    // Build the full path name
    FullPath.append(pTIDState->GetShare()->GetDirectoryName());    

    //
    // Get the directory name
    if(TRUE == pMyConnection->SupportsUnicode(pSMB->pInSMB)) {
       WCHAR *pDirectoryName = NULL;
           
       if(FAILED(RequestTokenizer.GetUnicodeString(&pDirectoryName))) {
           TRACEMSG(ZONE_ERROR, (L"SMBSRV-CRACKER: SMB_Com_MakeDirectory -- couldnt find filename!!"));
           dwRet = SMB_ERR(ERRSRV, ERRerror);
           goto Done;   
       }              
       FullPath.append(pDirectoryName);        
    } else {    
       CHAR *pDirectoryName = NULL;
       
       if(FAILED(RequestTokenizer.GetString(&pDirectoryName))) {
           TRACEMSG(ZONE_ERROR, (L"SMBSRV-CRACKER: SMB_Com_MakeDirectory -- couldnt find filename!!"));
           dwRet = SMB_ERR(ERRSRV, ERRerror);
           goto Done;   
       }
       
       //
       // Check that we have the right string type
       if(0x04 != *pDirectoryName) {
           TRACEMSG(ZONE_ERROR, (L"SMBSRV-CRACKER:  SMB_Com_MakeDirectory -- invalid string type!"));
           dwRet = SMB_ERR(ERRSRV, ERRerror);
           goto Done;
       }
       
       //
       // Advance beyond the 0x04 header
       pDirectoryName ++;  
       
       FullPath.append(pDirectoryName);
    }
    
    //
    // make sure the request doesnt have any unexpected characters   
    if(FAILED(pTIDState->GetShare()->IsValidPath(FullPath.GetString()))) {   
        TRACEMSG(ZONE_ERROR, (L"SMBSRV-CRACKER: SMB_Com_DeleteFile -- error filename has invalid character!"));
        dwRet = SMB_ERR(ERRSRV, ERRbadfile);
        goto Done;   
    }   
    
    //
    // Actually delete the file
    if(0 == DeleteFile(FullPath.GetString())) {
        TRACEMSG(ZONE_ERROR, (L"SMBSRV-CRACKER: SMB_Com_DeleteFile -- error DeleteFile() on filename %s failed with %d!", FullPath.GetString(), GetLastError()));
        if(SMB_ERR(ERRSRV, ERRerror) == (dwRet = ConvertGLEToSMB(GetLastError()))) {
            dwRet = SMB_ERR(ERRSRV, ERRbadfile);
        }
        goto Done;
    }
     
    //
    // Success
    dwRet = 0;
   
    Done:
        pResponse->ByteCount = 0;
        pResponse->WordCount = 0;
        *puiUsed = sizeof(SMB_DELETE_FILE_SERVER_RESPONSE);
        return dwRet;
}






DWORD SMB_FILE::SMB_Com_RenameFile(SMB_PROCESS_CMD *_pRawRequest, 
                                    SMB_PROCESS_CMD *_pRawResponse, 
                                    UINT *puiUsed,
                                    SMB_PACKET *pSMB)
{
    DWORD dwRet = 0;
    SMB_RENAME_FILE_CLIENT_REQUEST *pRequest =
            (SMB_RENAME_FILE_CLIENT_REQUEST *)_pRawRequest->pDataPortion;
    SMB_COM_GENERIC_RESPONSE *pResponse = 
            (SMB_COM_GENERIC_RESPONSE *)_pRawResponse->pDataPortion;   
    StringTokenizer RequestTokenizer; 
    BYTE *pSMBparam = NULL;
    TIDState *pTIDState = NULL;  
    ActiveConnection *pMyConnection;
    StringConverter FullNewFile;
    StringConverter FullOldFile;
    
    //
    // Verify that we have enough memory
    if(_pRawRequest->uiDataSize < sizeof(SMB_RENAME_FILE_CLIENT_REQUEST) + pRequest->ByteCount) {
        TRACEMSG(ZONE_ERROR, (L"SMBSRV-CRACKER: SMB_Com_RenameFile -- not enough memory for request!"));
        dwRet = SMB_ERR(ERRSRV, ERRerror);
        goto Done;
    }
    if(_pRawResponse->uiDataSize < sizeof(SMB_COM_GENERIC_RESPONSE)) {
        TRACEMSG(ZONE_ERROR, (L"SMBSRV-CRACKER: SMB_Com_RenameFile -- not enough memory for response!"));
        dwRet = SMB_ERR(ERRSRV, ERRerror);
        goto Done;
    }     
    
    //
    // build a tokenizer to get the filenames
    pSMBparam = (BYTE *)(pRequest + 1);
    RequestTokenizer.Reset(pSMBparam, pRequest->ByteCount);    
    
    //
    // Find our connection state        
    if(NULL == (pMyConnection = SMB_Globals::g_pConnectionManager->FindConnection(pSMB))) {
       TRACEMSG(ZONE_ERROR, (L"SMBSRV: SMB_INFO_ALLOCATION: -- cant find connection 0x%x!", pSMB->ulConnectionID));
       ASSERT(FALSE);
       dwRet = SMB_ERR(ERRSRV, ERRerror);
       goto Done;    
    }
    
    //
    // Find a share state 
    if(FAILED(pMyConnection->FindTIDState(_pRawRequest->pSMBHeader->Tid, &pTIDState,SEC_WRITE)) || NULL == pTIDState)
    {
        TRACEMSG(ZONE_ERROR, (L"SMBSRV-CRACKER: SMB_Com_RenameFile -- couldnt find share state!!"));
        dwRet = SMB_ERR(ERRSRV, ERRerror);
        goto Done;   
    }
    
    
    //
    // Build the full path name
    FullOldFile.append(pTIDState->GetShare()->GetDirectoryName());    
    FullNewFile.append(pTIDState->GetShare()->GetDirectoryName());        
        
    //
    // Get the old/new filename
    if(FALSE == pMyConnection->SupportsUnicode(pSMB->pInSMB)) {
        CHAR *pFileOld = NULL;
        CHAR *pFileNew = NULL;   
        BYTE fileOldID, fileNewID;
        
        if(FAILED(RequestTokenizer.GetByte(&fileOldID)) || 0x04 != fileOldID) {
            TRACEMSG(ZONE_ERROR, (L"SMBSRV-CRACKER: SMB_Com_RenameFile -- improper string id on old filename!!"));
            dwRet = SMB_ERR(ERRSRV, ERRerror);
            goto Done;  
        }
        if(FAILED(RequestTokenizer.GetString(&pFileOld))) {
            TRACEMSG(ZONE_ERROR, (L"SMBSRV-CRACKER: SMB_Com_RenameFile -- couldnt find old filename!!"));
            dwRet = SMB_ERR(ERRSRV, ERRerror);
            goto Done;   
        }
        if(FAILED(RequestTokenizer.GetByte(&fileNewID)) || 0x04 != fileNewID) {
            TRACEMSG(ZONE_ERROR, (L"SMBSRV-CRACKER: SMB_Com_RenameFile -- improper string id on new filename!!"));
            dwRet = SMB_ERR(ERRSRV, ERRerror);
            goto Done;  
        }   
        if(FAILED(RequestTokenizer.GetString(&pFileNew))) {
            TRACEMSG(ZONE_ERROR, (L"SMBSRV-CRACKER: SMB_Com_RenameFile -- couldnt find new filename!!"));
            dwRet = SMB_ERR(ERRSRV, ERRerror);
            goto Done;   
        }                
        FullOldFile.append(pFileOld);
        FullNewFile.append(pFileNew);        
    } else {
        WCHAR *pFileOld = NULL;
        WCHAR *pFileNew = NULL;    
        BYTE fileOldID, fileNewID;
        if(FAILED(RequestTokenizer.GetByte(&fileOldID)) || 0x04 != fileOldID) {
            TRACEMSG(ZONE_ERROR, (L"SMBSRV-CRACKER: SMB_Com_RenameFile -- improper string id on old filename!!"));
            dwRet = SMB_ERR(ERRSRV, ERRerror);
            goto Done;  
        }
        if(FAILED(RequestTokenizer.GetUnicodeString(&pFileOld))) {
            TRACEMSG(ZONE_ERROR, (L"SMBSRV-CRACKER: SMB_Com_RenameFile -- couldnt find old filename!!"));
            dwRet = SMB_ERR(ERRSRV, ERRerror);
            goto Done;   
        }
        if(FAILED(RequestTokenizer.GetByte(&fileNewID)) || 0x04 != fileNewID) {
            TRACEMSG(ZONE_ERROR, (L"SMBSRV-CRACKER: SMB_Com_RenameFile -- improper string id on new filename!!"));
            dwRet = SMB_ERR(ERRSRV, ERRerror);
            goto Done;  
        }        
        if(FAILED(RequestTokenizer.GetUnicodeString(&pFileNew))) {
            TRACEMSG(ZONE_ERROR, (L"SMBSRV-CRACKER: SMB_Com_RenameFile -- couldnt find new filename!!"));
            dwRet = SMB_ERR(ERRSRV, ERRerror);
            goto Done;   
        }                
        FullOldFile.append(pFileOld);
        FullNewFile.append(pFileNew);    
    }
    
                    
    
    //
    // make sure the request doesnt have any unexpected characters
    if(FAILED(pTIDState->GetShare()->IsValidPath(FullOldFile.GetString()))) {
        TRACEMSG(ZONE_ERROR, (L"SMBSRV-CRACKER: SMB_Com_RenameFile -- error old filename has invalid character!"));
        dwRet = SMB_ERR(ERRSRV, ERRbadfile);         
        goto Done;   
    }  
               
    



    if(0 == wcscmp(FullOldFile.GetString(), FullNewFile.GetString())) {
        TRACEMSG(ZONE_FILES, (L"SMBSRV-CRACKER: SMB_Com_RenameFile -- Files are the same... doing nothing %s->%s!", FullOldFile.GetString(), FullNewFile.GetString()));
    } else if(0 == MoveFile(FullOldFile.GetString(), FullNewFile.GetString())) {
           TRACEMSG(ZONE_ERROR, (L"SMBSRV-CRACKER: SMB_Com_RenameFile -- MoveFile() failed with %d on %s->%s!", GetLastError(), FullOldFile.GetString(), FullNewFile.GetString()));
           DWORD dwGLE = GetLastError();
           if(SMB_ERR(ERRSRV, ERRerror) == (dwRet = ConvertGLEToSMB(GetLastError()))) {
              dwRet = SMB_ERR(ERRSRV, ERRbadfile);
           }
           goto Done;   
    }  
    
    //
    // Success
    dwRet = 0;   
    Done:
        pResponse->ByteCount = 0;
        pResponse->WordCount = 0;
        *puiUsed = sizeof(SMB_COM_GENERIC_RESPONSE);
        return dwRet;
}



DWORD SMB_FILE::SMB_Com_TRANS2(SMB_PROCESS_CMD *_pRawRequest, 
                                 SMB_PROCESS_CMD *_pRawResponse, 
                                 UINT *puiUsed,
                                 SMB_PACKET *pSMB)
{   
   DWORD dwRet = 0;
   BYTE *pSMBparam;   
   SMB_COM_TRANSACTION2_CLIENT_REQUEST  *pRequest  = (SMB_COM_TRANSACTION2_CLIENT_REQUEST *)_pRawRequest->pDataPortion;
   SMB_COM_TRANSACTION2_SERVER_RESPONSE *pResponse = (SMB_COM_TRANSACTION2_SERVER_RESPONSE *)_pRawResponse->pDataPortion;
   ASSERT(TRUE == g_fFileServer);
   
   WORD wAPI;
   StringTokenizer RequestTokenizer;
   BOOL fNotSupported = FALSE;

   // 
   // Verify incoming parameters
   if(_pRawRequest->uiDataSize < sizeof(SMB_COM_TRANSACTION2_CLIENT_REQUEST) ||
       _pRawResponse->uiDataSize < sizeof(SMB_COM_TRANSACTION2_SERVER_RESPONSE) ||
       _pRawRequest->uiDataSize + sizeof(SMB_HEADER) < pRequest->DataOffset) {
        TRACEMSG(ZONE_ERROR, (L"SMBSRV: Error -- transaction -- not enough memory"));
        dwRet = SMB_ERR(ERRSRV, ERRerror);      
        goto Done;
   }
   if(pRequest->WordCount != 15 || 1 != pRequest->SetupCount) {
        TRACEMSG(ZONE_ERROR, (L"SMBSRV: Error -- word count on transaction has to be 15 and setupcount must be 1 -- error"));        
        dwRet = SMB_ERR(ERRSRV, ERRerror); 
        goto Done;
   }  
   
   //
   // Point the pSMBparam into the parameter section of the SMB
   pSMBparam = (BYTE *)_pRawRequest->pSMBHeader + pRequest->ParameterOffset;
   RequestTokenizer.Reset(pSMBparam, pRequest->ParameterCount);
   
   //
   // Get the API
   wAPI = pRequest->Setup;
   
   {                        
        switch(wAPI) {
            case TRANS2_QUERY_FILE_INFORMATION:
                dwRet = SMB_Trans2_Query_File_Information(_pRawRequest->pSMBHeader->Tid, 
                                              pRequest, 
                                              &RequestTokenizer,
                                              _pRawResponse,
                                              pResponse,
                                              pSMB,
                                              puiUsed);
                break;                              
            case TRANS2_FIND_FIRST2:
                dwRet = SMB_Trans2_Find_First(_pRawRequest->pSMBHeader->Tid, 
                                              pRequest, 
                                              &RequestTokenizer,
                                              _pRawResponse,
                                              pResponse,
                                              pSMB,
                                              puiUsed);                                     
                break;
           case TRANS2_FIND_NEXT2:
                dwRet = SMB_Trans2_Find_Next(_pRawRequest->pSMBHeader->Tid, 
                                              pRequest, 
                                              &RequestTokenizer,
                                              _pRawResponse,
                                              pResponse,
                                              pSMB,
                                              puiUsed);                                     
                break;            
            case TRANS2_QUERY_FS_INFORMATION:
            {
                RAPIBuilder RAPI((BYTE *)(pResponse+1), 
                                        _pRawResponse->uiDataSize-sizeof(SMB_COM_TRANSACTION2_SERVER_RESPONSE), 
                                        pRequest->MaxParameterCount, 
                                        pRequest->MaxDataCount);
                                
                dwRet = SMB_Trans2_Query_FS_Information(pRequest, 
                                                        &RequestTokenizer,
                                                        &RAPI,
                                                        pResponse, 
                                                        _pRawRequest->pSMBHeader->Tid,
                                                        pSMB);
                //
                // On error just return that error
                if(0 != dwRet) 
                    goto Done;
                
                //fill out response SMB 
                memset(pResponse, 0, sizeof(SMB_COM_TRANSACTION2_SERVER_RESPONSE));
                
                //word count is 3 bytes (1=WordCount 2=ByteCount) less than the response
                pResponse->WordCount = (sizeof(SMB_COM_TRANSACTION2_SERVER_RESPONSE) - 3) / sizeof(WORD);                              
                pResponse->TotalParameterCount = RAPI.ParamBytesUsed();
                pResponse->TotalDataCount = RAPI.DataBytesUsed();
                pResponse->ParameterCount = RAPI.ParamBytesUsed();
                pResponse->ParameterOffset = RAPI.ParamOffset((BYTE *)_pRawResponse->pSMBHeader); 
                pResponse->ParameterDisplacement = 0;
                pResponse->DataCount = RAPI.DataBytesUsed();
                pResponse->DataOffset = RAPI.DataOffset((BYTE *)_pRawResponse->pSMBHeader);
                pResponse->DataDisplacement = 0;
                pResponse->SetupCount = 0;
                pResponse->ByteCount = RAPI.TotalBytesUsed();               
                                
                ASSERT(10 == pResponse->WordCount);  
                     
                //set the number of bytes we used         
                *puiUsed = RAPI.TotalBytesUsed() + sizeof(SMB_COM_TRANSACTION2_SERVER_RESPONSE);                                                        
            }                                           
            break;
        }
  
     }
   Done:
       return dwRet;
}


DWORD SMB_FILE::SMB_Com_Query_Information(SMB_PROCESS_CMD *_pRawRequest, 
                                         SMB_PROCESS_CMD *_pRawResponse, 
                                         UINT *puiUsed,
                                         SMB_PACKET *pSMB)
{
    DWORD dwRet = 0;
    *puiUsed = 0;
    
    TIDState *pMyTIDState = NULL;
    SMB_COM_QUERY_INFO_REQUEST *pRequest = (SMB_COM_QUERY_INFO_REQUEST *)_pRawRequest->pDataPortion;    
    SMB_COM_QUERY_INFO_RESPONSE *pResponse = (SMB_COM_QUERY_INFO_RESPONSE *)_pRawResponse->pDataPortion;   
    StringConverter FileString;
    ActiveConnection *pMyConnection = NULL; 
    StringTokenizer RequestTokenizer;
    BYTE *pSMBparam = NULL;
    
    //
    // Find our connection state        
    if(NULL == (pMyConnection = SMB_Globals::g_pConnectionManager->FindConnection(pSMB))) {
       TRACEMSG(ZONE_ERROR, (L"SMBSRV: SMB_INFO_ALLOCATION: -- cant find connection 0x%x!", pSMB->ulConnectionID));
       ASSERT(FALSE);
       dwRet = SMB_ERR(ERRSRV, ERRerror);
       goto Done;    
    }  
    
    //
    // Verify incoming params
    if(_pRawRequest->uiDataSize - sizeof(SMB_COM_QUERY_INFO_REQUEST) < (UINT)(pRequest->ByteCount - 1)) {
        TRACEMSG(ZONE_ERROR, (L"SMBSRV:  COM_QUERY_INFO has a string larger than packet!! -- someone is tricking us?"));
        dwRet = SMB_ERR(ERRSRV, ERRerror);
        goto Done;
    }
    if(0 != pRequest->WordCount) {
        TRACEMSG(ZONE_ERROR, (L"SMBSRV:  COM_QUERY_INFO -- unexpected word count!"));
        dwRet = SMB_ERR(ERRSRV, ERRerror);
        goto Done;
    }
    
    //
    // Go get our TIDState information
    if(FAILED(pMyConnection->FindTIDState(_pRawRequest->pSMBHeader->Tid, &pMyTIDState, SEC_READ)) ||
       NULL == pMyTIDState) {
        TRACEMSG(ZONE_ERROR, (L"SMBSRV:  COM_QUERY_INFO -- couldnt find Tid info for %d!", _pRawRequest->pSMBHeader->Tid));
        goto BadFile;
    }
    if(FAILED(FileString.append(pMyTIDState->GetShare()->GetDirectoryName()))) {
        TRACEMSG(ZONE_ERROR, (L"SMBSRV:  COM_QUERY_INFO -- putting path on filename FAILED!!"));
        goto BadFile;
    }
    //
    // build a tokenizer to get the filename
    pSMBparam = (BYTE *)(pRequest + 1);
    RequestTokenizer.Reset(pSMBparam, pRequest->ByteCount); 
        
    if(FALSE == pMyConnection->SupportsUnicode(pSMB->pInSMB)) {
        CHAR *pFileName = (CHAR *)(pRequest + 1);
        if(FAILED(RequestTokenizer.GetString(&pFileName))) {
            TRACEMSG(ZONE_ERROR, (L"SMBSRV:  COM_QUERY_INFO -- getting filename failed!!"));
            goto BadFile;
        }           

        if(0x04 != *pFileName) {
            TRACEMSG(ZONE_ERROR, (L"SMBSRV:  COM_QUERY_INFO -- invalid string type!"));
            ASSERT(FALSE);
            goto BadFile;
        }
        //
        // Advance beyond the string identifier
        pFileName ++; 

        if(FAILED(FileString.append(pFileName))) {
            TRACEMSG(ZONE_ERROR, (L"SMBSRV:  COM_QUERY_INFO -- appending filename to path FAILED!!"));
            goto BadFile;
        }        
    } else {
        WCHAR *pFileName = NULL;
        
        if(FAILED(RequestTokenizer.GetUnicodeString(&pFileName))) {
            TRACEMSG(ZONE_ERROR, (L"SMBSRV:  COM_QUERY_INFO -- getting filename failed!!"));
            goto BadFile;
        }   
               
        if(FAILED(FileString.append(pFileName))) {
            TRACEMSG(ZONE_ERROR, (L"SMBSRV:  COM_QUERY_INFO -- appending filename to path FAILED!!"));
            goto BadFile;
        }
    }
    TRACEMSG(ZONE_FILES, (L"SMBSRV:  Checking attributes for file %s", FileString.GetString()));
    
    {
    WIN32_FIND_DATA fd;
    HANDLE hFileHand = FindFirstFile(FileString.GetString(), &fd);
    
    if(INVALID_HANDLE_VALUE == hFileHand) {
        TRACEMSG(ZONE_FILES, (L"SMBSRV:  COM_QUERY_INFO -- getting attributes for %s failed (%d)!!", FileString.GetString(), GetLastError()));
        goto BadFile;
    }
    FindClose(hFileHand);
    
    WORD attributes = Win32AttributeToDos(fd.dwFileAttributes);
    FILETIME UTCWriteTime;
    
    //WORDCOUNT = response struct - 3 ( for 'bytecount' and the actual word count byte) / size of a word
    pResponse->Fields.WordCount = (sizeof(SMB_COM_QUERY_INFO_RESPONSE)-3)/sizeof(WORD);
    pResponse->Fields.FileAttributes = attributes;

    if(0 == FileTimeToLocalFileTime(&fd.ftLastWriteTime, &UTCWriteTime)) {
        TRACEMSG(ZONE_FILES, (L"SMBSRV: COM_QUERY_INFO -- error converting file time to local time"));
        goto BadFile;
    }
    pResponse->Fields.LastModifyTime = SecSinceJan1970_0_0_0(&UTCWriteTime);
    pResponse->Fields.FileSize = fd.nFileSizeLow;
    pResponse->Fields.Unknown = 0;
    pResponse->Fields.Unknown2 = 0;
    pResponse->Fields.Unknown3 = 0;    
    pResponse->ByteCount = 0;
    *puiUsed = sizeof(SMB_COM_QUERY_INFO_RESPONSE);
    }
    
    goto Done;
    BadFile:
        {
            BYTE *pTmp = (BYTE *)pResponse;
            DWORD dwErr = GetLastError();
            pTmp[0] = 0;
            pTmp[1] = 0;
            pTmp[2] = 0;        
            *puiUsed = 3;
            if(ERROR_PATH_NOT_FOUND == dwErr) {
                dwRet = SMB_ERR(ERRDOS, ERRbadpath);
            } else {
                dwRet = SMB_ERR(ERRDOS, ERRbadfile);
            }           
        }
    
    Done:
        return dwRet;
}

DWORD SMB_FILE::SMB_Com_Query_Information_Disk(SMB_PROCESS_CMD *_pRawRequest, 
                                         SMB_PROCESS_CMD *_pRawResponse, 
                                         UINT *puiUsed,
                                         SMB_PACKET *pSMB)
{
    DWORD dwRet = 0;
    *puiUsed = 0;
    
    TIDState *pMyTIDState = NULL;
    SMB_COM_QUERY_INFO_DISK_REQUEST *pRequest = (SMB_COM_QUERY_INFO_DISK_REQUEST *)_pRawRequest->pDataPortion;    
    SMB_COM_QUERY_INFO_DISK_RESPONSE *pResponse = (SMB_COM_QUERY_INFO_DISK_RESPONSE *)_pRawResponse->pDataPortion;  
    ActiveConnection *pMyConnection = NULL; 
    ULARGE_INTEGER FreeToCaller;
    ULARGE_INTEGER NumberBytes;
    ULARGE_INTEGER TotalFree;
    
    BYTE *pSMBparam = NULL;
    
    //
    // Find our connection state        
    if(NULL == (pMyConnection = SMB_Globals::g_pConnectionManager->FindConnection(pSMB))) {
       TRACEMSG(ZONE_ERROR, (L"SMBSRV: SMB_Com_Query_Information_Disk: -- cant find connection 0x%x!", pSMB->ulConnectionID));
       ASSERT(FALSE);
       dwRet = SMB_ERR(ERRSRV, ERRerror);
       goto Done;    
    }  
    
    //
    // Verify incoming params
    if(_pRawRequest->uiDataSize < sizeof(SMB_COM_QUERY_INFO_DISK_REQUEST)) {
        TRACEMSG(ZONE_ERROR, (L"SMBSRV:  SMB_Com_Query_Information_Disk has a string larger than packet!! -- someone is tricking us?"));
        dwRet = SMB_ERR(ERRSRV, ERRerror);
        goto Done;
    }
    if(0 != pRequest->WordCount) {
        TRACEMSG(ZONE_ERROR, (L"SMBSRV:  SMB_Com_Query_Information_Disk -- unexpected word count!"));
        dwRet = SMB_ERR(ERRSRV, ERRerror);
        goto Done;
    }
    
    //
    // Go get our TIDState information
    if(FAILED(pMyConnection->FindTIDState(_pRawRequest->pSMBHeader->Tid, &pMyTIDState, SEC_READ)) ||
       NULL == pMyTIDState) {
        TRACEMSG(ZONE_ERROR, (L"SMBSRV:  SMB_Com_Query_Information_Disk -- couldnt find Tid info for %d!", _pRawRequest->pSMBHeader->Tid));
        dwRet = SMB_ERR(ERRSRV, ERRerror);
        goto Done;
    }
    
    //
    // Get the amount of free disk SPACEPARITY
    if(0 == GetDiskFreeSpaceEx(pMyTIDState->GetShare()->GetDirectoryName(),
                               &FreeToCaller,
                               &NumberBytes,
                               &TotalFree)) {
        TRACEMSG(ZONE_ERROR, (L"SMBSRV: SMB_Com_Query_Information_Disk -- couldnt get free disk space!!"));              
        goto Done;                               
    }
     
    //
    // Fill in parameters
    pResponse->WordCount = (sizeof(SMB_COM_QUERY_INFO_DISK_RESPONSE)-3)/sizeof(WORD);
    ASSERT(5 == pResponse->WordCount);
    pResponse->TotalUnits = 0;
    pResponse->BlocksPerUnit = 2048;
    pResponse->BlockSize = 512;
    ASSERT(NumberBytes.QuadPart / pResponse->BlocksPerUnit / pResponse->BlockSize <= 0xFFFF);
   
    //
    // Compute the number of units
    pResponse->TotalUnits = (USHORT)(NumberBytes.QuadPart / pResponse->BlocksPerUnit / pResponse->BlockSize);
    pResponse->FreeUnits = (USHORT)(TotalFree.QuadPart / pResponse->BlocksPerUnit / pResponse->BlockSize);
    pResponse->Reserved = 0;
    pResponse->ByteCount = 0;
    *puiUsed = sizeof(SMB_COM_QUERY_INFO_DISK_RESPONSE);
         
   
    Done:
        return dwRet;
}


DWORD SMB_FILE::SMB_Com_SetInformation(SMB_PROCESS_CMD *_pRawRequest, 
                                      SMB_PROCESS_CMD *_pRawResponse, 
                                      UINT *puiUsed,
                                      SMB_PACKET *pSMB)
{
    DWORD dwRet = 0;
    *puiUsed = 0;
    
    TIDState *pMyTIDState = NULL;
    SMB_SET_ATTRIBUTE_CLIENT_REQUEST *pRequest = (SMB_SET_ATTRIBUTE_CLIENT_REQUEST *)_pRawRequest->pDataPortion;
    SMB_COM_GENERIC_RESPONSE *pResponse = (SMB_COM_GENERIC_RESPONSE *)_pRawResponse->pDataPortion;  
   
    FILETIME WriteTime;
    FILETIME *pWriteTime = &WriteTime;
    WORD     Win32Attributes  = 0;
    StringConverter FileName;
    HANDLE h = INVALID_HANDLE_VALUE;
    BOOL fIsDir = FALSE;
    BYTE *pSMBparam = NULL;
    ActiveConnection *pMyConnection = NULL;
    StringTokenizer RequestTokenizer;
    
    //
    // Verify incoming params
    if(_pRawRequest->uiDataSize < sizeof(SMB_SET_ATTRIBUTE_CLIENT_REQUEST)) {
        TRACEMSG(ZONE_ERROR, (L"SMBSRV:  SMB_Com_SetInformation has a string larger than packet!! -- someone is tricking us?"));
        dwRet = SMB_ERR(ERRSRV, ERRerror);
        goto Done;
    }
    if(8 != pRequest->WordCount) {
        TRACEMSG(ZONE_ERROR, (L"SMBSRV:  SMB_Com_SetInformation -- unexpected word count!"));
        dwRet = SMB_ERR(ERRSRV, ERRerror);
        goto Done;
    }   
    
    //
    // Find our connection state        
    if(NULL == (pMyConnection = SMB_Globals::g_pConnectionManager->FindConnection(pSMB))) {
       TRACEMSG(ZONE_ERROR, (L"SMBSRV: SMB_INFO_ALLOCATION: -- cant find connection 0x%x!", pSMB->ulConnectionID));
       ASSERT(FALSE);
       dwRet = SMB_ERR(ERRSRV, ERRerror);
       goto Done;    
    }
            
    //
    // Go get our TIDState information
    if(FAILED(pMyConnection->FindTIDState(_pRawRequest->pSMBHeader->Tid, &pMyTIDState, SEC_WRITE)) || NULL == pMyTIDState) {
        TRACEMSG(ZONE_ERROR, (L"SMBSRV:  SMB_Com_SetInformation -- couldnt find Tid info for %d!", _pRawRequest->pSMBHeader->Tid));
        dwRet = SMB_ERR(ERRSRV, ERRerror);
        goto Done;
    }  
    //
    // build a tokenizer to get the filename
    pSMBparam = (BYTE *)(pRequest + 1);
    RequestTokenizer.Reset(pSMBparam, pRequest->ByteCount);                 
                
    //
    // See what we really need to set
    if(0 == pRequest->ModifyDate && 0 == pRequest->ModifyTime) {
        pWriteTime = NULL;
    } else if(0 == DosDateTimeToFileTime(pWriteTime, pRequest->ModifyDate, pRequest->ModifyTime)) {
        pWriteTime = NULL;        
    }
    
    //
    // Parse out the file attributes
    Win32Attributes = DosAttributeToWin32(pRequest->FileAttributes);
    
    //
    // Add the Dir+FileName       
    FileName.append(pMyTIDState->GetShare()->GetDirectoryName());

    
    if(TRUE == pMyConnection->SupportsUnicode(pSMB->pInSMB)) {
        WCHAR *pFileName = NULL;
        
        if(FAILED(RequestTokenizer.GetUnicodeString(&pFileName))) {
            TRACEMSG(ZONE_ERROR, (L"SMBSRV:  SMB_Com_SetInformation -- string is of wrong time!!"));
            dwRet = SMB_ERR(ERRSRV, ERRerror);
            ASSERT(FALSE);
            goto Done;
        }
        FileName.append(pFileName);
        
    } else {
        CHAR *pFileName = NULL;
       
        if(FAILED(RequestTokenizer.GetString(&pFileName)) || 0x04 != *pFileName) {
            TRACEMSG(ZONE_ERROR, (L"SMBSRV:  SMB_Com_SetInformation -- string is of wrong time!!"));
            dwRet = SMB_ERR(ERRSRV, ERRerror);
            ASSERT(FALSE);
            goto Done;
        }
        FileName.append(pFileName+1);
    }  
    
    //
    // Check to see if the string is a file or directory
    if(FAILED(IsDirectory(FileName.GetString(), &fIsDir))) {
        TRACEMSG(ZONE_ERROR, (L"SMBSRV:  SMB_Com_SetInformation -- file doesnt exit GetLastError(%d)!", FileName.GetString(), GetLastError()));
        dwRet = SMB_ERR(ERRSRV, ERRbadpath);
        goto Done;                       
    }
    
    //
    // Setting all the information is a two part process first open the file
    //    and SetFileTime, then close and do a SetFileAttributes
    if(FALSE == fIsDir && NULL != pWriteTime) {
        if(INVALID_HANDLE_VALUE == (h = CreateFile(FileName.GetString(),                                                    
                                                   GENERIC_WRITE, 
                                                   FILE_SHARE_READ | FILE_SHARE_WRITE,
                                                   NULL,
                                                   OPEN_EXISTING, 
                                                   FILE_ATTRIBUTE_NORMAL, 
                                                   NULL))) {
            TRACEMSG(ZONE_ERROR, (L"SMBSRV:  SMB_Com_SetInformation -- couldnt open up %s GetLastError(%d)!", FileName.GetString(), GetLastError()));
            dwRet = SMB_ERR(ERRSRV, ERRbadpath);
            goto Done;                               
        }
        
        //
        // Set the file time
        FILETIME UTCft;
        if(0 == ::FileTimeToLocalFileTime(pWriteTime, &UTCft)) {
               TRACEMSG(ZONE_ERROR, (L"SMBSRV:  SMB_Com_SetInformation -- couldnt SetFileTime for %s! GetLastError(%d)", FileName.GetString(), GetLastError()));
               ASSERT(FALSE);
               dwRet = SMB_ERR(ERRSRV, ERRbadpath);
               goto Done;   
        }
        if(0 == ::SetFileTime(h, NULL, NULL, &UTCft)) {
               TRACEMSG(ZONE_ERROR, (L"SMBSRV:  SMB_Com_SetInformation -- couldnt SetFileTime for %s! GetLastError(%d)", FileName.GetString(), GetLastError()));
               dwRet = SMB_ERR(ERRSRV, ERRbadpath);
               goto Done;   
        }
        
        //
        // Close up
        CloseHandle(h);
        h = INVALID_HANDLE_VALUE;
    }
    ASSERT(INVALID_HANDLE_VALUE == h);
    
    //
    // Finally do the SetFileAttributes
    if(0 == SetFileAttributes(FileName.GetString(), Win32Attributes)) {
        TRACEMSG(ZONE_ERROR, (L"SMBSRV:  SMB_Com_SetInformation -- couldnt SetFileAttributes for %s! GetLastError(%d)", FileName.GetString(),GetLastError()));
        dwRet = SMB_ERR(ERRSRV, ERRbadpath);
        goto Done;    
    }
  
    //
    // Success
    dwRet = 0;
    Done:
        if(INVALID_HANDLE_VALUE != h) {
            CloseHandle(h);
        }
        
        pResponse->WordCount = 0; 
        pResponse->ByteCount = 0;
        *puiUsed = sizeof(SMB_COM_GENERIC_RESPONSE);
        return dwRet;
}


DWORD SMB_FILE::SMB_Com_SetInformation2(SMB_PROCESS_CMD *_pRawRequest, 
                                      SMB_PROCESS_CMD *_pRawResponse, 
                                      UINT *puiUsed,
                                      SMB_PACKET *pSMB)
{
    DWORD dwRet = 0;
    *puiUsed = 0;
    
    TIDState *pMyTIDState = NULL;
    ActiveConnection *pMyConnection = NULL;
    SMB_SET_EXTENDED_ATTRIBUTE_CLIENT_REQUEST *pRequest = (SMB_SET_EXTENDED_ATTRIBUTE_CLIENT_REQUEST *)_pRawRequest->pDataPortion;
    SMB_COM_GENERIC_RESPONSE *pResponse = (SMB_COM_GENERIC_RESPONSE *)_pRawResponse->pDataPortion;  
   
    FILETIME CreationTime;
    FILETIME AccessTime;
    FILETIME WriteTime;
    FILETIME UTCCreationTime;
    FILETIME UTCAccessTime;
    FILETIME UTCWriteTime;
    FILETIME *pCreationTime = &UTCCreationTime;
    FILETIME *pAccessTime =  &UTCAccessTime;
    FILETIME *pWriteTime = &UTCWriteTime;
    
    //
    // Verify incoming params
    if(_pRawRequest->uiDataSize < sizeof(SMB_SET_EXTENDED_ATTRIBUTE_CLIENT_REQUEST)) {
        TRACEMSG(ZONE_ERROR, (L"SMBSRV:  SMB_Com_SetInformation2 has a string larger than packet!! -- someone is tricking us?"));
        dwRet = SMB_ERR(ERRSRV, ERRerror);
        goto Done;
    }
    if(7 != pRequest->WordCount) {
        TRACEMSG(ZONE_ERROR, (L"SMBSRV:  SMB_Com_SetInformation2 -- unexpected word count!"));
        dwRet = SMB_ERR(ERRSRV, ERRerror);
        goto Done;
    }
    
    //
    // Find our connection state        
    if(NULL == (pMyConnection = SMB_Globals::g_pConnectionManager->FindConnection(pSMB))) {
       TRACEMSG(ZONE_ERROR, (L"SMBSRV: SMB_INFO_ALLOCATION: -- cant find connection 0x%x!", pSMB->ulConnectionID));
       ASSERT(FALSE);
       dwRet = SMB_ERR(ERRSRV, ERRerror);
       goto Done;    
    }
    
    //
    // Go get our TIDState information
    if(FAILED(pMyConnection->FindTIDState(_pRawRequest->pSMBHeader->Tid, &pMyTIDState, SEC_WRITE)) ||
       NULL == pMyTIDState) {
        TRACEMSG(ZONE_ERROR, (L"SMBSRV:  SMB_Com_SetInformation2 -- couldnt find Tid info for %d!", _pRawRequest->pSMBHeader->Tid));
        dwRet = SMB_ERR(ERRSRV, ERRerror);
        goto Done;
    }  
    
    //
    // See what we really need to set, and do localtime to filetime conversions
    if(0 == pRequest->CreationDate && 0 == pRequest->CreationTime) {
        pCreationTime = NULL;
    } else {
        if(0 == DosDateTimeToFileTime(&CreationTime, pRequest->CreationDate, pRequest->CreationTime)) {
            pCreationTime = NULL;
        }else if(0 == LocalFileTimeToFileTime(&CreationTime, pCreationTime)) {
            pCreationTime = NULL;
        }
    }    
    if(0 == pRequest->AccessDate && 0 == pRequest->AccessTime) {
        pAccessTime = NULL;
    } else {
        if(0 == DosDateTimeToFileTime(&AccessTime, pRequest->AccessDate, pRequest->AccessTime)) {
            pAccessTime = NULL;
        } else if(0 == LocalFileTimeToFileTime(&AccessTime, pAccessTime)) {
            pAccessTime = NULL;
        }        
    }  
    if(0 == pRequest->ModifyDate && 0 == pRequest->ModifyTime) {
        pWriteTime = NULL;
    } else {
        if(0 == DosDateTimeToFileTime(&WriteTime, pRequest->ModifyDate, pRequest->ModifyTime)) {
            pWriteTime = NULL;
        } else if(0 == LocalFileTimeToFileTime(&WriteTime, pWriteTime)) {
            pWriteTime = NULL;
        }
    }
    
    //
    // Set the file attributes
    if(FAILED(pMyTIDState->SetFileTime(pRequest->FileID, pCreationTime, pAccessTime, pWriteTime))) {
         TRACEMSG(ZONE_ERROR, (L"SMBSRV-CRACKER: SMB_Com_SetInformation2 -- couldnt set file time!!"));
        dwRet = SMB_ERR(ERRDOS, ERRbadfile);
        goto Done;   
    }
       
    Done:
        pResponse->WordCount = 0; 
        pResponse->ByteCount = 0;
        *puiUsed = sizeof(SMB_COM_GENERIC_RESPONSE);
        return dwRet;
}



DWORD 
SMB_FILE::SMB_Com_Flush(SMB_PROCESS_CMD *_pRawRequest, 
                 SMB_PROCESS_CMD *_pRawResponse, 
                 UINT *puiUsed,
                 SMB_PACKET *pSMB)
{
    DWORD dwRet = 0;
    *puiUsed = 0;
    
    TIDState *pMyTIDState = NULL;
    ActiveConnection *pMyConnection = NULL;
    SMB_FILE_FLUSH_CLIENT_REQUEST *pRequest = (SMB_FILE_FLUSH_CLIENT_REQUEST *)_pRawRequest->pDataPortion;
    SMB_COM_GENERIC_RESPONSE *pResponse = (SMB_COM_GENERIC_RESPONSE *)_pRawResponse->pDataPortion;   

    //
    // Verify incoming params
    if(_pRawRequest->uiDataSize < sizeof(SMB_FILE_FLUSH_CLIENT_REQUEST)) {
        TRACEMSG(ZONE_ERROR, (L"SMBSRV:  SMB_Com_Flush has a string larger than packet!! -- someone is tricking us?"));
        dwRet = SMB_ERR(ERRSRV, ERRerror);
        goto Done;
    }
    if(1 != pRequest->WordCount) {
        TRACEMSG(ZONE_ERROR, (L"SMBSRV:  SMB_Com_Flush -- unexpected word count!"));
        dwRet = SMB_ERR(ERRSRV, ERRerror);
        goto Done;
    }
    
    
    //
    // Find our connection state        
    if(NULL == (pMyConnection = SMB_Globals::g_pConnectionManager->FindConnection(pSMB))) {
       TRACEMSG(ZONE_ERROR, (L"SMBSRV: SMB_INFO_ALLOCATION: -- cant find connection 0x%x!", pSMB->ulConnectionID));
       ASSERT(FALSE);
       dwRet = SMB_ERR(ERRSRV, ERRerror);
       goto Done;    
    }
    
    //
    // Go get our TIDState information
    if(FAILED(pMyConnection->FindTIDState(_pRawRequest->pSMBHeader->Tid, &pMyTIDState, 0)) ||
       NULL == pMyTIDState) {
        TRACEMSG(ZONE_ERROR, (L"SMBSRV:  SMB_Com_Flush -- couldnt find Tid info for %d!", _pRawRequest->pSMBHeader->Tid));
        dwRet = SMB_ERR(ERRSRV, ERRerror);
        goto Done;
    }
    
    //
    // Perform the operation
    if(FAILED(pMyTIDState->Flush(pRequest->FileID))) {
        TRACEMSG(ZONE_ERROR, (L"SMBSRV:  SMB_Com_Flush -- Flush() on TID:%d failed!", pRequest->FileID));
        dwRet = SMB_ERR(ERRSRV, ERRerror);
        goto Done;
    }
              
    Done:
        pResponse->WordCount = 0; 
        pResponse->ByteCount = 0;
        *puiUsed = sizeof(SMB_COM_GENERIC_RESPONSE);
        return dwRet;
}


DWORD 
SMB_FILE::SMB_Com_CheckPath(SMB_PROCESS_CMD *_pRawRequest, 
                                     SMB_PROCESS_CMD *_pRawResponse, 
                                     UINT *puiUsed,
                                     SMB_PACKET *pSMB)
{
    DWORD dwRet = 0;
    *puiUsed = 0;

    ActiveConnection *pMyConnection = NULL;
    SMB_CHECKPATH_CLIENT_REQUEST *pRequest = (SMB_CHECKPATH_CLIENT_REQUEST *)_pRawRequest->pDataPortion;
    SMB_CHECKPATH_SERVER_RESPONSE *pResponse = (SMB_CHECKPATH_SERVER_RESPONSE *)_pRawResponse->pDataPortion;
    TIDState *pMyTIDState = NULL;
    ULONG ulOffset = 0;
    Share *pMyShare = NULL;
    StringConverter FullPath;
    StringTokenizer RequestTokenizer;
    BYTE *pSMBparam = NULL;
    
    //
    // Verify incoming params
    if(_pRawRequest->uiDataSize < sizeof(SMB_CHECKPATH_CLIENT_REQUEST)) {
        TRACEMSG(ZONE_ERROR, (L"SMBSRV:  SMB_Com_CheckPath has a string larger than packet!! -- someone is tricking us?"));
        dwRet = SMB_ERR(ERRSRV, ERRerror);
        goto Done;
    }
    if(0 != pRequest->WordCount) {
        TRACEMSG(ZONE_ERROR, (L"SMBSRV:  SMB_Com_CheckPath -- unexpected word count!"));
        dwRet = SMB_ERR(ERRSRV, ERRerror);
        goto Done;
    }    
       
    //
    // Find our connection state        
    if(NULL == (pMyConnection = SMB_Globals::g_pConnectionManager->FindConnection(pSMB))) {
       TRACEMSG(ZONE_ERROR, (L"SMBSRV: SMB_Com_CheckPath: -- cant find connection 0x%x!", pSMB->ulConnectionID));
       ASSERT(FALSE);
       dwRet = SMB_ERR(ERRSRV, ERRerror);
       goto Done;    
    }
    
    //
    // Go get our TIDState information
    if(FAILED(pMyConnection->FindTIDState(_pRawRequest->pSMBHeader->Tid, &pMyTIDState, 0)) ||
       NULL == pMyTIDState) {
        TRACEMSG(ZONE_ERROR, (L"SMBSRV:  SMB_Com_CheckPath -- couldnt find Tid info for %d!", _pRawRequest->pSMBHeader->Tid));
        dwRet = SMB_ERR(ERRSRV, ERRerror);
        goto Done;
    }

    //
    // Get the share state
    if(NULL == (pMyShare = pMyTIDState->GetShare())) {
        ASSERT(FALSE);
        TRACEMSG(ZONE_ERROR, (L"SMBSRV:  SMB_Com_CheckPath -- couldnt find share state info for %d!", _pRawRequest->pSMBHeader->Tid));
        dwRet = SMB_ERR(ERRSRV, ERRerror);
        goto Done;
    }

    //
    // Get the directory they are interested in
    pSMBparam = (BYTE *)(pRequest + 1);
    RequestTokenizer.Reset(pSMBparam, pRequest->ByteCount);
    FullPath.append(pMyShare->GetDirectoryName());
    if(TRUE == pMyConnection->SupportsUnicode(pSMB->pInSMB)) {
        WCHAR *pDirectoryName = NULL;
            
        if(FAILED(RequestTokenizer.GetUnicodeString(&pDirectoryName))) {
            TRACEMSG(ZONE_ERROR, (L"SMBSRV-CRACKER: SMB_Com_CheckPath -- couldnt find filename!!"));
            dwRet = SMB_ERR(ERRSRV, ERRerror);
            goto Done;   
        }              
        FullPath.append(pDirectoryName);        
    } else {
        CHAR *pDirectoryName = NULL;
        
        if(FAILED(RequestTokenizer.GetString(&pDirectoryName))) {
            TRACEMSG(ZONE_ERROR, (L"SMBSRV-CRACKER: SMB_Com_CheckPath -- couldnt find filename!!"));
            dwRet = SMB_ERR(ERRSRV, ERRerror);
            goto Done;   
        }
        
        //
        // Check that we have the right string type
        if(0x04 != *pDirectoryName) {
            TRACEMSG(ZONE_ERROR, (L"SMBSRV-CRACKER:  SMB_Com_CheckPath -- invalid string type!"));
            dwRet = SMB_ERR(ERRSRV, ERRerror);
            goto Done;
        }
        
        //
        // Advance beyond the 0x04 header
        pDirectoryName ++;  
        
        FullPath.append(pDirectoryName);
    }

    //
    // See if it exists (and is safe with the share)
    if(FAILED(pMyShare->IsValidPath(FullPath.GetString()))) {
        TRACEMSG(ZONE_ERROR, (L"SMBSRV-CRACKER:  SMB_Com_CheckPath -- invalid string %s -- not safe!", FullPath.GetString()));
        dwRet = SMB_ERR(ERRDOS, ERRbadpath);
        goto Done;
    }

    BOOL fStatus;
    if(FAILED(IsDirectory(FullPath.GetString(), &fStatus)) || FALSE == fStatus) {
        dwRet = SMB_ERR(ERRDOS, ERRbadpath);
        goto Done;
    }

    //
    // Success
    dwRet = 0;
   
    Done:
        *puiUsed = sizeof(SMB_CHECKPATH_SERVER_RESPONSE);
        return dwRet;
}


DWORD 
SMB_FILE::SMB_Com_Search(SMB_PROCESS_CMD *_pRawRequest, 
                                 SMB_PROCESS_CMD *_pRawResponse, 
                                 UINT *puiUsed,
                                 SMB_PACKET *pSMB)
{
    DWORD dwRet = 0;
    *puiUsed = 0;

    ActiveConnection *pMyConnection = NULL;
    SMB_COM_SEARCH_RESPONSE *pResponse = (SMB_COM_SEARCH_RESPONSE *)_pRawResponse->pDataPortion;
       
    //
    // Find our connection state        
    if(NULL == (pMyConnection = SMB_Globals::g_pConnectionManager->FindConnection(pSMB))) {
       TRACEMSG(ZONE_ERROR, (L"SMBSRV: SMB_Com_Search: -- cant find connection 0x%x!", pSMB->ulConnectionID));
       ASSERT(FALSE);
       dwRet = SMB_ERR(ERRSRV, ERRerror);
       goto Done;    
    }
    TRACEMSG(ZONE_ERROR, (L"SMB_SRV:  SEARCH not supported b/c WinCE doesnt have 8.3 fileformat! sending no files back!"));
    dwRet = 0;
    *puiUsed = sizeof(SMB_COM_SEARCH_RESPONSE);

    pResponse->WordCount = 1;
    pResponse->Count = 0;
    pResponse->ByteCount = 3;   
    pResponse->BufferFormat = 0x05;
    pResponse->DataLength = 0;
    Done:
        return dwRet;
}

DWORD 
SMB_FILE::SMB_Com_Seek(SMB_PROCESS_CMD *_pRawRequest, 
                 SMB_PROCESS_CMD *_pRawResponse, 
                 UINT *puiUsed,
                 SMB_PACKET *pSMB)
{
    DWORD dwRet = 0;
    *puiUsed = 0;
    
    TIDState *pMyTIDState = NULL;
    ActiveConnection *pMyConnection = NULL;
    SMB_SEEK_CLIENT_REQUEST *pRequest = (SMB_SEEK_CLIENT_REQUEST *)_pRawRequest->pDataPortion;
    SMB_SEEK_SERVER_RESPONSE *pResponse = (SMB_SEEK_SERVER_RESPONSE *)_pRawResponse->pDataPortion;
    
    ULONG ulOffset = 0;
    //
    // Verify incoming params
    if(_pRawRequest->uiDataSize < sizeof(SMB_SEEK_CLIENT_REQUEST)) {
        TRACEMSG(ZONE_ERROR, (L"SMBSRV:  SMB_Com_Seek has a string larger than packet!! -- someone is tricking us?"));
        dwRet = SMB_ERR(ERRSRV, ERRerror);
        goto Done;
    }
    if(4 != pRequest->WordCount) {
        TRACEMSG(ZONE_ERROR, (L"SMBSRV:  SMB_Com_Seek -- unexpected word count!"));
        dwRet = SMB_ERR(ERRSRV, ERRerror);
        goto Done;
    }    
    
    //
    // Find our connection state        
    if(NULL == (pMyConnection = SMB_Globals::g_pConnectionManager->FindConnection(pSMB))) {
       TRACEMSG(ZONE_ERROR, (L"SMBSRV: SMB_Com_Seek: -- cant find connection 0x%x!", pSMB->ulConnectionID));
       ASSERT(FALSE);
       dwRet = SMB_ERR(ERRSRV, ERRerror);
       goto Done;    
    }
    
    //
    // Go get our TIDState information
    if(FAILED(pMyConnection->FindTIDState(_pRawRequest->pSMBHeader->Tid, &pMyTIDState, 0)) ||
       NULL == pMyTIDState) {
        TRACEMSG(ZONE_ERROR, (L"SMBSRV:  SMB_Com_Seek -- couldnt find Tid info for %d!", _pRawRequest->pSMBHeader->Tid));
        dwRet = SMB_ERR(ERRSRV, ERRerror);
        goto Done;
    }
    
    //
    // Perform the operation    
    if(FAILED(pMyTIDState->SetFilePointer(pRequest->FileID, pRequest->Offset, pRequest->Mode, &ulOffset))) {
        TRACEMSG(ZONE_ERROR, (L"SMBSRV:  SMB_Com_Seek -- Flush() on TID:%d failed!", pRequest->FileID));
        dwRet = SMB_ERR(ERRSRV, ERRerror);
        goto Done;
    }
    
    //
    // SUCCESS
    dwRet = 0;
              
    Done:
        pResponse->WordCount = (sizeof(SMB_SEEK_SERVER_RESPONSE) - 3) / sizeof(WORD); 
        pResponse->Offset = ulOffset;
        pResponse->ByteCount = 0;
        *puiUsed = sizeof(SMB_SEEK_SERVER_RESPONSE);
        ASSERT(2 == pResponse->WordCount);
        return dwRet;
}


DWORD SMB_FILE::SMB_Com_Query_EX_Information(SMB_PACKET *pSMB,
                                            SMB_PROCESS_CMD *_pRawRequest, 
                                            SMB_PROCESS_CMD *_pRawResponse, 
                                            UINT *puiUsed)
{
    DWORD dwRet = 0;
    *puiUsed = 0;
    
    TIDState *pMyTIDState = NULL;
    SMB_COM_QUERY_EXINFO_REQUEST *pRequest = (SMB_COM_QUERY_EXINFO_REQUEST *)_pRawRequest->pDataPortion;
    SMB_COM_QUERY_EXINFO_RESPONSE *pResponse = (SMB_COM_QUERY_EXINFO_RESPONSE *)_pRawResponse->pDataPortion;   
    ActiveConnection *pMyConnection = NULL;
    HRESULT hr = E_FAIL;  
    WIN32_FIND_DATA fd;
    WORD attributes;

    //
    // Verify incoming params
    if(_pRawRequest->uiDataSize < sizeof(SMB_COM_QUERY_EXINFO_REQUEST)) {
        TRACEMSG(ZONE_ERROR, (L"SMBSRV:  COM_QUERY_EX_INFO has a string larger than packet!! -- someone is tricking us?"));
        dwRet = SMB_ERR(ERRSRV, ERRerror);
        goto Done;
    }
    if(1 != pRequest->WordCount) {
        TRACEMSG(ZONE_ERROR, (L"SMBSRV:  COM_QUERY_EX_INFO -- unexpected word count!"));
        dwRet = SMB_ERR(ERRSRV, ERRerror);
        goto Done;
    }
    
    //
    // Find our connection state        
    if(NULL == (pMyConnection = SMB_Globals::g_pConnectionManager->FindConnection(pSMB))) {
       TRACEMSG(ZONE_ERROR, (L"SMBSRV: SMB_Com_Seek: -- cant find connection 0x%x!", pSMB->ulConnectionID));
       ASSERT(FALSE);
       dwRet = SMB_ERR(ERRSRV, ERRerror);
       goto Done;    
    }
    
    //
    // Go get our TIDState information
    if(FAILED(pMyConnection->FindTIDState(_pRawRequest->pSMBHeader->Tid, &pMyTIDState, 0)) ||
       NULL == pMyTIDState) {
        TRACEMSG(ZONE_ERROR, (L"SMBSRV:  SMB_Com_Seek -- couldnt find Tid info for %d!", _pRawRequest->pSMBHeader->Tid));
        dwRet = SMB_ERR(ERRSRV, ERRerror);
        goto Done;
    }          
       
    //
    // Seek out the FID
    if(FAILED(pMyTIDState->QueryFileInformation(pRequest->FID, &fd))) {
        TRACEMSG(ZONE_FILES, (L"SMBSRV:  COM_QUERY_EX_INFO: couldnt QueryFileInformation with FID: 0x%x \n", pRequest->FID));     
        dwRet = SMB_ERR(ERRDOS, ERRbadfile);
        goto BadFile;        
    }
        
    attributes = Win32AttributeToDos(fd.dwFileAttributes);

    FILETIME UTCCreationTime;
    FILETIME UTCWriteTime;
    FILETIME UTCAccessTime;

    if(0 == FileTimeToLocalFileTime(&fd.ftCreationTime, &UTCCreationTime) ||
       0 == FileTimeToLocalFileTime(&fd.ftLastWriteTime, &UTCWriteTime) ||
       0 == FileTimeToLocalFileTime(&fd.ftLastAccessTime, &UTCAccessTime))
    {
        ASSERT(FALSE);
        dwRet = SMB_ERR(ERRDOS, ERRbadfile);
        goto BadFile;
    }
    if(FAILED(FileTimeToSMBTime(&UTCCreationTime,  &pResponse->Fields.CreateTime, &pResponse->Fields.CreateDate)) ||
       FAILED(FileTimeToSMBTime(&UTCWriteTime,     &pResponse->Fields.ModifyTime, &pResponse->Fields.ModifyDate)) ||
       FAILED(FileTimeToSMBTime(&UTCAccessTime,    &pResponse->Fields.AccessTime, &pResponse->Fields.AccessDate))) {
        ASSERT(FALSE);
        dwRet = SMB_ERR(ERRDOS, ERRbadfile);
        goto BadFile;
    }
    
    
    //WORDCOUNT = response struct - 3 ( for 'bytecount' and the actual word count byte) / size of a word
    pResponse->Fields.WordCount = (sizeof(SMB_COM_QUERY_EXINFO_RESPONSE)-3)/sizeof(WORD);    
    pResponse->Fields.FileSize = fd.nFileSizeLow;
    pResponse->Fields.Allocation = 0;
    pResponse->Fields.Attributes = attributes;
    pResponse->ByteCount = 0;
    *puiUsed = sizeof(SMB_COM_QUERY_EXINFO_RESPONSE);    
    
    goto Done;
    BadFile:
        {
            BYTE *pTmp = (BYTE *)pResponse;
            pTmp[0] = 0;
            pTmp[1] = 0;
            pTmp[2] = 0;        
            *puiUsed = 3;
        }
    
    Done:
        return dwRet;
}




FileStream::FileStream(TIDState *pMyState) : SMBFileStream(FILE_STREAM, pMyState)
{
    USHORT usJobID; 

    //
    // get a Unique ID and set it   
    if(FAILED(SMB_Globals::g_pUniqueFID->GetID(&usJobID))) {   
      ASSERT(FALSE);
      usJobID = 0xFFFF;        
    }
    ASSERT(usJobID != 0xFFFF);
    ASSERT(NULL != SMB_Globals::g_pAbstractFileSystem);    
    this->SetFID(usJobID);
    m_fOpened = FALSE;  
}

FileStream::~FileStream()
{    
    ASSERT(0xFFFF != this->FID());
    ASSERT(NULL != SMB_Globals::g_pAbstractFileSystem);
    
    //
    // If our file hasnt been closed, close it now
    if(m_fOpened) {
        TRACEMSG(ZONE_FILES, (L"SMB-FILESTREAM: Closing up file because the TID is going away and Close() wasnt called!"));
        this->Close();
    }
    
    if(0xFFFF != this->FID()) {
        if(FAILED(SMB_Globals::g_pUniqueFID->RemoveID(this->FID()))) {
            ASSERT(FALSE);
        }
    } 
}

BOOL
FileStream::CanRead() {
    return (m_dwAccess & GENERIC_READ)?TRUE:FALSE;
}

BOOL
FileStream::CanWrite() {
    return (m_dwAccess & GENERIC_WRITE)?TRUE:FALSE;
}

HRESULT 
FileStream::Read(BYTE *pDest, DWORD dwOffset, DWORD dwReqSize, DWORD *pdwRead)
{  
    ASSERT(NULL != SMB_Globals::g_pAbstractFileSystem);
    FileObject *pObject = NULL;
    HRESULT hr = E_FAIL;
    
    if(FALSE == CanRead()) {
        return E_ACCESSDENIED;
    }
    if(FAILED(SMB_Globals::g_pAbstractFileSystem->FindFile(this->FID(), &pObject))) {
        TRACEMSG(ZONE_FILES, (L"SMB-FILESTREAM: couldnt find file with FID:0x%x on read\n", this->FID()));
        goto Done;
    }
    if(NULL == pObject || FAILED(hr = pObject->Read(this->FID(), pDest, dwOffset, dwReqSize, pdwRead))) {
        TRACEMSG(ZONE_FILES, (L"SMB-FILESTREAM: couldnt Read() to  file with FID:0x%x\n", this->FID()));
        goto Done;
    }   
      
    hr = S_OK;
    
    //
    // Update our current offset
    m_dwCurrentOffset = dwOffset + *pdwRead;    
    Done:
        return hr;
}

HRESULT 
FileStream::Write(BYTE *pSrc, DWORD dwOffset, DWORD dwReqSize, DWORD *pdwWritten)
{    
    ASSERT(NULL != SMB_Globals::g_pAbstractFileSystem);
    FileObject *pObject = NULL;
    HRESULT hr = E_FAIL;
    
    if(FALSE == CanWrite()) {
        return E_ACCESSDENIED;
    }    
    if(FAILED(SMB_Globals::g_pAbstractFileSystem->FindFile(this->FID(), &pObject))) {
        TRACEMSG(ZONE_FILES, (L"SMB-FILESTREAM: couldnt find file with FID:0x%x\n", this->FID()));
        goto Done;
    }
    if(NULL == pObject || FAILED(hr = pObject->Write(this->FID(), pSrc, dwOffset, dwReqSize, pdwWritten))) {
        TRACEMSG(ZONE_FILES, (L"SMB-FILESTREAM: couldnt Write() to  file with FID:0x%x\n", this->FID()));
        goto Done;
    }
    
    hr = S_OK;
    
    //
    // Update our current offset
    m_dwCurrentOffset = dwOffset + *pdwWritten;
    Done:
        return hr;
}

        
HRESULT 
FileStream::Open(const WCHAR *pFileName, 
                 DWORD dwAccess, 
                 DWORD dwDisposition,
                 DWORD dwAttributes, 
                 DWORD dwShareMode,
                 DWORD *pdwActionTaken)
{
    HRESULT hr = E_FAIL;   
    SMBPrintQueue *pPrintQueue = NULL;
    StringConverter FullPath;
    TIDState *pMyTIDState;
    FileObject *pFile = NULL;
    BOOL fIsDirectory = FALSE;
    
    //
    // Make sure this stream hasnt already been opened
    if(m_fOpened) {
        ASSERT(FALSE);
        hr = E_UNEXPECTED;
        goto Done;
    }
    
    //
    // Get our TIDState so we can start opening the file
    if(NULL == (pMyTIDState = this->GetTIDState())) {
        ASSERT(FALSE);
        TRACEMSG(ZONE_ERROR, (L"SMBSRV-CRACKER: SMB_OpenX -- cant get TID STATE!!!!"));
        hr = E_UNEXPECTED;
        goto Done;
    }
    
    FullPath.append(pMyTIDState->GetShare()->GetDirectoryName());
    FullPath.append(pFileName);
   
    TRACEMSG(ZONE_FILES, (L"SMBSRV-FILE: OpenX for %s", FullPath.GetString()));
   
    //
    // make sure the request doesnt have any unexpected characters
    if(FAILED(pMyTIDState->GetShare()->IsValidPath(FullPath.GetString()))) {
        TRACEMSG(ZONE_ERROR, (L"SMBSRV-CRACKER: SMB_OpenX -- error filename has invalid character!"));
        hr = E_ACCESSDENIED;
        goto Done;   
    }   
    
    //
    // Dont try to open a directory as a file
    if(FAILED(hr = IsDirectory(FullPath.GetString(), &fIsDirectory))) {
        TRACEMSG(ZONE_ERROR, (L"SMBSRV-CRACKER: SMB_OpenX -- error cant check file as directory!! %s", FullPath.GetString()));
        ASSERT(FALSE);
        hr = E_ACCESSDENIED;
        goto Done;
    }    
    if(TRUE == fIsDirectory) {
        hr = E_DOS_ERROR_ACCESS_DENIED;
        goto Done;
    }
    
    //
    // Open up the file
    if(FAILED(hr = SMB_Globals::g_pAbstractFileSystem->Open(FullPath.GetString(), 
                                                       this->FID(),
                                                       &pFile,
                                                       dwAccess,
                                                       dwDisposition,
                                                       dwAttributes,
                                                       dwShareMode,
                                                       pdwActionTaken)))
    {
        TRACEMSG(ZONE_FILES, (L"SMBSRV-CRACKER: SMB_OpenX -- error -- cant open up from abstract file system!"));
        goto Done;   
    }
    m_fOpened = TRUE;
    m_dwCurrentOffset = 0;
    m_dwAccess = dwAccess;
    hr = S_OK;
    Done:
        return hr;
}
   
   
   
HRESULT 
FileStream::SetEndOfStream(DWORD dwOffset)
{
    ASSERT(NULL != SMB_Globals::g_pAbstractFileSystem);
    FileObject *pObject = NULL;
    HRESULT hr = E_FAIL;
    
    if(FALSE == CanWrite() && FALSE == CanRead()) {
        return E_ACCESSDENIED;
    }
    if(FAILED(SMB_Globals::g_pAbstractFileSystem->FindFile(this->FID(), &pObject))) {
        TRACEMSG(ZONE_FILES, (L"SMBSRVR: FileStream -- couldnt find file with FID:0x%x", this->FID()));
        goto Done;
    }
    if(NULL == pObject || FAILED(pObject->SetEndOfStream(dwOffset))) {
        TRACEMSG(ZONE_FILES, (L"SMBSRVR: FileStream -- couldnt set endof stream for file with FID:0x%x", this->FID()));
        goto Done;
    }
    
    hr = S_OK;
    
    //
    // Update our current offset
    m_dwCurrentOffset = dwOffset;
    
    Done:
        return hr;
}
  
  
//
// NOTE: this API is totally stupid -- its mainly used as an old way of figuring out
//   how big a file is.  we wont hack up our FileObject just to support it
//   (no file operations depend on it...)  
HRESULT 
FileStream::SetFilePointer(LONG lDistance, DWORD dwMoveMethod, ULONG *pOffset)
{   
    ASSERT(NULL != SMB_Globals::g_pAbstractFileSystem);
    FileObject *pObject = NULL;
    HRESULT hr = E_FAIL;
    DWORD dwFileSize = 0;    
    DWORD dwNewOffset = 0;
    
    *pOffset = 0;
    
    if(FALSE == CanWrite() && FALSE == CanRead()) {
        return E_ACCESSDENIED;
    }
    
    //
    // Find our file
    if(FAILED(SMB_Globals::g_pAbstractFileSystem->FindFile(this->FID(), &pObject))) {
        TRACEMSG(ZONE_FILES, (L"SMBSRVR: FileStream -- couldnt find file with FID:0x%x", this->FID()));
        goto Done;
    }
    
    // 
    // Figure out our old file size_t
    if(FAILED(hr = pObject->GetFileSize(&dwFileSize))) {
        goto Done;
    }    
    
    //
    // Figure out the offset we would be at
    switch(dwMoveMethod) {
        case 0: // seek from start of file
            dwNewOffset = lDistance;
            break;
        case 1: // seek from current file pointer
            dwNewOffset = m_dwCurrentOffset + lDistance;
            break;
        case 2: // seek from end of file
            if(lDistance > 0 && (DWORD)lDistance > dwFileSize) {
                hr = E_INVALIDARG;
                goto Done;
            }
            dwNewOffset = dwFileSize - lDistance;
            break;
        default:
            ASSERT(FALSE);
            goto Done;
    }
    
    //
    // If after moving the offsets around we are larger than we used to
    //   be set the end of stream
    if(CanWrite () && m_dwCurrentOffset >= dwFileSize) {
        if(FAILED(hr = pObject->SetEndOfStream(m_dwCurrentOffset))) {
            goto Done;
        }
    }
    //
    // Update our offset since we are successful
    m_dwCurrentOffset = dwNewOffset;
    *pOffset = m_dwCurrentOffset;
    hr = S_OK;
    Done:
        return hr;   
}
  
HRESULT 
FileStream::SetFileTime(FILETIME *pCreation, 
                    FILETIME *pAccess, 
                    FILETIME *pWrite)
{   
    ASSERT(NULL != SMB_Globals::g_pAbstractFileSystem);
    FileObject *pObject = NULL;
    HRESULT hr = E_FAIL;
    
    if(FALSE == CanWrite()) {
        return E_ACCESSDENIED;
    }
    if(FAILED(SMB_Globals::g_pAbstractFileSystem->FindFile(this->FID(), &pObject))) {
        TRACEMSG(ZONE_FILES, (L"SMBSRVR: FileStream -- couldnt find file with FID:0x%x", this->FID()));
        goto Done;
    }
    if(NULL == pObject || FAILED(pObject->SetFileTime(pCreation, pAccess, pWrite))) {
        TRACEMSG(ZONE_FILES, (L"SMBSRVR: FileStream -- couldnt set file time for file with FID:0x%x", this->FID()));
        goto Done;
    }       
    hr = S_OK;
    Done:
        return hr;
}
   
HRESULT
FileStream::Close() {
    ASSERT(NULL != SMB_Globals::g_pAbstractFileSystem);   
    HRESULT hr = E_FAIL;
    
    
    //
    // Make sure we are really open
    if(FALSE == m_fOpened) {
        ASSERT(FALSE);
        goto Done;
    }
    
    //
    // Close up the file in the abstract filesystem
    TRACEMSG(ZONE_FILES, (L"SMBSRVR: FileServer -- closing up file with FID:0x%x", this->FID()));
    if(FAILED(SMB_Globals::g_pAbstractFileSystem->Close(this->FID()))) {
        goto Done;
    }
 
    m_fOpened = FALSE;
    
    //
    // Update our current offset
    m_dwCurrentOffset = 0;
    
    hr = S_OK;
    Done:
        return hr;  
}


HRESULT 
FileStream::GetFileSize(DWORD *pdwSize) {      
    ASSERT(NULL != SMB_Globals::g_pAbstractFileSystem);
    FileObject *pObject = NULL;
    HRESULT hr = E_FAIL;    
   
    if(FAILED(SMB_Globals::g_pAbstractFileSystem->FindFile(this->FID(), &pObject))) {
        goto Done;
    }
    hr = pObject->GetFileSize(pdwSize);
    TRACEMSG(ZONE_FILES, (L"SMBSRVR: FileServer -- GetFileSize on FID:0x%x", this->FID()));
    Done:
        return hr;
}


HRESULT 
FileStream::GetFileTime(LPFILETIME lpCreationTime, 
                       LPFILETIME lpLastAccessTime, 
                       LPFILETIME lpLastWriteTime)
{    
    ASSERT(NULL != SMB_Globals::g_pAbstractFileSystem);
    FileObject *pObject = NULL;
    HRESULT hr = E_FAIL;
    
    if(FAILED(SMB_Globals::g_pAbstractFileSystem->FindFile(this->FID(), &pObject))) {
       goto Done;
    }
    hr = pObject->GetFileTime(lpCreationTime, lpLastAccessTime, lpLastWriteTime);
    
    Done:
       if(FAILED(hr)) {
            TRACEMSG(ZONE_FILES, (L"SMBSRVR: FileServer -- GetFileTime on FID:0x%x failed", this->FID()));
       }
       return hr;
}


HRESULT 
FileStream::Flush() {
    ASSERT(NULL != SMB_Globals::g_pAbstractFileSystem);
    FileObject *pObject = NULL;
    HRESULT hr = E_FAIL;
    
    if(FAILED(SMB_Globals::g_pAbstractFileSystem->FindFile(this->FID(), &pObject))) {
        goto Done;
    }
    hr = pObject->Flush();
    TRACEMSG(ZONE_FILES, (L"SMBSRVR: FileServer -- Flush on FID:0x%x", this->FID()));
    Done:
        return hr;
}

HRESULT 
FileStream::QueryFileInformation(WIN32_FIND_DATA *fd)
{
    ASSERT(NULL != SMB_Globals::g_pAbstractFileSystem);
    FileObject *pObject = NULL;
    HRESULT hr = E_FAIL;
    HANDLE hFileHand = INVALID_HANDLE_VALUE;

    if(FAILED(SMB_Globals::g_pAbstractFileSystem->FindFile(this->FID(), &pObject))) {
        TRACEMSG(ZONE_ERROR, (L"SMBSRV:  COM_QUERY_EX_INFO -- cant find FID %d!!", this->FID()));
        goto Done;
    }   
    memset(fd, 0, sizeof(WIN32_FIND_DATA));
    if(INVALID_HANDLE_VALUE == (hFileHand = FindFirstFile(pObject->FileName(), fd))) {
        TRACEMSG(ZONE_ERROR, (L"SMBSRV:  COM_QUERY_EX_INFO -- getting attributes for %s failed (%d)!!", pObject->FileName(), GetLastError()));
        goto Done;
    }
    TRACEMSG(ZONE_FILES, (L"SMBSRVR: FileServer -- QueryFileInformation on FID:0x%x", this->FID()));
    
    //
    // Success
    hr = S_OK;
    Done:
        if(INVALID_HANDLE_VALUE != hFileHand) {
            FindClose(hFileHand);
        }                
        return hr;    
}




HRESULT 
FileStream::IsLocked(USHORT usPID,ULONG Offset,ULONG Length, BOOL *pfLocked)
{
    ASSERT(NULL != SMB_Globals::g_pAbstractFileSystem);
    FileObject *pObject = NULL;
    HRESULT hr = E_FAIL;

    if(FAILED(SMB_Globals::g_pAbstractFileSystem->FindFile(this->FID(), &pObject))) {
        TRACEMSG(ZONE_ERROR, (L"SMBSRV:  IsLocked -- cant find FID %d!!", this->FID()));
        goto Done;
    }   
    
    hr = pObject->IsLocked(usPID, Offset, Length, pfLocked);
   
    Done:                   
        return hr;    
}

HRESULT 
FileStream::Lock(USHORT usPID,ULONG Offset,ULONG Length)
{   
    ASSERT(NULL != SMB_Globals::g_pAbstractFileSystem);
    FileObject *pObject = NULL;
    HRESULT hr = E_FAIL;

    if(FAILED(SMB_Globals::g_pAbstractFileSystem->FindFile(this->FID(), &pObject))) {
        TRACEMSG(ZONE_ERROR, (L"SMBSRV:  IsLocked -- cant find FID %d!!", this->FID()));
        goto Done;
    }   
    
    hr = pObject->Lock(usPID, Offset, Length);
   
    Done:                   
        return hr;    
}

HRESULT 
FileStream::UnLock(USHORT usPID,ULONG Offset, ULONG Length)
{
    ASSERT(NULL != SMB_Globals::g_pAbstractFileSystem);
    FileObject *pObject = NULL;
    HRESULT hr = E_FAIL;
 
    if(FAILED(SMB_Globals::g_pAbstractFileSystem->FindFile(this->FID(), &pObject))) {
        TRACEMSG(ZONE_ERROR, (L"SMBSRV:  IsLocked -- cant find FID %d!!", this->FID()));
        goto Done;
    }   
    
    hr = pObject->UnLock(usPID, Offset, Length);
   
    Done:                   
        return hr;    

}




//
// Abstraction used for all file operations -- mainly here to implement range locking
FileObject::FileObject(USHORT usFID)
{
    m_usFID = usFID;
    m_hFile = INVALID_HANDLE_VALUE;
    m_LockNode = NULL;
}

FileObject::~FileObject()
{      
}

HRESULT
FileObject::Close()
{
    ASSERT(NULL != m_LockNode);
    
    m_LockNode->UnLockAll(m_usFID);
    
    if(INVALID_HANDLE_VALUE != m_hFile) {
            TRACEMSG(ZONE_FILES, (L"Closing %s FID:%d with handle 0x%x:", FileName(), m_usFID, (UINT)m_hFile));
            CloseHandle(m_hFile);
            m_hFile = INVALID_HANDLE_VALUE;
    }
    return S_OK;
}
HRESULT
FileObject::Open(const WCHAR *pFile,                 
                 DWORD _dwCreationDispostion,
                 DWORD _dwAttributes, 
                 DWORD _dwAccess,
                 DWORD _dwShareMode,
                 DWORD *_pdwActionTaken)
{   
    HRESULT hr = E_FAIL;
    
    TRACEMSG(ZONE_FILES, (L"Opening %s with following attributes:", pFile));
    
    //
    // Display all creation disposition stuff
#ifdef DEBUG
    if(CREATE_NEW == _dwCreationDispostion) {
        TRACEMSG(ZONE_FILES, (L"   CREATE_NEW"));
    } else if(CREATE_ALWAYS == _dwCreationDispostion) {
        TRACEMSG(ZONE_FILES, (L"   CREATE_ALWAYS"));
    } else if(OPEN_EXISTING == _dwCreationDispostion) {
        TRACEMSG(ZONE_FILES, (L"   OPEN_EXISTING"));
    } else if(OPEN_ALWAYS == _dwCreationDispostion) {
        TRACEMSG(ZONE_FILES, (L"   OPEN_ALWAYS"));
    } else if(TRUNCATE_EXISTING == _dwCreationDispostion) {
        TRACEMSG(ZONE_FILES, (L"   TRUNCATE_EXISTING"));
    }
       
    if(_dwAccess & GENERIC_READ) {        
        TRACEMSG(ZONE_FILES, (L"   GENERIC_READ"));
    }
    if(_dwAccess & GENERIC_WRITE) {        
        TRACEMSG(ZONE_FILES, (L"   GENERIC_WRITE"));
    }
    
    if(_dwAttributes & FILE_ATTRIBUTE_ARCHIVE) {       
        TRACEMSG(ZONE_FILES, (L"   FILE_ATTRIBUTE_ARCHIVE"));
    }
    if(_dwAttributes & FILE_ATTRIBUTE_HIDDEN) {       
        TRACEMSG(ZONE_FILES, (L"   FILE_ATTRIBUTE_HIDDEN"));
    }
    if(_dwAttributes & FILE_ATTRIBUTE_NORMAL) {       
        TRACEMSG(ZONE_FILES, (L"   FILE_ATTRIBUTE_NORMAL"));
    }
    if(_dwAttributes & FILE_ATTRIBUTE_READONLY) {       
        TRACEMSG(ZONE_FILES, (L"   FILE_ATTRIBUTE_READONLY"));
    }
    if(_dwAttributes & FILE_ATTRIBUTE_SYSTEM) {       
        TRACEMSG(ZONE_FILES, (L"   FILE_ATTRIBUTE_SYSTEM"));
    }
    if(_dwAttributes & FILE_FLAG_WRITE_THROUGH) {       
        TRACEMSG(ZONE_FILES, (L"   FILE_FLAG_WRITE_THROUGH"));
    }
    
    //
    // Take care of share mode
    if(_dwShareMode & FILE_SHARE_READ) {
        TRACEMSG(ZONE_FILES, (L"   FILE_SHARE_READ"));
    }   
    if(_dwShareMode & FILE_SHARE_WRITE) {
        TRACEMSG(ZONE_FILES, (L"   FILE_SHARE_WRITE"));
    }
#endif
      
    //
    // Open up the file
    m_hFile = CreateFile(pFile, _dwAccess, _dwShareMode, NULL, _dwCreationDispostion ,_dwAttributes,NULL);
    
    if(INVALID_HANDLE_VALUE == m_hFile) {
        DWORD dwErr = GetLastError();
        //
        // Set any special
        if(ERROR_FILE_EXISTS == dwErr) {
            hr = E_FILE_ALREADY_EXISTS;
        } else if (ERROR_FILE_NOT_FOUND == dwErr) {
            hr = E_FILE_NOT_FOUND;
        } else if(ERROR_PATH_NOT_FOUND == dwErr) {
            hr = E_PATH_NOT_FOUUND;
        } else if(ERROR_ACCESS_DENIED == dwErr) {
            hr = E_ACCESSDENIED;
        } else if(ERROR_SHARING_VIOLATION == dwErr) {
            hr = E_ACCESSDENIED;
        }
        TRACEMSG(ZONE_FILES, (L"SMBSRV-FILE: CreateFile for %s failed!", pFile));
        goto Done;
    }
    TRACEMSG(ZONE_FILES, (L"Createfile on %s FID: %d got handle 0x%x:", FileName(), m_usFID, (UINT)m_hFile));    
    
    if(NULL != _pdwActionTaken) {
        if(CREATE_ALWAYS == _dwCreationDispostion || OPEN_ALWAYS == _dwCreationDispostion) {            
            if(ERROR_ALREADY_EXISTS == GetLastError()) {
                *_pdwActionTaken = 1;
            } else {
                *_pdwActionTaken = 2;
            }
        } else if (CREATE_NEW == _dwCreationDispostion) {
            *_pdwActionTaken = 2;
        } else {
            *_pdwActionTaken = 1;    
        }
    }
    if(FAILED(hr = m_FileName.append(pFile))) {
        ASSERT(FALSE);        
        goto Done;
    }
        
    hr = S_OK;
    Done:
        if(FAILED(hr)) {
            if(INVALID_HANDLE_VALUE != m_hFile) {
                CloseHandle(m_hFile);
                m_hFile = INVALID_HANDLE_VALUE;
            }
        }    
        return hr;
}


HRESULT FileObject::Read(USHORT usFID, 
                        BYTE *pDest, 
                        LONG lOffset, 
                        DWORD dwReqSize, 
                        DWORD *pdwRead)
{
    HRESULT hr = S_OK;
    DWORD dwTotalRead = 0;
    DWORD dwJustRead = 0;
    USHORT usRangeFID = 0xFFFF;
    BOOL   fIsLocked = TRUE;
    
    ASSERT(INVALID_HANDLE_VALUE != m_hFile);
    IFDBG(DWORD dwRequestOrig = dwReqSize);
    TRACEMSG(ZONE_FILES, (L"Read on %s FID: %d got handle 0x%x:", FileName(), m_usFID, (UINT)m_hFile));    
    
    //
    // First make sure that this region isnt locked (and if it is locked, that its locked by us)
    if(FAILED(m_LockNode->IsLocked(lOffset, dwReqSize, &fIsLocked, &usRangeFID))) {
        TRACEMSG(ZONE_ERROR, (L"SMBFILEOBJECT: Can't test range lock (FID: 0x%x)failed on Read", usFID));          
        ASSERT(FALSE);
        hr = E_LOCK_VIOLATION;
        goto Done;
    }
    if(TRUE == fIsLocked && usFID != usRangeFID) {
        TRACEMSG(ZONE_ERROR, (L"SMBFILEOBJECT: Can't read (FID: 0x%x) (Offset: %d)  (Size: %d) because its locked by (FID: %d)", usFID, lOffset,dwReqSize, usRangeFID));       
        hr = E_LOCK_VIOLATION;
        goto Done;
    }
    
    //
    // Set the file pointer.
    if(0xFFFFFFFF == ::SetFilePointer(m_hFile, lOffset, NULL, FILE_BEGIN)) {
        if(ERROR_SUCCESS != GetLastError()) {
            TRACEMSG(ZONE_ERROR, (L"SMBFILEOBJECT: Setting file pointer (FID: 0x%x)failed on Read -- GetLastError(%d)", usFID, GetLastError()));  
            hr = E_FAIL;
            goto Done;
        }
    }
    
    //
    // Do the file operation    
    while(dwReqSize) {        
        if(0 == ReadFile(m_hFile, pDest, dwReqSize, &dwJustRead, NULL)) {
            TRACEMSG(ZONE_ERROR, (L"SMBSRVR: File -- call to ReadFile(FID:0x%x) for %d bytes failed -- GetLastError(%d)", usFID, dwReqSize, GetLastError()));
            hr = E_FAIL;
            goto Done;
        }

        if(0 == dwJustRead) {
            goto Done;
        }
        dwReqSize -= dwJustRead;
        pDest += dwJustRead;
        dwTotalRead += dwJustRead;
    }    
Done:
    IFDBG(TRACEMSG(ZONE_FILES, (L"SMBSRV: Readfile -- FID: %d  Offset: %d  ReqSize: %d  Got: %d  GLE: %d", usFID, lOffset, dwRequestOrig, dwTotalRead, GetLastError())));
    *pdwRead = dwTotalRead;
    return hr;
        
}

HRESULT FileObject::Write(USHORT usFID, 
                         BYTE *pSrc, 
                         LONG lOffset, 
                         DWORD dwReqSize, 
                         DWORD *pdwWritten)
{
    HRESULT hr = S_OK;
    DWORD dwJustWritten = 0;
    DWORD dwTotalWritten = 0;
    ASSERT(INVALID_HANDLE_VALUE != m_hFile);
    IFDBG(DWORD dwOrigRequest = dwReqSize);    
    TRACEMSG(ZONE_FILES, (L"Write on %s FID: %d got handle 0x%x:", FileName(), m_usFID, (UINT)m_hFile));    
    USHORT usRangeFID = 0xFFFF;
    BOOL   fIsLocked = TRUE;
    
    //
    // First make sure that this region isnt locked (and if it is locked, that its locked by us)
    if(FAILED(m_LockNode->IsLocked(lOffset, dwReqSize, &fIsLocked, &usRangeFID))) {
        TRACEMSG(ZONE_ERROR, (L"SMBFILEOBJECT: Can't test range lock (FID: 0x%x)failed on Write", usFID));        
        ASSERT(FALSE);
        hr = E_LOCK_VIOLATION;
        goto Done;
    }
    if(TRUE == fIsLocked && usFID != usRangeFID) {
        TRACEMSG(ZONE_ERROR, (L"SMBFILEOBJECT: Can't write (FID: 0x%x) (Offset: %d)  (Size: %d) because its locked by (FID:%d)", usFID, lOffset,dwReqSize, usRangeFID));       
        hr = E_LOCK_VIOLATION;
        goto Done;
    }
   
    //
    // Set the file pointer.
    if(0xFFFFFFFF == ::SetFilePointer(m_hFile, lOffset, NULL, FILE_BEGIN)) {
        if(ERROR_SUCCESS != GetLastError()) {
            TRACEMSG(ZONE_ERROR, (L"SMBFILEOBJECT: Setting file pointer (FID: 0x%x)failed on Write-- GetLastError(%d)", usFID, GetLastError()));   
            hr = E_FAIL;
            goto Done;
        }
    }
    
    //
    // Do the file operation
    while(dwReqSize) {
        *pdwWritten = 0;        
        if(0 == WriteFile(m_hFile, pSrc, dwReqSize, &dwJustWritten, NULL)){
            TRACEMSG(ZONE_ERROR, (L"SMBSRVR: File -- call to WriteFile (FID: 0x%x) for %d bytes failed -- GetLastError(%d)", usFID, dwReqSize, GetLastError()));
            hr = E_FAIL;
            goto Done;
        }
               
        dwReqSize -= dwJustWritten;
        pSrc += dwJustWritten;
        dwTotalWritten += dwJustWritten;
    }
    
    Done:
        *pdwWritten = dwTotalWritten;
        IFDBG(TRACEMSG(ZONE_FILES, (L"SMBSRV: Writefile -- FID: %d  Offset: %d  ReqSize: %d  Wrote: %d  GLE: %d", usFID, lOffset, dwOrigRequest, dwTotalWritten, GetLastError())));
        return hr;
}


HRESULT 
FileObject::SetEndOfStream(DWORD dwOffset)
{
    HRESULT hr = S_OK;
    ASSERT(INVALID_HANDLE_VALUE != m_hFile);    
    TRACEMSG(ZONE_FILES, (L"SetEndOfStream on %s FID: %d got handle 0x%x:", FileName(), m_usFID, (UINT)m_hFile));    
    
    //
    // Set the file pointer.
    if(0xFFFFFFFF == ::SetFilePointer(m_hFile, dwOffset, NULL, FILE_BEGIN)) {
        if(ERROR_SUCCESS != GetLastError()) {
            TRACEMSG(ZONE_ERROR, (L"SMBFILEOBJECT: Setting file pointer failed on SetEndOfStream-- GetLastError(%d)", GetLastError()));        
            hr = E_FAIL;
            goto Done;
        }
    }
    
    //
    // Do the set file operation
    if(0 == SetEndOfFile(m_hFile)) {
        TRACEMSG(ZONE_ERROR, (L"SMBFILEOBJECT: Setting end of file failed on SetEndOfStream-- GetLastError(%d)", GetLastError()));           
        hr = E_FAIL;
    }    
    
    Done:
        return hr;
}

HRESULT FileObject::SetFileTime(FILETIME *pCreation, 
                               FILETIME *pAccess, 
                               FILETIME *pWrite)
{  
    HRESULT hr = E_FAIL;;
    ASSERT(INVALID_HANDLE_VALUE != m_hFile);
    TRACEMSG(ZONE_FILES, (L"SetFileTime on %s FID: %d got handle 0x%x:", FileName(), m_usFID, (UINT)m_hFile));    

    //
    // Do the set file operation
    if(0 == ::SetFileTime(m_hFile, pCreation, pAccess, pWrite)) {
        TRACEMSG(ZONE_ERROR, (L"SMBFILEOBJECT: Setting filetime failed on SetFileTime-- GetLastError(%d)", GetLastError()));           
        hr = E_FAIL;
        goto Done;
    } 
    
    //
    // Success
    hr = S_OK;
    
    Done:
        return hr;
}

HRESULT 
FileObject::GetFileSize(DWORD *pdwSize)
{
    ASSERT(INVALID_HANDLE_VALUE != m_hFile);
    TRACEMSG(ZONE_FILES, (L"GetFileSize on %s FID: %d got handle 0x%x:", FileName(), m_usFID, (UINT)m_hFile));    
    
    *pdwSize = ::GetFileSize(m_hFile, NULL);
    return S_OK;
}

USHORT 
FileObject::FID()
{    
    return m_usFID;
}

const WCHAR  *
FileObject::FileName()
{
    return m_FileName.GetString();
}


HRESULT 
FileObject::Flush()
{
    HRESULT hr = E_FAIL;
    ASSERT(INVALID_HANDLE_VALUE != m_hFile);    
    TRACEMSG(ZONE_FILES, (L"Flush on %s FID: %d got handle 0x%x:", FileName(), m_usFID, (UINT)m_hFile));    
    
    //
    // Try the operation
    if(0 == FlushFileBuffers(m_hFile)) {
        TRACEMSG(ZONE_ERROR, (L"SMBSRVR: FileObject::FlushFileBuffers() failed!"));
        ASSERT(FAILED(hr));
        goto Done;
    }
    
    //
    // if we get here, everything is okay
    hr = S_OK;
    
    Done:
        return hr;
}

HRESULT 
FileObject::GetFileTime(LPFILETIME lpCreationTime, 
                       LPFILETIME lpLastAccessTime, 
                       LPFILETIME lpLastWriteTime)
{
    HRESULT hr = E_FAIL;
    ASSERT(INVALID_HANDLE_VALUE != m_hFile);
    TRACEMSG(ZONE_FILES, (L"GetFileTime on %s FID: %d got handle 0x%x:", FileName(), m_usFID, (UINT)m_hFile));    

    //
    // Try the operation
    // NOTE: we have to do this by filename (with findfirstfile) b/c the file may not be open for read
    if(FAILED(GetFileTimesFromFileName(FileName(), lpCreationTime, lpLastAccessTime, lpLastWriteTime))) {
        TRACEMSG(ZONE_ERROR, (L"SMBSRVR: FileObject::GetFileTime() failed! (%d)", GetLastError()));
        ASSERT(FALSE);
        goto Done;        
    }
    
    //
    // if we get here, everything is okay
    hr = S_OK;
    
    Done:
        return hr;
}


VOID 
FileObject::SetLockNode(RangeLock *pNode)

{
    ASSERT(NULL == m_LockNode);
    m_LockNode = pNode;
}

HRESULT 
FileObject::IsLocked(USHORT usPID,ULONG Offset,ULONG Length, BOOL *pfLocked)
{      
    return m_LockNode->IsLocked(Offset, Length, pfLocked);
}

HRESULT 
FileObject::Lock(USHORT usPID,ULONG Offset,ULONG Length)
{   
    return m_LockNode->Lock(m_usFID, Offset, Length);
}

HRESULT 
FileObject::UnLock(USHORT usPID,ULONG Offset, ULONG Length)
{
    return m_LockNode->UnLock(m_usFID, Offset, Length);
}



//
// Abstraction for file system (it makes up for things the CE filesystem lacks)
AbstractFileSystem::AbstractFileSystem()
{
    ASSERT(0 == FileObjectList.size());
}

AbstractFileSystem::~AbstractFileSystem()
{
    //
    // Go through all the file objects deleting (and closing) them
    while(FileObjectList.size()) {        
        FileObjectList.pop_front();
    }
}

HRESULT 
AbstractFileSystem::Open(const WCHAR *pFileName, 
             USHORT usFID,
             FileObject **ppFile,
             DWORD dwAccess, 
             DWORD dwCreationDispostion, 
             DWORD dwAttributes, 
             DWORD dwShareMode,
             DWORD *pdwActionTaken)
{        
    HRESULT hr = E_FAIL;
    ce::list<RangeLock, AFS_LOCKNODE_ALLOC >::iterator it;
    RangeLock *pLockNode = NULL;
    FileObject NewFile(usFID);
  
    //
    // Try to open up the file        
    if(FAILED(hr = NewFile.Open(pFileName, dwCreationDispostion, dwAttributes, dwAccess, dwShareMode, pdwActionTaken))) {
        goto Done;
    }    
    ASSERT(SUCCEEDED(hr));
        
    //
    // Search out a lock node -- if one doesnt exist, make one 
    for(it = LockNodeList.begin(); it != LockNodeList.end(); ++it) {
        if(0 == wcscmp(pFileName, (*it).FileName.GetString())) {
            pLockNode = &(*it);
            ASSERT(0 != pLockNode->uiRefCnt);
            break;
        }    
    }
    
    if(NULL == pLockNode) {
        RangeLock LockTemp;
        LockNodeList.push_front(LockTemp);
        pLockNode = &(LockNodeList.front());
        pLockNode->uiRefCnt = 0;
        pLockNode->FileName.append(pFileName);
    }
    
    ASSERT(NULL != pLockNode);
    pLockNode->uiRefCnt ++;
    
    hr = S_OK; 
    Done:
        if(SUCCEEDED(hr)) {   
            NewFile.SetLockNode(pLockNode);
            FileObjectList.push_back(NewFile);
        }    
        return hr;
}

HRESULT 
AbstractFileSystem::Close(USHORT usFID)
{
    ce::list<FileObject, AFS_FILEOBJECT_ALLOC >::iterator it;
    ce::list<FileObject, AFS_FILEOBJECT_ALLOC >::iterator itEnd = FileObjectList.end();
    FileObject *pFile = NULL;
    HRESULT hr = E_FAIL;
          
    //
    // Search out our file 
    for(it = FileObjectList.begin(); it != itEnd; ++it) {
        if(usFID == (*it).FID()) {
           StringConverter myFile;

           IFDBG(BOOL fFound = FALSE);
           ce::list<RangeLock, AFS_LOCKNODE_ALLOC >::iterator itLock;               
           (*it).Close();
           myFile.append((*it).FileName());
           FileObjectList.erase(it);
           
           //
           // Search out a lock node -- dec the ref cnt, and if its 0 delete the node
           for(itLock = LockNodeList.begin(); itLock != LockNodeList.end(); ++itLock) {
               ASSERT(0 != (*itLock).uiRefCnt);

               if(0 == wcscmp((*itLock).FileName.GetString(), myFile.GetString())) {                    
                    IFDBG(fFound = TRUE);
                    (*itLock).uiRefCnt --;
                    if(0 == (*itLock).uiRefCnt) {
                        LockNodeList.erase(itLock);                       
                    }
                    break;
               }    
           }
           IFDBG(ASSERT(TRUE == fFound));
           hr = S_OK;
           break;               
        } 
    }    
    ASSERT(SUCCEEDED(hr));
    return hr;
}

HRESULT 
AbstractFileSystem::FindFile(USHORT usFID, FileObject **ppFileObject)
{
    ce::list<FileObject, AFS_FILEOBJECT_ALLOC >::iterator it;
    ce::list<FileObject, AFS_FILEOBJECT_ALLOC >::iterator itEnd = FileObjectList.end();
    HRESULT hr = E_FAIL;
          
    //
    // Search out our file 
    for(it = FileObjectList.begin(); it != itEnd; ++it) {
        if(usFID == (*it).FID()) {
            *ppFileObject = &(*it);
            hr = S_OK;
            goto Done;
        }
    }    
    Done:
        return hr;
}



//  Compare two range nodes
//  0 = equals 
//  + = pOne > pTwo
//  - = pOne < pTwo
INT CompareRange(RANGE_NODE *pOne, RANGE_NODE *pTwo) 
{
    RANGE_NODE *pA, *pB;
    BOOL fFlip = FALSE;
    INT  iRet;
    
    //
    // Sort the two points
    if(pOne->Offset >= pTwo->Offset) {
        pA = pTwo;
        pB = pOne;
        fFlip = TRUE;
    } else {    
        pA = pOne;
        pB = pTwo;
        fFlip = FALSE;
    }
    
    //
    // 2 cases (since they are sorted)
    // 
    // 1. A and B overlap
    // 2. A is less than B
    if(pA->Offset + pA->Length > pB->Offset) {
        // 
        // Overlap
        iRet = 0;
    } else {
        //
        // One is greater (using the fFlip flag to determine which one)
        iRet = (fFlip)?-1:1;
    }
    
    return iRet;    
}

HRESULT RangeLock::IsLocked(ULONG Offset,ULONG Length, BOOL *pfVal, USHORT *pFID)
{
    ce::list<RANGE_NODE, RANGE_NODE_ALLOC >::iterator it;
    RANGE_NODE NewNode;
    NewNode.Offset = Offset;
    NewNode.Length = Length;
    
    BOOL fFound = FALSE;
    
    for(it = m_LockList.begin(); it != m_LockList.end(); ++it) {
        if(0 == CompareRange(&NewNode, &(*it))) {
            if(NULL != pFID) {
                *pFID = (*it).usFID;
            }
            fFound = TRUE;
            goto Done;
        }           
    }
    Done:
        *pfVal = fFound;
        return S_OK;
}

HRESULT 
RangeLock::Lock(USHORT usFID, ULONG Offset, ULONG Length)
{
    ce::list<RANGE_NODE, RANGE_NODE_ALLOC >::iterator it;
    
    HRESULT hr = S_OK;
    RANGE_NODE NewNode;
    NewNode.Offset = Offset;
    NewNode.Length = Length;
    NewNode.usFID = usFID;
      
    //
    // Search out and lock the node
    for(it = m_LockList.begin(); it != m_LockList.end(); ++it) {
        INT uiCompare = CompareRange(&(*it), &NewNode);
        ASSERT(0 != uiCompare);        
        if(uiCompare > 0) {            
            break;
        }    
    }   
    m_LockList.insert(it, NewNode);
    
    #ifdef DEBUG
        //
        // Perform a sanity check on the list
        ce::list<RANGE_NODE, RANGE_NODE_ALLOC >::iterator itPrev = m_LockList.begin();
        ce::list<RANGE_NODE, RANGE_NODE_ALLOC >::iterator itVar = m_LockList.begin();
        itVar ++;
        for(; itVar != m_LockList.end(); ++itVar) {
            ASSERT(CompareRange(&(*itPrev), &(*itVar)) < 0);
            itPrev = itVar;
        }
    #endif
   
    return S_OK;
}

HRESULT 
RangeLock::UnLock(USHORT usFID, ULONG Offset, ULONG Length)
{
    ce::list<RANGE_NODE, RANGE_NODE_ALLOC >::iterator it;
    RANGE_NODE NewNode;
    NewNode.Offset = Offset;
    NewNode.Length = Length;
    BOOL fFound = FALSE;
    
    //
    // Find and unlock our node
    for(it = m_LockList.begin(); it != m_LockList.end(); ++it) {
    
        //
        // If the ranges are equal and the FID's match, erase it
        if((0 == CompareRange(&NewNode, &(*it))) && (usFID == (*it).usFID)) {
            m_LockList.erase(it);
            fFound = TRUE;
            goto Done;
        }           
    }
    Done:        
        return fFound?S_OK:E_FAIL;
}

HRESULT 
RangeLock::UnLockAll(USHORT usFID)
{
    ce::list<RANGE_NODE, RANGE_NODE_ALLOC >::iterator it;

    //
    // Find and unlock our node
    for(it = m_LockList.begin(); it != m_LockList.end();) {    
        //
        // If the FID's match, erase it
        if(usFID == (*it).usFID) {
            m_LockList.erase(it++);
        } else {
            ++it;
        }
    }
    return S_OK;
}


