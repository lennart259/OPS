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
#define NUM_NODES   200

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
        int totalNumNodes;
        string ownMACAddress;
        int ownNodeIndex;
        int nextAppID;
        int maximumCacheSize;
        double antiEntropyInterval;
        int maximumHopCount;
        double maximumRandomBackoffDuration;
        bool useTTL;
        int usedRNG;
        double cacheSizeReportingFrequency;
        double TimePerPacket;
        int maxNumPacketsInBuffer;
        int ackHopsToLive;
        int sortingMode;

        int ackTtl;

        int numEventsHandled;
        int currentCacheSize;

        int numPacketsTransmitted;
        int numTransmissionOpportunities;

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
            int finalDestinationNodeIndex;

            int goodnessValue;
            int hopsTravelled;

            int msgUniqueID;
            simtime_t initialInjectionTime;

            double createdTime;
            double updatedTime;
            double lastAccessedTime;

            double pathCost; // used for sorting the cache
            list<int> hopList;
        };

        struct SyncedNeighbour {
            string nodeMACAddress;
            double syncCoolOffEndTime;

            bool randomBackoffStarted;
            double randomBackoffEndTime;

            bool neighbourSyncing;
            double neighbourSyncEndTime;

            bool sendRoutingNext;
            bool sendACKNext;
            bool sendDataNext;

            bool activeTransmission;
            int packetsTransmitted;

            bool nodeConsidered;

        };

        /*// not needed here, because declared in KOPSMsg.msg
        struct PeerLikelihood {
            int nodeIndex;
            string nodeMACAddress;
            double likelihood;
        };
        */

        struct RoutingInfo {
            int nodeIndex;
            string nodeMACAddress;       // the node's own node ID
            vector<PeerLikelihood> peerLikelihoods; // other node ID's and path likelihoods
        };

        /*
        struct AckCacheEntry{
            int hopsToLive;
            int msgUniqueID;
        };
        */

        // local list holding all lists of peerLikelihoods for a respective node
        vector<RoutingInfo> routingInfoList;

        list<AppInfo*> registeredAppList;
        list<CacheEntry*> cacheList;
        list<CacheEntry*> cacheListSortPath;
        list<SyncedNeighbour*> syncedNeighbourList;
        list<Ack*> ackCacheList;
        double nodeGraph[NUM_NODES][NUM_NODES];
        double pathCosts[NUM_NODES];
        bool syncedNeighbourListIHasChanged;

        void ageDataInCache();
        void handleAppRegistrationMsg(cMessage *msg);
        void handleDataMsgFromUpperLayer(cMessage *msg);
        void handleNeighbourListMsgFromLowerLayer(cMessage *msg);
        void handleDataMsgFromLowerLayer(cMessage *msg);


        SyncedNeighbour* getSyncingNeighbourInfo(string nodeMACAddress);
        void setSyncingNeighbourInfoForNextRound();
        void setSyncingNeighbourInfoForNoNeighboursOrEmptyCache();
        KSummaryVectorMsg* makeSummaryVectorMessage();

        // new MaxProp
        void handleAckMsgFromLowerLayer(cMessage *msg);
        void handleRoutingInfoMsgFromLowerLayer(cMessage *msg);
        void handleLinkAckMsg(cMessage *msg);

        void sendRoutingInfoMessage(string destinationAddress);
        void sendAckVectorMessage(string destinationAddress);

        int sendDataDestinedToNeighbor(string nodeMACAddress);
        void sendDataMsgs(string nodeMACAddress);
        void createAndSendDataMessage(CacheEntry *cacheEntry, string destinationAddress);
        int macAddressToNodeIndex(string macAddress);
        void computePathCostsToFinalDest(int neighbourNodeIndex);
        int minDistance(double dist[], bool sptSet[]);
        void slowDijkstra(double graph[NUM_NODES][NUM_NODES], int src);
        void sortBuffer(int mode);
        void applyCachingPolicy(int mode);
        static bool compare_hopcount(const CacheEntry* first, const CacheEntry* second);
        static bool compare_pathcost (const CacheEntry *first, const CacheEntry *second);


        // stats related variables
        simsignal_t dataBytesReceivedSignal;
        simsignal_t sumVecBytesReceivedSignal;
        simsignal_t dataReqBytesReceivedSignal;
        simsignal_t totalBytesReceivedSignal;

        simsignal_t hopsTravelledSignal;
        simsignal_t hopsTravelledCountSignal;

        simsignal_t cacheBytesRemovedByAckSignal;
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

        simsignal_t receivedDuplicateMsgs;

};

#define KMAXPROPROUTINGLAYER_SIMMODULEINFO         " KMaxPropRoutingLayer>!<" << simTime() << ">!<" << getParentModule()->getFullName()

#define KMAXPROPROUTINGLAYER_MSG_ID_HASH_SIZE      4 // in bytes
#define KMAXPROPROUTINGLAYER_CACHESIZE_REP_EVENT   175

#endif /* KMAXPROPROUTINGLAYER_H_ */
