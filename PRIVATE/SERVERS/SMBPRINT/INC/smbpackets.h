//
// Copyright (c) Microsoft Corporation.  All rights reserved.
//
//
// This source code is licensed under Microsoft Shared Source License
// Version 1.0 for Windows CE.
// For a copy of the license visit http://go.microsoft.com/fwlink/?LinkId=3223.
//
#ifndef SMB_PACKETS_H
#define SMB_PACKETS_H

#pragma pack(1)

struct SMB_COM_NEGOTIATE_CLIENT_REQUEST 
{
    UCHAR WordCount;
    USHORT ByteCount; 
    
    //for the remainder of the packet, it will be in the format
    //[byte][null term ASCII string]
    // byte MUST be 0x02
};

struct SMB_COM_NEGOTIATE_SERVER_RESPONSE 
{    
    UCHAR  WordCount;   
    USHORT DialectIndex;
    UCHAR  SecurityMode;
    USHORT MaxMpxCount;    
    USHORT MaxCountVCs;    
    ULONG  MaxTransmitBufferSize;   
    ULONG  MaxRawSize;    
    ULONG  SessionKey;    
    ULONG  Capabilities;    
    ULONG  SystemTimeLow;    
    ULONG  SystemTimeHigh;    
    USHORT ServerTimeZone; 
    UCHAR  EncryptionKeyLength;
};


struct SMB_COM_NEGOTIATE_SERVER_RESPONSE_DIALECT_LM {
    SMB_COM_NEGOTIATE_SERVER_RESPONSE inner;    
    USHORT ByteCount;
    BYTE   Blob[16]; //used for either server GUID or for encryption key
}; 


struct SMB_COM_NEGOTIATE_SERVER_RESPONSE_DIALECT_NTLM 
{
    SMB_COM_NEGOTIATE_SERVER_RESPONSE inner;    
    USHORT ByteCount;
    BYTE   ServerGuid[16];
}; 

struct SMB_COM_NEGOTIATE_SERVER_RESPONSE_DIALECT_PCNETPROG 
{
    UCHAR WordCount;
    USHORT DialectIndex;
    USHORT ByteCount;
};

struct SMB_RAP_RESPONSE_PARAM_HEADER 
{
    USHORT ErrorStatus;
    USHORT ConverterWord;
    USHORT NumberEntries;
    USHORT TotalEntries;
};

struct SMB_HEADER;
struct SMB_PROCESS_CMD 
{
    SMB_HEADER *pSMBHeader;
    BYTE *pDataPortion;
    UINT uiDataSize;
};

struct SMB_NETSHARE_GETINFO_RESPONSE_PARMS 
{
    USHORT ReturnStatus;
    USHORT ConverterWord;
    USHORT AvailableBytes;
};


struct SMB_COM_TRANSACTION_SERVER_RESPONSE 
{
    UCHAR WordCount;
    USHORT TotalParameterCount; //smb_tprcnt
    USHORT TotalDataCount;      //smb_tdrcnt
    USHORT Reserved;
    USHORT ParameterCount;      //smb_prcnt -- # bytes in THIS buffer
    USHORT ParameterOffset;     
    USHORT ParameterDisplacement;
    USHORT DataCount;           //smb_suwcnt -- setup word count
    USHORT DataOffset;
    USHORT DataDisplacement;
    UCHAR SetupCount;
    UCHAR Reserved2;
    //USHORT Setup[SetupCount];    
    USHORT ByteCount; // only valid if SetupCount = 0
};
struct SMB_COM_TRANSACTION_CLIENT_REQUEST 
{
    UCHAR  WordCount;
    USHORT TotalParameterCount;
    USHORT TotalDataCount;
    USHORT MaxParameterCount;
    USHORT MaxDataCount;
    UCHAR  MaxSetupCount;
    UCHAR  Reserved;
    USHORT Flags;

    ULONG  Timeout;
    USHORT Reserved2;
    USHORT ParameterCount;
    USHORT ParameterOffset;
    USHORT DataCount;
    USHORT DataOffset;
    UCHAR  SetupCount;
    UCHAR  Reserved3;
    //USHORT Setup[SetupCount];
};





struct SMB_COM_TRANSACTION2_CLIENT_REQUEST 
{
    UCHAR  WordCount;
    USHORT TotalParameterCount;
    USHORT TotalDataCount;
    USHORT MaxParameterCount;
    USHORT MaxDataCount;
    UCHAR  MaxSetupCount;
    UCHAR  Reserved;
    USHORT Flags;
    ULONG  Timeout;
    USHORT Reserved2;
    USHORT ParameterCount;
    USHORT ParameterOffset;
    USHORT DataCount;
    USHORT DataOffset;
    UCHAR  SetupCount;
    UCHAR  Reserved3;  
    USHORT Setup; //note: this is really Setup[SetupCount]
};

struct SMB_COM_TRANSACTION2_SERVER_RESPONSE 
{
    UCHAR WordCount;
    USHORT TotalParameterCount;
    USHORT TotalDataCount;
    USHORT Reserved;
    USHORT ParameterCount;
    USHORT ParameterOffset;
    USHORT ParameterDisplacement;
    USHORT DataCount;
    USHORT DataOffset;
    USHORT DataDisplacement;
    UCHAR SetupCount;
    UCHAR Reserved2;
    //USHORT Setup[SetupCount];    
    USHORT ByteCount; // only valid if SetupCount = 0
};

struct SMB_TRANS2_FIND_FIRST2_CLIENT_REQUEST 
{
    USHORT SearchAttributes;
    USHORT SearchCount;
    USHORT Flags;
    USHORT InformationLevel;
    ULONG SearchStorageType;
    //[STRING] FileName
    //UCHAR Data[TotalDataCount]
};

struct SMB_TRANS2_FIND_FIRST2_SERVER_RESPONSE 
{
    USHORT Sid;
    USHORT SearchCount;
    USHORT EndOfSearch;
    USHORT EaErrorOffset;
    USHORT LastNameOffset;
    //UCHAR Data[TotalDataCount];
};

#define FIND_NEXT_CLOSE_AFTER_REQUEST 1
#define FIND_NEXT_CLOSE_AT_END        2
#define FIND_NEXT_RETURN_RESUME_KEY   4
#define FIND_NEXT_RESUME_FROM_PREV    8
#define FIND_NEXT_FIND_WITH_BACKUP   16

struct SMB_TRANS2_FIND_NEXT2_CLIENT_REQUEST 
{
    USHORT Sid;
    USHORT SearchCount;
    USHORT InformationLevel;
    ULONG  ResumeKey;
    USHORT Flags;    
    //[STRING] FileName    
};

struct SMB_TRANS2_FIND_NEXT2_SERVER_RESPONSE 
{
    USHORT SearchCount;
    USHORT EndOfSearch;
    USHORT EaErrorOffset;
    USHORT LastNameOffset;
    //UCHAR Data[TotalDataCount];
};

struct SMB_TRANS2_FIND_CLOSE_CLIENT_REQUEST 
{
    UCHAR  WordCount;
    USHORT Sid;
    USHORT ByteCount;
};


struct SMB_DATE{
        USHORT Day : 5;
        USHORT Month : 4;
        USHORT Year : 7;
};

struct SMB_TIME{
        USHORT TwoSeconds : 5;
        USHORT Minutes : 6;
        USHORT Hours : 5;
};


#define SMB_INFO_STANDARD                      1
#define SMB_INFO_QUERY_EA_SIZE                 2
#define SMB_INFO_EAS_FROM_LIST                 3
#define SMB_FIND_FILE_DIRECTORY_INFO           0x101
#define SMB_FIND_FILE_FULL_DIRECTORY_INFO      0x102
#define SMB_FIND_FILE_NAMES_INFO               0x103
#define SMB_FIND_FILE_BOTH_DIRECTORY_INFO      0x104


//
// Spec in cifs9f.doc
struct SMB_FIND_FILE_BOTH_DIRECTORY_INFO_STRUCT 
{
    ULONG NextEntryOffset;
    ULONG FileIndex;
    LARGE_INTEGER CreationTime;
    LARGE_INTEGER LastAccessTime;
    LARGE_INTEGER LastWriteTime;
    LARGE_INTEGER ChangeTime;
    LARGE_INTEGER EndOfFile;
    LARGE_INTEGER AllocationSize;
    ULONG ExtFileAttributes;
    ULONG FileNameLength;
    ULONG EaSize;
    UCHAR ShortNameLength;
    UCHAR Reserved;
    WCHAR ShortName[12];
    //[STRING] Files full name
};


//
// Spec in cifs9f.doc
struct SMB_FIND_FILE_STANDARD_INFO_STRUCT 
{
	ULONG ulResume;
    SMB_DATE CreationDate;
    SMB_TIME CreationTime;
    SMB_DATE LastAccessDate;
    SMB_TIME LastAccessTime;
    SMB_DATE LastWriteDate;
    SMB_TIME LastWriteTime;
    ULONG DataSize;
    ULONG AllocationSize;
    USHORT Attributes;
    UCHAR FileNameLen;
    //[STRING] FileName
};



//
// Taken from cifsrap2.doc
struct SMB_SERVER_INFO_1 
{
        CHAR            Name[16];                    //(null terminated)
        CHAR            version_major;               //version #
        CHAR            version_minor;               //version #
        ULONG           Type;                        //see PC_NET_PROG.h for possible values (one or more)
        CHAR            *comment_or_master_browser;  //comment describing server (or domain)
};

//
// Taken from CIFSRAP2.doc
struct SMB_SERVER_INFO_0 
{
        char        sv0_name[16];
};



struct SMB_SHARE_INFO_1 
{
    CHAR            Netname[13]; //contains share name (ASCIIZ) of resource
    //CHAR            Netname[81];
    CHAR            Pad;         
    USHORT          Type;        //type of shared resource 
                                 //  0 = Disk Directory Tree
                                 //  1 = Printer Queue  
                                 //  2 = Communications device
                                 //  3 = IPC
                                 
    CHAR            *Remark;     //null terminated string with comment describing share 
};

struct SMB_COM_QUERY_INFO_REQUEST
{
    BYTE WordCount;
    USHORT ByteCount;
    //CHAR StringType; //must be 0x04
    //here is a STRING for filename (null terminated)
};


struct SMB_COM_QUERY_INFO_RESPONSE_INSIDE 
{
    BYTE WordCount;
    USHORT FileAttributes;
    ULONG LastModifyTime;
    ULONG  FileSize;   
    ULONG  Unknown;
    ULONG  Unknown2;
    USHORT Unknown3;
};

struct SMB_COM_QUERY_INFO_RESPONSE
{
    SMB_COM_QUERY_INFO_RESPONSE_INSIDE Fields;
    USHORT ByteCount;
};



struct SMB_COM_QUERY_INFO_DISK_REQUEST //From SMBPUB.zip (SMBPUB.DOC)
{
    BYTE WordCount;
    USHORT ByteCount;
};


//From SMBPUB.zip (SMBPUB.DOC)
struct SMB_COM_QUERY_INFO_DISK_RESPONSE
{
    BYTE    WordCount;
    USHORT  TotalUnits;
    USHORT  BlocksPerUnit;
    USHORT  BlockSize;   
    USHORT  FreeUnits;
    USHORT  Reserved;
    USHORT ByteCount;
};

struct SMB_COM_QUERY_EXINFO_REQUEST 
{
    BYTE   WordCount;
    USHORT FID;
};


struct SMB_COM_QUERY_EXINFO_RESPONSE_INSIDE 
{
    BYTE     WordCount;
    SMB_DATE CreateDate;
    SMB_TIME CreateTime; 
    SMB_DATE AccessDate;
    SMB_TIME AccessTime; 
    SMB_DATE ModifyDate;
    SMB_TIME ModifyTime; 
    ULONG    FileSize;   
    ULONG    Allocation;
    USHORT   Attributes;
};

struct SMB_COM_QUERY_EXINFO_RESPONSE
{
    SMB_COM_QUERY_EXINFO_RESPONSE_INSIDE Fields;
    USHORT ByteCount;
};


struct SMB_COM_ANDX_HEADER_INNER
{
    UCHAR AndXCommand;  
    UCHAR AndXReserved;    
    USHORT AndXOffset; 
};

struct SMB_COM_ANDX_HEADER
{
    UCHAR WordCount;    
    UCHAR AndXCommand;  
    UCHAR AndXReserved;    
    USHORT AndXOffset;
};

struct SMB_COM_GENERIC_RESPONSE {
    UCHAR WordCount;
    USHORT ByteCount;
};

struct SMB_COM_SEARCH_RESPONSE {
    UCHAR WordCount;
    USHORT Count;
    USHORT ByteCount;
    UCHAR BufferFormat;
    USHORT DataLength;
};


//
// Per netmon -- I cant find where this is speced -- its used
//    when the extended bit is set 
struct SMB_COM_SESSION_SETUP_REQUEST_EXTENDED_NTLM 
{
    SMB_COM_ANDX_HEADER ANDX; 
    USHORT MaxBufferSize;    
    USHORT MaxMpxCount;    
    USHORT VcNumber;   
    ULONG SessionKey;    
    USHORT PasswordLength;    
    ULONG Reserved; 
    ULONG Capabilities;
    USHORT ByteCount;    
};

//
// Per netmon -- I cant find where this is speced -- its used
//    when the extended bit is set 
struct SMB_COM_SESSION_SETUP_RESPONSE_EXTENED_NTLM
{
    SMB_COM_ANDX_HEADER ANDX;  
    USHORT Action;    
    USHORT SecurityBlobLength;   
    USHORT ByteCount;
    //STRING NativeOS
    //STRING NativeLanMan
    //STRING PrimaryDomain
};

//
// Per netmon of SPNEGO packet -- note: this should be done for real in the security API's
//   WinCE 4.2 + 1 (Macallan?)
struct SMB_SPNEGO_RESPONSE_HEADER 
{
	WORD wChoice;
	WORD wChoiceLen;
	WORD wSeq;
	WORD wSeqLen;
	BYTE OID[19];
	WORD wUnknown1;
	WORD wRemainingLen1;
	WORD wUnknown2;
	WORD wRemainingLen2;
	//BYTE array containing token 
};

//
// Per NT LM 0.12 spec in SMBPUB.DOC
struct SMB_COM_SESSION_SETUP_REQUEST_NTLM 
{
    SMB_COM_ANDX_HEADER ANDX; 
    USHORT MaxBufferSize;    
    USHORT MaxMpxCount;    
    USHORT VcNumber;   
    ULONG SessionKey;    
    USHORT CaseInsensitivePasswordLength;    
    USHORT CaseSensitivePasswordLength;    
    ULONG Reserved; 
    ULONG Capabilities;
    USHORT ByteCount;
    //UCHAR CaseInsensitivePassword[]
    //UCHAR CaseSensitivePassword[]
    //STRING AccountName[]
    //STRING PrimaryDomain[]
    //STRING NativeOS[]
    //STRING NativeLanMan[]    
};


//
// Per NT LM 0.12 spec in SMBPUB.DOC
struct SMB_COM_SESSION_SETUP_RESPONSE_NTLM
{
    SMB_COM_ANDX_HEADER ANDX;  
    USHORT Action;    
    //USHORT SecurityBlobLength;   (NOTE: I'm not sure about this field -- its for extended auth.  see spec if using extended auth)
    USHORT ByteCount;
    //STRING NativeOS
    //STRING NativeLanMan
    //STRING PrimaryDomain
};

struct SMB_COM_TREE_ANDX_CONNECT_RESPONSE 
{    
    SMB_COM_ANDX_HEADER ANDX; 
    USHORT ByteCount;      
};

struct SMB_COM_TREE_ANDX_CONNECT_CLIENT_REQUEST 
{
    SMB_COM_ANDX_HEADER ANDX;  
    USHORT Flags;
    USHORT PasswordLength;    
    USHORT ByteCount;
};

#define OPENX_ATTR_READ_ONLY 1
#define OPENX_ATTR_HIDDEN    2
#define OPENX_ATTR_SYSTEM    4
#define OPENX_ATTR_RESERVED  8
#define OPENX_ATTR_DIRECTORY 16
#define OPENX_ATTR_ARCHIVE   32

struct SMB_OPENX_CLIENT_REQUEST 
{   
    SMB_COM_ANDX_HEADER ANDX;
    USHORT Flags;
    USHORT Mode;
    USHORT SearchAttributes;
    USHORT FileAttributes;
    ULONG  Time;
    USHORT OpenFunction;
    ULONG FileSize;
    DWORD Timeout;
    DWORD Reserved;
    WORD ByteCount;
};

struct SMB_OPENX_SERVER_RESPONSE 
{
    SMB_COM_ANDX_HEADER ANDX;
    USHORT FileID;
    USHORT FileAttribute;
    ULONG LastModifyTime;
    ULONG  FileSize;
    USHORT OpenMode; //ie access
    USHORT FileType;
    USHORT DeviceState;
    USHORT ActionTaken;
    ULONG  ServerFileHandle;
    USHORT Reserved;
    USHORT ByteCount;
};

struct SMB_OPEN_CLIENT_REQUEST 
{   
    UCHAR WordCount;   
    USHORT Mode;
    USHORT FileAttributes;
    USHORT ByteCount;
};

struct SMB_OPEN_SERVER_RESPONSE 
{
    USHORT WordCount;
    USHORT FileID;
    USHORT FileAttribute;
    ULONG  LastModifyTime;  
    ULONG  FileSize;    
    USHORT OpenMode; //ie access
    USHORT ByteCount;
};



//from netmon
struct SMB_CLOSE_CLIENT_REQUEST 
{   
    UCHAR WordCount;
    USHORT FileID;
    ULONG FileTime;
};

struct SMB_CLOSE_SERVER_RESPONSE 
{
   BYTE WordCount;
   WORD ByteCount;   
};


struct SMB_LOCKX_CLIENT_REQUEST 
{   
    SMB_COM_ANDX_HEADER ANDX;
    USHORT FileID;
    USHORT LockType;
    ULONG OpenTimeout;
    USHORT NumUnlocks;
    USHORT NumLocks;
    USHORT ByteCount;
};

struct SMB_LOCKX_SERVER_RESPONSE 
{
    SMB_COM_ANDX_HEADER ANDX;
    WORD ByteCount;  
};

struct SMB_LOCK_RANGE {
    USHORT PID;
    ULONG Offset;
    ULONG Length;
};

//
// Per spec in smbhlp.zip
struct SMB_SET_EXTENDED_ATTRIBUTE_CLIENT_REQUEST 
{
    BYTE WordCount;
    USHORT FileID;
    USHORT CreationDate;
    USHORT CreationTime;
    USHORT AccessDate;
    USHORT AccessTime;
    USHORT ModifyDate;
    USHORT ModifyTime;
    };

//
// Per spec in smbhlp.zip
struct SMB_SET_ATTRIBUTE_CLIENT_REQUEST 
{
    BYTE   WordCount;
    USHORT FileAttributes;
    USHORT ModifyDate;
    USHORT ModifyTime;    
    USHORT Reserved[5];
    USHORT ByteCount;
    //ASCII String (null terminated)
};


//
// Per spec in smbhlp.zip
struct SMB_FILE_FLUSH_CLIENT_REQUEST 
{
    BYTE WordCount;
    USHORT FileID;
};


// 
// Per spec in smbhlp.zip 
struct SMB_CREATE_DIRECTORY_CLIENT_REQUEST 
{   
    BYTE WordCount;
    USHORT ByteCount;
    //STRING of filename
};


// 
// Per spec in smbhlp.zip 
struct SMB_DELETE_DIRECTORY_CLIENT_REQUEST 
{   
    BYTE WordCount;
    USHORT ByteCount;
    //STRING of filename
};


// 
// Per spec in smbhlp.zip 
struct SMB_DELETE_FILE_CLIENT_REQUEST 
{   
    BYTE WordCount;
    USHORT Attributes;
    USHORT ByteCount;
    //array of bytes
};

// 
// Per spec in smbhlp.zip 
struct SMB_DELETE_FILE_SERVER_RESPONSE 
{
    BYTE WordCount;
    WORD ByteCount;
};


// 
// Per spec in smbhlp.zip 
struct SMB_RENAME_FILE_CLIENT_REQUEST 
{   
    BYTE WordCount;
    USHORT Attributes;
    USHORT ByteCount;
};


struct SMB_COM_TREE_DISCONNECT_CLIENT_REQUEST
{
    UCHAR WordCount;
    USHORT ByteCount;
};

struct SMB_COM_TREE_DISCONNECT_CLIENT_RESPONSE
{
    UCHAR WordCount;
    USHORT ByteCount;
};


//
// Per spec in smbpub.zip
struct SMB_SEEK_CLIENT_REQUEST 
{
    BYTE WordCount;
    USHORT FileID;
    USHORT Mode;
    LONG Offset;
    USHORT ByteCount;
};

//
// Per spec in smbpub.zip
struct SMB_SEEK_SERVER_RESPONSE 
{
    BYTE WordCount;
    LONG Offset;
    USHORT ByteCount;
};


//
// Per spec in smbpub.zip
struct SMB_CHECKPATH_CLIENT_REQUEST 
{
    BYTE WordCount;
    USHORT ByteCount;
    //STRING path
};

//
// Per spec in smbpub.zip
struct SMB_CHECKPATH_SERVER_RESPONSE 
{
    BYTE WordCount;
    USHORT ByteCount;
};


struct SMB_SHARE_INFO_0 
{
    char shi1_netname[13];
};

struct SMB_NET_SHARE_GET_INFO_RESPONSE 
{
    USHORT ReturnStatus;
    USHORT ConverterWord;
    USHORT TotalRemainingBytes;
};


struct SMB_PRQINFO_1 
{
    USHORT JobID;
    char UserName[21];
    BYTE Pad;
    char NotifyName[16];
    char DataType[10];
    char *pParams;
    USHORT Position;
    USHORT Status;
    char *pStatus;
    ULONG ulSubmitted;
    ULONG ulSize;
    char *pComment;
};

struct SMB_PRQINFO_2 
{ 
    char Name[13];
    BYTE   Pad;
    USHORT Priority;
    USHORT StartTime;
    USHORT UntilTime;
    char  *pSepFile;
    char  *pPrProc;
    char  *pDest;
    char  *pParams;
    char  *pDontKnow; 
    USHORT Status;
    USHORT AuxCount;
};

struct SMB_PRQINFO_3 
{
    char *pszName;
    USHORT Priority;
    USHORT StartTime;
    USHORT UntilTime;
    USHORT Pad1;
    char *pSepFile;
    char *pPrProc;
    char *pParams;
    char *pComment;
    USHORT Status;
    USHORT cJobs;
    char *pPrinters;
    char *pDriverName;
    char *pDriverData;
};


//
// Taken from cifsprt.doc
struct PrintQueueGetInfo_PARAM_STRUCT 
{
    USHORT ReturnStatus;
    USHORT ConverterWord;
    USHORT TotalBytes;
};

//
//Taken off netmon + figured out what it was from the 
//  WB21BB16B109zWWzDDz... according to cifsprt.doc there should only
//  be two kinds of PRJINFO's (0 and 
struct PRJINFO_1 
{
    USHORT JobID;
    char UserName[21];
    BYTE Pad;
    char NotifyName[16];
    char DataType[10];    
    char *pParams;
    USHORT Position;
    USHORT Status;
    char *pStatus;
    ULONG ulSubmitted;
    ULONG ulSize;
    char *pComment;
};



//
// Taken from cifsrap2.doc
struct ServerGetInfo_RESPONSE_PARAMS 
{
    USHORT ReturnStatus;
    USHORT ConverterWord;
    USHORT TotalBytes;
};

//
// Taken from cifsrap2.doc
struct WNetWkstaGetInfo_RESPONSE_PARAMS 
{
    USHORT ReturnStatus;
    USHORT ConverterWord;
    USHORT TotalBytes;
};

//
// Taken from cifsrap2.doc
struct SMB_USERINFO_11 
{ 
    char *pComputerName;
    char *pUserName;
    char *pLanGroup;
    UCHAR verMajor;
    UCHAR verMinor;
    char *pDomain;
    char *pOtherDomains;
};

struct SMB_PRQINFO_0  
{
    char Name[13];
};

struct SMB_PRQ_GETINFO_RESPONSE_PARMS 
{
    USHORT ReturnStatus;
    USHORT ConverterWord;
    USHORT AvailableBytes;
};

struct SMB_NET_SERVER_GET_INFO
{    
    USHORT ReturnStatus;
    USHORT ConverterWord;
    USHORT AvailableBytes;
};

struct SMB_OPEN_PRINT_SPOOL_CLIENT_REQUEST 
{
    UCHAR WordCount;
    USHORT SpoolHeaderSize;
    USHORT SpoolMode;
    USHORT ByteCount; 
    //<STRING> fileName
};

struct SMB_OPEN_PRINT_SPOOL_SERVER_RESPONSE 
{
    UCHAR WordCount;
    USHORT FileID;
    BYTE ByteCount; //will be zero... subtract this out for WordCount
};


struct SMB_CLOSE_PRINT_SPOOL_CLIENT_REQUEST 
{
    UCHAR WordCount;
    USHORT FileID;
    USHORT ByteCount; 
};   

struct SMB_CLOSE_PRINT_SPOOL_SERVER_RESPONSE  
{
    UCHAR WordCount;
    USHORT ByteCount; 
};


struct SMB_WRITEX_CLIENT_REQUEST 
{
    UCHAR WordCount;
    USHORT FileID;
    USHORT IOBytes;
    ULONG FileOffset;
    USHORT BytesLeft;
    USHORT ByteCount;
    BYTE Pad;
    USHORT IOBytes2;  

};   

struct SMB_WRITEX_SERVER_RESPONSE 
{
    UCHAR WordCount;
    USHORT DataLength;
    USHORT ByteCount;
};


struct SMB_PRINT_JOB_PAUSE_SERVER_RESPONSE 
{
    USHORT ErrorCode;   
};

struct SMB_PRINT_JOB_SET_INFO_SERVER_RESPONSE 
{
    USHORT ErrorCode;   
    USHORT ConverterWord;
};

struct SMB_PRINT_JOB_DEL_SERVER_RESPONSE 
{
    USHORT ErrorCode;   
    USHORT ConverterWord;
};

//
// SMB TRANS2 -- TRANS2_QUERY_FS_INFORMATION
struct SMB_INFO_VOLUME_SERVER_RESPONSE 
{
    ULONG ulVolumeSerialNumber; 
    UCHAR NumCharsInLabel; //DOESNT include null, however put the NULLS in STRING!
    //STRING Label; 
};

//
// from cifs9f.doc
struct SMB_QUERY_FS_VOLUME_INFO_SERVER_RESPONSE {
	LARGE_INTEGER VolumeCreationTime;
	ULONG VolumeSerialNumber;
	ULONG LengthOfLabel;
	BYTE Reserved1;
	BYTE Reserved2;
	//STRING label
};
struct SMB_QUERY_FS_ATTRIBUTE_INFO_SERVER_RESPONSE 
{
    ULONG FileSystemAttributes;
    ULONG MaxFileNameComponent;
    ULONG NumCharsInLabel;
    //STRING Label
};

struct SMB_QUERY_INFO_ALLOCATION_SERVER_RESPONSE 
{
   ULONG idFileSystem;
   ULONG cSectorUnit;
   ULONG cUnit;
   ULONG cUnitAvail;
   USHORT cbSector;
};


//
// Spec in cifs9f.doc
struct SMB_QUERY_FILE_ALL_INFO 
{
    LARGE_INTEGER CreationTime;
    LARGE_INTEGER LastAccessTime;
    LARGE_INTEGER LastWriteTime;
    LARGE_INTEGER ChangeTime;
    ULONG Attributes;
    ULONG Pad;
    LARGE_INTEGER AllocationSize;
    LARGE_INTEGER EndOfFile;
    ULONG NumberOfLinks;
    BOOLEAN DeletePending;
    BOOLEAN Directory;
    LARGE_INTEGER Index_Num;
    ULONG EASize;
    ULONG AccessFlags;
    LARGE_INTEGER IndexNumber;
    LARGE_INTEGER CurrentByteOffset;
    ULONG Mode;
    ULONG AlignmentRequirement;
    ULONG FileNameLength;
    //STRING FileName;
};

//
// Spec in dfaft-leach-cifs-v1-spec-01.txt
struct SMB_QUERY_FILE_BASIC_INFO 
{
    LARGE_INTEGER CreationTime;
    LARGE_INTEGER LastAccessTime;
    LARGE_INTEGER LastWriteTime;
    LARGE_INTEGER ChangeTime;
    USHORT Attributes;   
};


struct SMB_READX_CLIENT_REQUEST  
{
    SMB_COM_ANDX_HEADER ANDX;
    USHORT FileID;
    ULONG  FileOffset;
    USHORT MaxCount;
    USHORT MinCount;
    ULONG  OpenTimeout;
    USHORT BytesLeft;
    USHORT ByteCount;  
};

struct SMB_READX_SERVER_RESPONSE 
{ 
    SMB_COM_ANDX_HEADER ANDX;
    USHORT  BytesLeft;
    ULONG   Reserved;
    USHORT  DataLength;
    USHORT  DataOffset;
    USHORT  Reserved1; 
    ULONG   Reserved2;
    ULONG   Reserved3; 
    USHORT  ByteCount;
};


//from SMBPUB.DOC
struct SMB_READ_CLIENT_REQUEST  
{
    UCHAR  WordCount;
    USHORT FileID;
    USHORT MaxCount;
    ULONG  FileOffset;    
    USHORT Remaining;
    USHORT ByteCount;  
};

//from SMBPUB.DOC
struct SMB_READ_SERVER_RESPONSE 
{
    UCHAR  WordCount;
    USHORT Count;
    USHORT Reserved[4];
    USHORT ByteCount;
    UCHAR  BufferFormat;
    USHORT DataLength;
};


#pragma pack()

#endif
