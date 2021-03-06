#include "condor_common.h"
#include "classad_collection.h"
#include "gahp-client.h"
#include "Functor.h"
#include "GetInstances.h"

const char * emptyStringIfNull( const char * str ) {
	if( strcmp( str, "NULL" ) == 0 ) { return ""; } else { return str; }
}

int
GetInstances::operator() () {
	dprintf( D_FULLDEBUG, "GetInstances::operator()\n" );

	std::string annexID;
	scratchpad->LookupString( "AnnexID", annexID );

	std::string errorCode;
	StringList returnStatus;
	int rc = gahp->ec2_vm_status_all(
		service_url, public_key_file, secret_key_file,
		// We can't filter on this until AWS supports SFR pass-through tagging.
		// "tag-key", "htcondor:AnnexName",
		returnStatus, errorCode
	);
	if( rc == GAHPCLIENT_COMMAND_NOT_SUBMITTED || rc == GAHPCLIENT_COMMAND_PENDING ) {
		// We expect to exit here the first time.
		return KEEP_STREAM;
	}

	if( rc == 0 ) {
		unsigned count = 0;
		std::string iName;

		int spotFleetRequestCount = 0;
		scratchpad->LookupInteger( "SpotFleetRequestCount", spotFleetRequestCount );

		std::string aName;
		std::string sfrID;
		std::set< std::string > spotFleetRequestIDs;
		for( int i = 0; i < spotFleetRequestCount; ++i ) {
			formatstr( aName, "SpotFleetRequest%d.ID", i );
			scratchpad->LookupString( aName.c_str(), sfrID );
			spotFleetRequestIDs.insert( sfrID );
		}

		returnStatus.rewind();
		ASSERT( returnStatus.number() % 8 == 0 );
		for( int i = 0; i < returnStatus.number(); i += 8 ) {
			std::string instanceID = returnStatus.next();
			std::string status = returnStatus.next();
			std::string clientToken = returnStatus.next();
			std::string keyName = returnStatus.next();
			std::string stateReasonCode = returnStatus.next();
			std::string publicDNSName = returnStatus.next();
			std::string spotFleetRequestID = emptyStringIfNull( returnStatus.next() );
			std::string annexName = emptyStringIfNull( returnStatus.next() );

			// An instance is in annex iff it has the 'htcondor:AnnexName'
			// tag OR -- until AWS gets its act together -- it has a
			// aws:ec2spot:fleet-request-id tag whose value is a SFR made
			// by annex (as identified by the client token).
			if( annexName.empty() && spotFleetRequestID.empty() ) {
				continue;
			}

			// We can also remove this when AWS allows, etc.
			if(! spotFleetRequestID.empty()) {
				if( annexName.empty() ) {
					annexName = "[spot requests]";
				}
			}

			// If we're looking for a specific annex's instances, filter.
			if(! annexID.empty()) {
				// Since SFR instances won't be tagged with their annex names
				// until AWS introduces that feature sometime in 2017, we
				// check and see if the instance is one belonging to a SFR
				// whose client token starts with the annex ID.
				if( annexName != annexID
				 && spotFleetRequestIDs.count( spotFleetRequestID ) == 0 ) {
					continue;
				}
			}

			formatstr( iName, "Instance%d", count++ );
			scratchpad->Assign( (iName + ".instanceID").c_str(), instanceID );
			scratchpad->Assign( (iName + ".status").c_str(), status );
			scratchpad->Assign( (iName + ".clientToken").c_str(), clientToken );
			scratchpad->Assign( (iName + ".keyName").c_str(), keyName );
			scratchpad->Assign( (iName + ".stateReasonCode").c_str(), stateReasonCode );
			scratchpad->Assign( (iName + ".publicDNSName").c_str(), publicDNSName );
			scratchpad->Assign( (iName + ".annexName").c_str(), annexName );
		}

		reply->Assign( ATTR_RESULT, getCAResultString( CA_SUCCESS ) );
		rc = PASS_STREAM;
	} else {
		std::string message;
		formatstr( message, "Failed to get instances: '%s' (%d): '%s'.",
			errorCode.c_str(), rc, gahp->getErrorString() );

		dprintf( D_ALWAYS, "%s\n", message.c_str() );
		reply->Assign( ATTR_RESULT, getCAResultString( CA_FAILURE ) );
		reply->Assign( ATTR_ERROR_STRING, message );
		rc = FALSE;
	}

	daemonCore->Reset_Timer( gahp->getNotificationTimerId(), 0, TIMER_NEVER );
	return rc;
}

int
GetInstances::rollback() {
	dprintf( D_FULLDEBUG, "GetInstances::rollback()\n" );

	daemonCore->Reset_Timer( gahp->getNotificationTimerId(), 0, TIMER_NEVER );
	return PASS_STREAM;
}
