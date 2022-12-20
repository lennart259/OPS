//
// The model implementation for the MAXPROP Routing layer
//
// @author :    Lennart Hinz, Julian Suendermann
//              Asanga Udugama (adu@comnets.uni-bremen.de)
// @date   : 02-may-2017
//
//

#ifndef KMAXPROPROUTINGLAYER_H_
#define KMAXPROPROUTINGLAYER_H_

#define TRUE                            1
#define FALSE                           0

#include <omnetpp.h>
#include <cstdlib>
#include <sstream>
#include <string>

#include "KOPSMsg_m.h"
#include "KInternalMsg_m.h"

using namespace omnetpp;

using namespace std;

class KMaxPropRoutingLayer : public cSimpleModule
{
    protected:
        virtual void initialize(int stage);
        virtual void handleMessage(cMessage *msg);
        virtual int numInitStages() const;
        virtual void finish();

    // todo: What is not needed for MaxProp?
    private:
        string ownMACAddress;
        int nextAppID;
        int maximumCacheSize;
        double antiEntropyInterval;
        int maximumHopCount;
        double maximumRandomBackoffDuration;
        bool useTTL;
        int usedRNG;
        double cacheSizeReportingFrequency;

        int ackHopsToLive;

        int numEventsHandled;
        int currentCacheSize;

        cMessage *cacheSizeReportingTimeoutEvent;

        struct AppInfo {
            int appID;
            string appName;
            string prefixName;
        };


        // DATA message cache entry
        struct CacheEntry {
            string messageID;
            int hopCount;

            string dataName;
            int realPayloadSize;
            string dummyPayloadContent;

            simtime_t validUntilTime;

            int realPacketSize;

            bool destinationOriented;
            string initialOriginatorAddress;
            string finalDestinationAddress;

            int goodnessValue;
            int hopsTravelled;

            int msgUniqueID;
            simtime_t initialInjectionTime;

            double createdTime;
            double updatedTime;
            double lastAccessedTime;
        };

        struct SyncedNeighbour {
            string nodeMACAddress;
            double syncCoolOffEndTime;

            bool randomBackoffStarted;
            double randomBackoffEndTime;

            bool neighbourSyncing;
            double neighbourSyncEndTime;

            bool nodeConsidered;

        };

        /*
        struct PeerLikelihood {
            string nodeMACAddress;
            double likelihood;
        };
        */

        struct RoutingInfo {
            string nodeMACAdress;       // the node's own node ID
            list<PeerLikelihood*> peerLikelihoods; // other node ID's and path likelihoods
        };

        struct AckCacheEntry{
            int hopsToLive;
            int msgUniqueID;
        };

        // local list holding all lists of peerLikelihoods for a respective node
        list<RoutingInfo*> routingInfoList; // idk if this works

        list<AppInfo*> registeredAppList;
        list<CacheEntry*> cacheList;
        list<SyncedNeighbour*> syncedNeighbourList;
        list<AckCacheEntry*> ackCacheList;
        bool syncedNeighbourListIHasChanged;

        void ageDataInCache();
        void handleAppRegistrationMsg(cMessage *msg);
        void handleDataMsgFromUpperLayer(cMessage *msg);
        void handleNeighbourListMsgFromLowerLayer(cMessage *msg);
        void handleDataMsgFromLowerLayer(cMessage *msg);
        void handleSummaryVectorMsgFromLowerLayer(cMessage *msg);
        void handleDataRequestMsgFromLowerLayer(cMessage *msg);

        void handleAckMsgFromLowerLayer(cMessage *msg);
        void handleRoutingInfoMsgFromLowerLayer(cMessage *msg);

        SyncedNeighbour* getSyncingNeighbourInfo(string nodeMACAddress);
        void setSyncingNeighbourInfoForNextRound();
        void setSyncingNeighbourInfoForNoNeighboursOrEmptyCache();
        KSummaryVectorMsg* makeSummaryVectorMessage();

        // stats related variables
        simsignal_t dataBytesReceivedSignal;
        simsignal_t sumVecBytesReceivedSignal;
        simsignal_t dataReqBytesReceivedSignal;
        simsignal_t totalBytesReceivedSignal;

        simsignal_t hopsTravelledSignal;
        simsignal_t hopsTravelledCountSignal;

        simsignal_t cacheBytesRemovedSignal;
        simsignal_t cacheBytesAddedSignal;
        simsignal_t cacheBytesUpdatedSignal;
        simsignal_t currentCacheSizeBytesSignal;
        simsignal_t currentCacheSizeReportedCountSignal;
        simsignal_t currentCacheSizeBytesPeriodicSignal;

        simsignal_t currentCacheSizeBytesSignal2;

        simsignal_t dataBytesSentSignal;
        simsignal_t sumVecBytesSentSignal;
        simsignal_t dataReqBytesSentSignal;
        simsignal_t totalBytesSentSignal;

};

#define KMAXPROPROUTINGLAYER_SIMMODULEINFO         " KMaxPropRoutingLayer>!<" << simTime() << ">!<" << getParentModule()->getFullName()

#define KMAXPROPROUTINGLAYER_MSG_ID_HASH_SIZE      4 // in bytes
#define KMAXPROPROUTINGLAYER_CACHESIZE_REP_EVENT   175

#endif /* KMAXPROPROUTINGLAYER_H_ */
