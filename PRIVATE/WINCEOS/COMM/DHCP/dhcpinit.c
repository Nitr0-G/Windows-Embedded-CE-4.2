//
// Copyright (c) Microsoft Corporation.  All rights reserved.
//
//
// This source code is licensed under Microsoft Shared Source License
// Version 1.0 for Windows CE.
// For a copy of the license visit http://go.microsoft.com/fwlink/?LinkId=3223.
//
/*++


Module Name:

    init.c

Abstract:

    This is the main routine for the DHCP client.

Environment:

    User Mode - Win32

Revision History:

--*/


#include <dhcpcli.h>
#include <stdio.h>

#ifndef VXD
#include <..\newnt\dhcploc.h>
#endif

//
// Internal function prototypes
//


DWORD
DhcpInitialize(
    LPDWORD SleepTime
    )
/*++

Routine Description:

    This function initializes DHCP.  It walks the work list looking for
    address to be acquired, or to renew.  It then links the DHCP context
    block for each card on the renewal list.

Arguments:

    None.

Return Value:

    minTimeToSleep - The time before the until DHCP must wake up to renew,
        or reattempt acquisition of an address.

    -1 - DHCP could not initialize, or no cards are configured for DHCP.

--*/
{
    DWORD Error;
    PDHCP_CONTEXT dhcpContext;

    PLIST_ENTRY listEntry;

    DWORD minTimeToSleep = (DWORD)-1;
    DWORD timeToSleep;

    //
    // Perform Global (common) variables initialization.
    //

#ifdef  __DHCP_DYNDNS_ENABLED__
#ifdef DBG
    // initialize the debug build of dnslib

    DnsStartDebug( 0, NULL, NULL, NULL, NULL );
#endif
#endif

    DhcpGlobalProtocolFailed = FALSE;

    //
    // Perform local initialization
    //

    Error = SystemInitialize();
    if ( Error != ERROR_SUCCESS ) {
        DhcpPrint(( DEBUG_ERRORS, "System initialize failed, %ld.\n", Error ));
        minTimeToSleep = (DWORD)-1;
        goto Cleanup;
    }

    //
    // ?? Introduce Random delay between 0 to 10 seconds
    // to desynchronize the use of DHCP at startup.
    //

    //
    // Loop through the list of installed cards, and attempt to renew
    // or acquire leases.
    //

    for ( listEntry = DhcpGlobalNICList.Flink;
            listEntry != &DhcpGlobalNICList;
                listEntry = listEntry->Flink ) {

        dhcpContext =
            CONTAINING_RECORD( listEntry, DHCP_CONTEXT, NicListEntry );


        DhcpPrint(( DEBUG_LEASE,
            "Old IpAddress %s\n",
                inet_ntoa(*(struct in_addr *)&dhcpContext->IpAddress) ));

        DhcpPrint(( DEBUG_LEASE,
            "Old lease obtained %s", ctime(&dhcpContext->LeaseObtained) ));

        DhcpPrint(( DEBUG_LEASE,
            "Old lease expires  %s", ctime(&dhcpContext->LeaseExpires) ));

        if ( DhcpIsInitState( dhcpContext ) )
        {

            Error = ReObtainInitialParameters( dhcpContext, &timeToSleep );

            if ( (Error != ERROR_SUCCESS) &&
                    (Error != ERROR_SEM_TIMEOUT) &&
                        (Error != ERROR_ACCESS_DENIED) &&
                            (Error != ERROR_DHCP_ADDRESS_CONFLICT ) ) {

                minTimeToSleep = (DWORD)-1;
                goto Cleanup;
            }

        } else {

            //
            // Otherwise verify lease.
            //

            Error = ReRenewParameters( dhcpContext, &timeToSleep );

            if ( (Error != ERROR_SUCCESS) &&
                    (Error != ERROR_SEM_TIMEOUT) &&
                        (Error != ERROR_ACCESS_DENIED) &&
                            ( Error != ERROR_DHCP_ADDRESS_CONFLICT )) {

                minTimeToSleep = (DWORD)-1;
                goto Cleanup;
            }
        }

#ifndef VXD

        UpdateStatus();     // send heart beat to SC.
#endif

        minTimeToSleep = MIN( minTimeToSleep, timeToSleep );

        //
        // this adapter has been processed by the dhcp client. flag the adapter
        // accordingly.
        //

#ifdef __DHCP_DYNDNS_ENABLED__
        dhcpContext->fFlags |= DHCP_CONTEXT_FLAGS_PROCESSED;
#endif

    }

#ifdef __DHCP_DYNDNS_ENABLED__

    //
    // if a dynamic dns update still needs to be done, do it now.  Normally,
    // the update would have been performed when the last dhcp adapter in the
    // list was processed above.  However, if processing for this adapter failed
    // for some reason ( e.g. the dhcp server couldn't be updated ) then we need
    // to do the update now.
    //

    




    if ( DhcpIsDynamicDNSEnabled() )
    {
        DhcpUpdateDns( NULL, FALSE );
    }
#endif

    Error = ERROR_SUCCESS;

Cleanup:

    if( SleepTime != NULL ) {
        *SleepTime = minTimeToSleep;
    }

#ifdef __DHCP_DYNDNS_ENABLED__
#ifdef DBG
    if ( ERROR_SUCCESS != Error )
    {
        DnsEndDebug();
    }
#endif
#endif


    return( Error );
}



DWORD
CalculateTimeToSleep(
    PDHCP_CONTEXT DhcpContext
    )
/*++

Routine Description:

    Calculate the amount of time to wait before sending a renewal,
    or new address request.

    Algorithm:

        //
        // ?? check retry times.
        //

        If Current-Ip-Address == 0
            TimeToSleep = ADDRESS_ALLOCATION_RETRY
            UseBroadcast = TRUE
        else if the lease is permanent
            TimeToSleep = INFINIT_LEASE
            UseBroadcast = TRUE
        else if the time now is < T1 time
            TimeToSleep = T1 - Now
            UseBroadcast = FALSE
        else  if the time now is < T2 time
            TimeToSleep = Min( 1/8 lease time, MIN_RETRY_TIME );
            UseBroadcast = FALSE
        else if the time now is < LeaseExpire
            TimeToSleep = Min( 1/8 lease time, MIN_RETRY_TIME );
            UseBroadcast = TRUE


Arguments:

    RenewalContext - A pointer to a renewal context.

Return Value:

    TimeToSleep - Returns the time to wait before sending the next
        DHCP request.

    This routine sets DhcpContext->DhcpServer to -1 if a broadcast
    should be used.

--*/
{
    time_t TimeNow;

    //
    // if there is no lease.
    //

    if ( DhcpContext->IpAddress == 0 ) {

        // DhcpContext->DhcpServerAddress = (DHCP_IP_ADDRESS)-1;
        return( ADDRESS_ALLOCATION_RETRY );
    }

    //
    // if the lease is permanent.
    //

    if ( DhcpContext->Lease == INFINIT_LEASE ) {

        return( INFINIT_TIME );
    }

    TimeNow = time( NULL );

    //
    // if the time is < T1
    //

    if( TimeNow < DhcpContext->T1Time ) {
        return ( DhcpContext->T1Time - TimeNow );
    }

    //
    // if the time is between T1 and T2.
    //

    if( TimeNow < DhcpContext->T2Time ) {

        //
        // wait half of the ramaining time but minimum of
        // MIN_TIME_SLEEP secs.
        //

        return( MAX( ((DhcpContext->T2Time - TimeNow) / 2),
                        MIN_SLEEP_TIME ) );
    }

    //
    // if the time is between T2 and LeaseExpire.
    //

    if( TimeNow < DhcpContext->LeaseExpires ) {
        DhcpContext->DhcpServerAddress = (DHCP_IP_ADDRESS)-1;

        //
        // wait half of the ramaining time but minimum of
        // MIN_SLEEP_TIME secs.
        //

        return( MAX( ((DhcpContext->LeaseExpires - TimeNow) / 2),
                        MIN_SLEEP_TIME ) );
    }

    //
    // Lease has Expired.
    //

    // DhcpContext->IpAddress = 0;
    return( MIN_SLEEP_TIME );
}


DWORD
InitializeDhcpSocket(
    SOCKET *Socket,
    DHCP_IP_ADDRESS IpAddress
    )
/*++

Routine Description:

    This function initializes and binds a socket to the specified IP address.

Arguments:

    Socket - Returns a pointer to the initialized socket.

    IpAddress - The IP address to bind the socket to.  It is legitimate
        to bind a socket to 0.0.0.0 if the card has no current IP address.

Return Value:

    The status of the operation.

--*/
{
    DWORD error;
    DWORD closeError;
    DWORD value;
    struct sockaddr_in socketName;
    DWORD i;
    SOCKET sock;

    //
    // Sockets initialization
    //

    sock = socket( PF_INET, SOCK_DGRAM, IPPROTO_UDP );

    if ( sock == INVALID_SOCKET ) {
        error = WSAGetLastError();
        DhcpPrint(( DEBUG_ERRORS, "socket failed, error = %ld\n", error ));
        return( error );
    }

    //
    // Make the socket share-able
    //

    value = 1;

    error = setsockopt( sock, SOL_SOCKET, SO_REUSEADDR, (char FAR *)&value, sizeof(value) );
    if ( error != 0 ) {
        error = WSAGetLastError();
        DhcpPrint((DEBUG_ERRORS, "setsockopt failed, err = %ld\n", error ));

        closeError = closesocket( sock );
        if ( closeError != 0 ) {
            DhcpPrint((DEBUG_ERRORS, "closesocket failed, err = %d\n", closeError ));
        }
        return( error );
    }

    error = setsockopt( sock, SOL_SOCKET, SO_BROADCAST, (char FAR *)&value, sizeof(value) );
    if ( error != 0 ) {
        error = WSAGetLastError();
        DhcpPrint((DEBUG_ERRORS, "setsockopt failed, err = %ld\n", error ));

        closeError = closesocket( sock );
        if ( closeError != 0 ) {
            DhcpPrint((DEBUG_ERRORS, "closesocket failed, err = %d\n", closeError ));
        }
        return( error );
    }

    //
    // If the IpAddress is zero, set the special socket option to make
    // stack work with zero address.
    //

#ifdef VXD
    if( IpAddress == 0 ) {
#else

    




    if( (IpAddress == 0 ) && (DhcpGlobalIsService == TRUE) ) {

#endif
        value = 1234;
        error = setsockopt( sock, SOL_SOCKET, 0x8000, (char FAR *)&value, sizeof(value) );
        if ( error != 0 ) {
            error = WSAGetLastError();
            DhcpPrint((DEBUG_ERRORS, "setsockopt failed, err = %ld\n", error ));

            closeError = closesocket( sock );
            if ( closeError != 0 ) {
                DhcpPrint((DEBUG_ERRORS, "closesocket failed, err = %d\n", closeError ));
            }
            return( error );
        }
    }

    socketName.sin_family = PF_INET;
    socketName.sin_port = htons( DHCP_CLIENT_PORT );
    socketName.sin_addr.s_addr = IpAddress;

    for ( i = 0; i < 8 ; i++ ) {
        socketName.sin_zero[i] = 0;
    }

    //
    // Bind this socket to the DHCP server port
    //

    error = bind(
               sock,
               (struct sockaddr FAR *)&socketName,
               sizeof( socketName )
               );

    if ( error != 0 ) {
        error = WSAGetLastError();
        DhcpPrint((DEBUG_ERRORS, "bind failed, err = %ld\n", error ));
        closeError = closesocket( sock );
        if ( closeError != 0 ) {
            DhcpPrint((DEBUG_ERRORS, "closesocket failed, err = %d\n", closeError ));
        }
        return( error );
    }

    *Socket = sock;
    return( NO_ERROR );
}


