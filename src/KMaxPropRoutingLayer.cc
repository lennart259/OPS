// COPY of Epidemic, will be adjusted foe MaxProp
// The model implementation for the MaxProp Routing layer
//
// @author : Lennart Hinz, Julian Suendermann
//           Asanga Udugama (adu@comnets.uni-bremen.de),
//           Hai Thien Long Thai (fix 1, 2) (hthai@uni-bremen.de, thaihaithienlong@yahoo.com)
// @date   : 07-june-2022
//


#include "KMaxPropRoutingLayer.h"

Define_Module(KMaxPropRoutingLayer);

void KMaxPropRoutingLayer::initialize(int stage)
{
    if (stage == 0) {
        // get parameters
        totalNumNodes = getParentModule()->getParentModule()->par("numNodes");
        ownMACAddress = par("ownMACAddress").stringValue();
        ownNodeIndex = par("nodeIndex");
        nextAppID = 1;
        maximumCacheSize = par("maximumCacheSize");
        currentCacheSize = 0;
        antiEntropyInterval = par("antiEntropyInterval");
        maximumHopCount = par("maximumHopCount");
        maximumRandomBackoffDuration = par("maximumRandomBackoffDuration");
        useTTL = par("useTTL");
        usedRNG = par("usedRNG");
        cacheSizeReportingFrequency = par("cacheSizeReportingFrequency");
        numEventsHandled = 0;
        //TimePerPacket = par("TimePerPacket");
        ackTtl = par("ackTtl");

        int dataSizeInBytes = getParentModule()->getSubmodule("app")->par("dataSizeInBytes");
        int wirelessHeaderSize = getParentModule()->getSubmodule("link")->par("wirelessHeaderSize");
        double bandwidthBitRate = getParentModule()->getSubmodule("link")->par("bandwidthBitRate");
        TimePerPacket = (dataSizeInBytes+wirelessHeaderSize)/bandwidthBitRate*8;



        syncedNeighbourListIHasChanged = TRUE;

        // initialize routingInfo
        routingInfoList.reserve(totalNumNodes); // reserve max size of vector which could hold routing info of all nodes in the NW

        RoutingInfo ownRoutingInfo;
        ownRoutingInfo.nodeIndex = ownNodeIndex;
        ownRoutingInfo.nodeMACAddress = ownMACAddress;
        ownRoutingInfo.peerLikelihoods.reserve(totalNumNodes);

        routingInfoList.push_back(ownRoutingInfo); // own routing info is always routingInfoList[0]


    } else if (stage == 1) {


    } else if (stage == 2) {

        // create and setup cache size reporting trigger
        cacheSizeReportingTimeoutEvent = new cMessage("Cache Size Reporting Event");
        cacheSizeReportingTimeoutEvent->setKind(KMAXPROPROUTINGLAYER_CACHESIZE_REP_EVENT);
        scheduleAt(simTime() + cacheSizeReportingFrequency, cacheSizeReportingTimeoutEvent);

        // setup statistics signals
        dataBytesReceivedSignal = registerSignal("fwdDataBytesReceived");
        sumVecBytesReceivedSignal = registerSignal("fwdSumVecBytesReceived");
        dataReqBytesReceivedSignal = registerSignal("fwdDataReqBytesReceived");
        totalBytesReceivedSignal = registerSignal("fwdTotalBytesReceived");
        hopsTravelledSignal = registerSignal("fwdHopsTravelled");
        hopsTravelledCountSignal = registerSignal("fwdHopsTravelledCount");

        cacheBytesRemovedSignal = registerSignal("fwdCacheBytesRemoved");
        cacheBytesAddedSignal = registerSignal("fwdCacheBytesAdded");
        cacheBytesUpdatedSignal = registerSignal("fwdCacheBytesUpdated");
        currentCacheSizeBytesSignal = registerSignal("fwdCurrentCacheSizeBytes");
        currentCacheSizeReportedCountSignal = registerSignal("fwdCurrentCacheSizeReportedCount");
        currentCacheSizeBytesPeriodicSignal = registerSignal("fwdCurrentCacheSizeBytesPeriodic");

        currentCacheSizeBytesSignal2 = registerSignal("fwdCurrentCacheSizeBytes2");

        dataBytesSentSignal = registerSignal("fwdDataBytesSent");
        sumVecBytesSentSignal = registerSignal("fwdSumVecBytesSent");
        dataReqBytesSentSignal = registerSignal("fwdDataReqBytesSent");
        totalBytesSentSignal = registerSignal("fwdTotalBytesSent");

    } else {
        EV_FATAL << KMAXPROPROUTINGLAYER_SIMMODULEINFO << "Something is radically wrong in initialization \n";
    }
}

int KMaxPropRoutingLayer::numInitStages() const
{
    return 3;
}



/************************ handleMessage() *************************
 *
 * will be called each time, the omnet simulator throws an event.
 * contains a large conditional statement to determine the type of message
 * and then call a respective subfunction
 *
 * */
void KMaxPropRoutingLayer::handleMessage(cMessage *msg)
{
    cGate *gate;
    char gateName[64];

    numEventsHandled++;

    // age the data in the cache only if needed (e.g. a message arrived)
    if (useTTL)
        ageDataInCache();

    // self messages
    if (msg->isSelfMessage()) {
        if (msg->getKind() == KMAXPROPROUTINGLAYER_CACHESIZE_REP_EVENT) {

            // report cache size
            emit(currentCacheSizeBytesPeriodicSignal, currentCacheSize);

            // setup next cache size reporting trigger
            scheduleAt(simTime() + cacheSizeReportingFrequency, cacheSizeReportingTimeoutEvent);

        } else {
            EV_INFO << KMAXPROPROUTINGLAYER_SIMMODULEINFO << "Received unexpected self message" << "\n";
            delete msg;
        }

    // messages from other layers
    } else {

       // get message arrival gate name
        gate = msg->getArrivalGate();
        strcpy(gateName, gate->getName());

        // app registration message arrived from the upper layer (app layer)
        if (strstr(gateName, "upperLayerIn") != NULL && dynamic_cast<KRegisterAppMsg*>(msg) != NULL) {

            handleAppRegistrationMsg(msg);

        // data message arrived from the upper layer (app layer)
        } else if (strstr(gateName, "upperLayerIn") != NULL && dynamic_cast<KDataMsg*>(msg) != NULL) {

            handleDataMsgFromUpperLayer(msg);

        // neighbour list message arrived from the lower layer (link layer)
        } else if (strstr(gateName, "lowerLayerIn") != NULL && dynamic_cast<KNeighbourListMsg*>(msg) != NULL) {

            handleNeighbourListMsgFromLowerLayer(msg);

        // data message arrived from the lower layer (link layer)
        } else if (strstr(gateName, "lowerLayerIn") != NULL && dynamic_cast<KDataMsg*>(msg) != NULL) {

            handleDataMsgFromLowerLayer(msg);

        // summary vector message arrived from the lower layer (link layer)
        } else if (strstr(gateName, "lowerLayerIn") != NULL && dynamic_cast<KSummaryVectorMsg*>(msg) != NULL) {

            handleSummaryVectorMsgFromLowerLayer(msg);

        // data request message arrived from the lower layer (link layer)
        } else if (strstr(gateName, "lowerLayerIn") != NULL && dynamic_cast<KDataRequestMsg*>(msg) != NULL) {

            handleDataRequestMsgFromLowerLayer(msg);

        } else if (strstr(gateName, "lowerLayerIn") != NULL && dynamic_cast<KRoutingInfoMsg*>(msg) != NULL) {

            handleRoutingInfoMsgFromLowerLayer(msg);

        } else if (strstr(gateName, "lowerLayerIn") != NULL && dynamic_cast<KAckMsg*>(msg) != NULL) {

            handleAckMsgFromLowerLayer(msg);

        // received some unexpected packet
        } else {

            EV_INFO << KMAXPROPROUTINGLAYER_SIMMODULEINFO << "Received unexpected packet" << "\n";
            delete msg;
        }
    }
}


/* **************************ageDataInCache()*******************************
 *
 * iterates through cache list and deletes cache entries that are not
 * valid anymore (checked against simTime with the cache entry parameter validUntilTime
 *
 * */
void KMaxPropRoutingLayer::ageDataInCache()
{

    // remove expired data items
    int expiredFound = TRUE;
    while (expiredFound) {
        expiredFound = FALSE;

        CacheEntry *cacheEntry;
        list<CacheEntry*>::iterator iteratorCache;
        iteratorCache = cacheList.begin();
        while (iteratorCache != cacheList.end()) {
            cacheEntry = *iteratorCache;
            if (cacheEntry->validUntilTime < simTime().dbl()) {
                expiredFound = TRUE;
                break;
            }
            iteratorCache++;
        }
        if (expiredFound) {
            currentCacheSize -= cacheEntry->realPacketSize;

            emit(cacheBytesRemovedSignal, cacheEntry->realPayloadSize);
            emit(currentCacheSizeBytesSignal, currentCacheSize);
            emit(currentCacheSizeReportedCountSignal, (int) 1);

            emit(currentCacheSizeBytesSignal2, currentCacheSize);

            cacheList.remove(cacheEntry);
            delete cacheEntry;

        }
    }

}



/* **************************handleAppRegistrationMsg()**********************************
 *
 * Appends to registeredAppList, if new app is requested and not already included
 *
 * */
void KMaxPropRoutingLayer::handleAppRegistrationMsg(cMessage *msg)
{
    KRegisterAppMsg *regAppMsg = dynamic_cast<KRegisterAppMsg*>(msg);
    AppInfo *appInfo = NULL;
    int found = FALSE;
    list<AppInfo*>::iterator iteratorRegisteredApps = registeredAppList.begin();
    while (iteratorRegisteredApps != registeredAppList.end()) {
        appInfo = *iteratorRegisteredApps;
        if (appInfo->appName == regAppMsg->getAppName()) {
            found = TRUE;
            break;
        }
        iteratorRegisteredApps++;
    }

    if (!found) {
        appInfo = new AppInfo;
        appInfo->appID = nextAppID++;
        appInfo->appName = regAppMsg->getAppName();
        registeredAppList.push_back(appInfo);

    }
    appInfo->prefixName = regAppMsg->getPrefixName();
    delete msg;

}

/***************************handleDataMsgFromUpperLayer()************************
 *
 * is called, when the app generates a new datamessage for itself.
 * function checks, wether the message already exists in cache.
 * if not, new message is appended.
 * if cache size now exceeds maxCacheSize: delete oldest msg.
 * TODO: implement actual caching policy that is: sort messages according to maxprop protocol and then delete last msgs
 *
 */
void KMaxPropRoutingLayer::handleDataMsgFromUpperLayer(cMessage *msg)
{
    KDataMsg *omnetDataMsg = dynamic_cast<KDataMsg*>(msg);

    CacheEntry *cacheEntry;
    list<CacheEntry*>::iterator iteratorCache;
    int found = FALSE;
    iteratorCache = cacheList.begin();
    while (iteratorCache != cacheList.end()) {
        cacheEntry = *iteratorCache;
        if (cacheEntry->dataName == omnetDataMsg->getDataName()) {
            found = TRUE;
            break;
        }

        iteratorCache++;
    }

    if (!found) {

        // apply caching policy if limited cache and cache is full
        // TODO: implement caching policy function and replace this code (see also todo above in function description)
        if (maximumCacheSize != 0
                && (currentCacheSize + omnetDataMsg->getRealPayloadSize()) > maximumCacheSize
                && cacheList.size() > 0) {
            iteratorCache = cacheList.begin();
            CacheEntry *removingCacheEntry = *iteratorCache;
            iteratorCache = cacheList.begin();
            while (iteratorCache != cacheList.end()) {
                cacheEntry = *iteratorCache;
                if (cacheEntry->validUntilTime < removingCacheEntry->validUntilTime) {
                    removingCacheEntry = cacheEntry;
                }
                iteratorCache++;
            }
            currentCacheSize -= removingCacheEntry->realPayloadSize;

            emit(cacheBytesRemovedSignal, removingCacheEntry->realPayloadSize);
            emit(currentCacheSizeBytesSignal, currentCacheSize);
            emit(currentCacheSizeReportedCountSignal, (int) 1);

            emit(currentCacheSizeBytesSignal2, currentCacheSize);

            cacheList.remove(removingCacheEntry);
            delete removingCacheEntry;

        }

        cacheEntry = new CacheEntry;

        cacheEntry->messageID = omnetDataMsg->getDataName();
        cacheEntry->hopCount = 0;
        cacheEntry->dataName = omnetDataMsg->getDataName();
        cacheEntry->realPayloadSize = omnetDataMsg->getRealPayloadSize();
        cacheEntry->dummyPayloadContent = omnetDataMsg->getDummyPayloadContent();
        cacheEntry->validUntilTime = omnetDataMsg->getValidUntilTime();
        cacheEntry->realPacketSize = omnetDataMsg->getRealPacketSize();
        cacheEntry->initialOriginatorAddress = omnetDataMsg->getInitialOriginatorAddress();
        cacheEntry->destinationOriented = omnetDataMsg->getDestinationOriented();
        if (omnetDataMsg->getDestinationOriented()) {
            cacheEntry->finalDestinationAddress = omnetDataMsg->getFinalDestinationAddress();
            cacheEntry->finalDestinationNodeIndex = macAddressToNodeIndex(omnetDataMsg->getFinalDestinationAddress());
        }
        cacheEntry->goodnessValue = omnetDataMsg->getGoodnessValue();
        cacheEntry->hopsTravelled = 0;

        cacheEntry->msgUniqueID = omnetDataMsg->getMsgUniqueID();
        cacheEntry->initialInjectionTime = omnetDataMsg->getInitialInjectionTime();

        cacheEntry->createdTime = simTime().dbl();
        cacheEntry->updatedTime = simTime().dbl();

        cacheList.push_back(cacheEntry);

        currentCacheSize += cacheEntry->realPayloadSize;

    }

    cacheEntry->lastAccessedTime = simTime().dbl();

    // log cache update or add
    if (found) {
        emit(cacheBytesUpdatedSignal, cacheEntry->realPayloadSize);
    } else {
        emit(cacheBytesAddedSignal, cacheEntry->realPayloadSize);
    }
    emit(currentCacheSizeBytesSignal, currentCacheSize);
    emit(currentCacheSizeReportedCountSignal, (int) 1);

    emit(currentCacheSizeBytesSignal2, currentCacheSize);

    delete msg;
}

/*******************macAddressToNodeIndex()***************************
 * helper function to get a nodeIndex from MAC Address 02:00:00:00:00:01:02:03 -> 123
 */
int KMaxPropRoutingLayer::macAddressToNodeIndex(string macAddress){

    static int pow10[10] = {
        1, 10, 100, 1000, 10000,
        100000, 1000000, 10000000, 100000000, 1000000000
    };
    int outNodeIndex = 0;
    const char* delim = ":"; // use ':' as separator for the mac Address
    char* macStr = const_cast<char*>(macAddress.c_str());
    char *ptr;
    ptr = strtok(macStr, delim);
    int i = 5;

    while (ptr != NULL)
    {
        if(i < 5 && i >= 0) { // skip 1st part of MAC address
            outNodeIndex += stoi(ptr) * pow10[i];
        }
        ptr = strtok(NULL, delim);
        --i;
    }
    return outNodeIndex;
}


/**************************handleNeighbourListMsgFromLowerLayer()***************************
 *
 * periodically, we get the info about all neighbour nodes (nodes in wireless reach)
 *
 * each time, the function is called, we iterate through the new neighbourlist and determine,
 * if we need to sync with each respective neighbour or not
 *
 * TODO: Julian, sync prozess aendert sich sicher etwas
 */
void KMaxPropRoutingLayer::handleNeighbourListMsgFromLowerLayer(cMessage *msg)
{
    // todo LEN TEST
    // generate a dummy routing info
    string macAdresses[] = {"AAA", "BBB", "CCC"};
    list<PeerLikelihood*> peerLikelihoodList;
    for(int i = 0; i < 3; i++) {
        PeerLikelihood *pL = new PeerLikelihood();
        pL->nodeMACAddress = macAdresses[i];
        pL->likelihood = i;
        peerLikelihoodList.push_back(pL);
    }


    KRoutingInfoMsg *routingInfoMsg = new KRoutingInfoMsg();
    routingInfoMsg->setPeerLikelihoodsArraySize(3);

    list<PeerLikelihood*>::iterator iteratorPLList;
    int i = 0;
    iteratorPLList = peerLikelihoodList.begin();
    while (iteratorPLList != peerLikelihoodList.end()) {
        PeerLikelihood PL = **iteratorPLList;
        routingInfoMsg->setPeerLikelihoods(i, PL);
        iteratorPLList++;
        i++;
    }


    KNeighbourListMsg *neighListMsg = dynamic_cast<KNeighbourListMsg*>(msg);

    // if no neighbours or cache is empty, just return
    // todo
    if ((neighListMsg->getNeighbourNameListArraySize() == 0 || cacheList.size() == 0) && ackCacheList.size() == 0) {

        // setup sync neighbour list for the next time - only if there were some changes
        if (syncedNeighbourListIHasChanged) {
            setSyncingNeighbourInfoForNoNeighboursOrEmptyCache();
            syncedNeighbourListIHasChanged = FALSE;
        }

        delete msg;
        return;
    }

    // send summary vector messages (if appropriate) to all nodes to sync in a loop
    i = 0;
    EV << "neighbors: " << neighListMsg->getNeighbourNameListArraySize() << "\n";
    while (i < neighListMsg->getNeighbourNameListArraySize()) {
        string nodeMACAddress = neighListMsg->getNeighbourNameList(i);


        /*// routing info message test
        routingInfoMsg->setSourceAddress(ownMACAddress.c_str());
        routingInfoMsg->setDestinationAddress(nodeMACAddress.c_str());
        send(routingInfoMsg, "lowerLayerOut");

         */
        EV << ownMACAddress << " / nodeIndex " << ownNodeIndex << " is looking at Neighbour : " << nodeMACAddress.c_str() << "\n";
        // get syncing info of neighbor
        SyncedNeighbour *syncedNeighbour = getSyncingNeighbourInfo(nodeMACAddress);

        // indicate that this node was considered this time
        syncedNeighbour->nodeConsidered = TRUE;

        bool syncWithNeighbour = FALSE;
        bool isInSync = syncedNeighbour->sendRoutingNext || syncedNeighbour->sendDataNext;

        if (syncedNeighbour->syncCoolOffEndTime >= simTime().dbl() && not isInSync) {
            // if the sync was done recently, don't sync again until the anti-entropy interval
            // has elapsed
            syncWithNeighbour = FALSE;

        } else if (syncedNeighbour->randomBackoffStarted && syncedNeighbour->randomBackoffEndTime >= simTime().dbl()) {
            // if random backoff to sync is still active, then wait until time expires
            syncWithNeighbour = FALSE;

        } else if (syncedNeighbour->neighbourSyncing && syncedNeighbour->neighbourSyncEndTime >= simTime().dbl()) {
            // if this neighbour has started syncing with me, then wait until this neighbour finishes
            syncWithNeighbour = FALSE;

        } else if (syncedNeighbour->randomBackoffStarted && syncedNeighbour->randomBackoffEndTime < simTime().dbl()) {
            // has the random backoff just finished - if so, then my turn to start the syncing process
            syncWithNeighbour = TRUE;

        } else if (syncedNeighbour->neighbourSyncing && syncedNeighbour->neighbourSyncEndTime < simTime().dbl()) {
            // has the neighbours syncing period elapsed - if so, my turn to sync
            syncWithNeighbour = TRUE;

        } else {
            // neighbour seen for the first time (could also be after the cool off period)
            // then start the random backoff
            syncedNeighbour->randomBackoffStarted = TRUE;
            double randomBackoffDuration = uniform(1.0, maximumRandomBackoffDuration, usedRNG);
            syncedNeighbour->randomBackoffEndTime = simTime().dbl() + randomBackoffDuration;

            syncWithNeighbour = FALSE;

        }
        // from previous questions - if syncing required
        if (syncWithNeighbour) {
            if (syncedNeighbour->sendRoutingNext){
                // phase 2:
                // todo send routing info and Ack messages
                EV << ownMACAddress << ": Call sendRoutingInfoMessage from Neighbour handling, send to " << nodeMACAddress << "\n";
                sendRoutingInfoMessage(nodeMACAddress.c_str());
                syncedNeighbour->sendRoutingNext = FALSE;
                // syncedNeighbour->sendDataNext = TRUE;
            }
            else if (syncedNeighbour->sendDataNext){
                // phase 3:
                // todo send data
                EV << ownMACAddress << ": Send Data to " << nodeMACAddress.c_str() << "\n";
                sendDataMsgs(nodeMACAddress.c_str());
                syncedNeighbour->sendDataNext = FALSE;
            }
            else{
                // set the cooloff period
                syncedNeighbour->syncCoolOffEndTime = simTime().dbl() + antiEntropyInterval;
                EV << "Reset CoolOffEndTime next reset: " << syncedNeighbour->syncCoolOffEndTime << "\n";

                // initialize all other checks
                syncedNeighbour->randomBackoffStarted = FALSE;
                syncedNeighbour->randomBackoffEndTime = 0.0;
                syncedNeighbour->neighbourSyncing = FALSE;
                syncedNeighbour->neighbourSyncEndTime = 0.0;
                syncedNeighbour->sendRoutingNext = TRUE;
                syncedNeighbour->sendDataNext = FALSE;

                // // send summary vector (to start syncing)
                // KSummaryVectorMsg *summaryVectorMsg = makeSummaryVectorMessage();
                // summaryVectorMsg->setDestinationAddress(nodeMACAddress.c_str());
                // send(summaryVectorMsg, "lowerLayerOut");

                // todo phase detection
                // phase 1:
                // todo send packets destined to the neighbor
                EV << ownMACAddress << ": call sendDataDestinedToNeighbor(), send to " << nodeMACAddress << "\n";
                int numMsg = sendDataDestinedToNeighbor(nodeMACAddress);
                // todo if function: maximumRandomBackoffDuration only if numMsg==0 ???
                if (numMsg == 0)
                    EV << ownMACAddress << ": no messages in cache for peer " << nodeMACAddress << "\n";
                syncedNeighbour->neighbourSyncEndTime = simTime().dbl() + (numMsg+1)*TimePerPacket + maximumRandomBackoffDuration;
                EV << ownMACAddress << ": Set neighbourSyncEndTime next Part: " << syncedNeighbour->neighbourSyncEndTime << "\n";
                syncedNeighbour->neighbourSyncing = TRUE;

                // emit(sumVecBytesSentSignal, (long) summaryVectorMsg->getByteLength());
                // emit(totalBytesSentSignal, (long) summaryVectorMsg->getByteLength());
            }
        }

        i++;
    }

    // setup sync neighbour list for the next time
    setSyncingNeighbourInfoForNextRound();

    // synched neighbour list must be updated in next round
    // as there were changes
    syncedNeighbourListIHasChanged = TRUE;

    // delete the received neighbor list msg
    delete msg;
}


/* *********************handleDataMsgFromLowerLayer()********************
 *
 * Upon reception of a new DataMessage from a peer, we check if its destined to us.
 * If yes: pass to upper layer (to app)
 * If not, it is added to the local cache (and check for duplicates).
 * If maxSize is exceeded, apply caching policy
 *
 */
void KMaxPropRoutingLayer::handleDataMsgFromLowerLayer(cMessage *msg)
{
    KDataMsg *omnetDataMsg = dynamic_cast<KDataMsg*>(msg);
    bool found;

    // Proceed with sync Process
    string nodeBMacAddress = omnetDataMsg->getSourceAddress();
    SyncedNeighbour *syncedNeighbour = getSyncingNeighbourInfo(nodeBMacAddress.c_str());
    syncedNeighbour->neighbourSyncEndTime = simTime().dbl() + TimePerPacket;
    syncedNeighbour->neighbourSyncing = TRUE;
    EV << ownMACAddress << ": Set neighbourSyncEndTime next Part: " << syncedNeighbour->neighbourSyncEndTime << "\n";

    // increment the travelled hop count
    omnetDataMsg->setHopsTravelled(omnetDataMsg->getHopsTravelled() + 1);
    omnetDataMsg->setHopCount(omnetDataMsg->getHopCount() + 1);

    emit(dataBytesReceivedSignal, (long) omnetDataMsg->getByteLength());
    emit(totalBytesReceivedSignal, (long) omnetDataMsg->getByteLength());
    emit(hopsTravelledSignal, (long) omnetDataMsg->getHopsTravelled());
    emit(hopsTravelledCountSignal, 1);

    // if destination oriented data sent around and this node is the destination
    // or if maximum hop count is reached
    // then cache or else don't cache
    bool cacheData = TRUE;

    ///Fix 1: if this node is the destination, no caching, data passed directly to app layer
    if ((omnetDataMsg->getDestinationOriented() && strstr(ownMACAddress.c_str(), omnetDataMsg->getFinalDestinationAddress()) != NULL) || omnetDataMsg->getHopCount() >= maximumHopCount) {
    //if (omnetDataMsg->getHopCount() >= maximumHopCount) {

        cacheData = FALSE;
    }

    if(cacheData) {

        // insert/update cache
        CacheEntry *cacheEntry;
        list<CacheEntry*>::iterator iteratorCache;
        found = FALSE;
        iteratorCache = cacheList.begin();
        while (iteratorCache != cacheList.end()) {
            cacheEntry = *iteratorCache;
            if (cacheEntry->dataName == omnetDataMsg->getDataName()) {
                found = TRUE;
                break;
            }

            iteratorCache++;
        }

        if (!found) {

            // apply caching policy if limited cache and cache is full
            if (maximumCacheSize != 0
                && (currentCacheSize + omnetDataMsg->getRealPayloadSize()) > maximumCacheSize
                && cacheList.size() > 0) {
                iteratorCache = cacheList.begin();
                CacheEntry *removingCacheEntry = *iteratorCache;
                iteratorCache = cacheList.begin();
                while (iteratorCache != cacheList.end()) {
                    cacheEntry = *iteratorCache;
                    if (cacheEntry->validUntilTime < removingCacheEntry->validUntilTime) {
                        removingCacheEntry = cacheEntry;
                    }
                    iteratorCache++;
                }
                currentCacheSize -= removingCacheEntry->realPayloadSize;

                emit(cacheBytesRemovedSignal, removingCacheEntry->realPayloadSize);
                emit(currentCacheSizeBytesSignal, currentCacheSize);
                emit(currentCacheSizeReportedCountSignal, (int) 1);

                emit(currentCacheSizeBytesSignal2, currentCacheSize);

                cacheList.remove(removingCacheEntry);

                delete removingCacheEntry;
            }

            cacheEntry = new CacheEntry;

            cacheEntry->messageID = omnetDataMsg->getMessageID();
            cacheEntry->dataName = omnetDataMsg->getDataName();
            cacheEntry->realPayloadSize = omnetDataMsg->getRealPayloadSize();
            cacheEntry->dummyPayloadContent = omnetDataMsg->getDummyPayloadContent();
            cacheEntry->validUntilTime = omnetDataMsg->getValidUntilTime();
            cacheEntry->realPacketSize = omnetDataMsg->getRealPacketSize();
            cacheEntry->initialOriginatorAddress = omnetDataMsg->getInitialOriginatorAddress();
            cacheEntry->destinationOriented = omnetDataMsg->getDestinationOriented();
            if (omnetDataMsg->getDestinationOriented()) {
                cacheEntry->finalDestinationAddress = omnetDataMsg->getFinalDestinationAddress();
                cacheEntry->finalDestinationNodeIndex = (int)omnetDataMsg->getFinalDestinationNodeIndex();
            }
            cacheEntry->goodnessValue = omnetDataMsg->getGoodnessValue();

            cacheEntry->msgUniqueID = omnetDataMsg->getMsgUniqueID();
            cacheEntry->initialInjectionTime = omnetDataMsg->getInitialInjectionTime();

            cacheEntry->createdTime = simTime().dbl();
            cacheEntry->updatedTime = simTime().dbl();

            //copy hop list
             EV << ownMACAddress << ": received Data from " << omnetDataMsg->getSourceAddress() << "\n";
             EV << ownMACAddress << ": now caching new data; Hop List: \n";
             int hopIndex;
             vector<string> selectedMessageIDList;
             int i = 0;
             while (i < omnetDataMsg->getHopListArraySize()) {
                 hopIndex = omnetDataMsg->getHopList(i);
                 cacheEntry->hopList.push_back(hopIndex);
                 EV << "Entry " << i << ": " << hopIndex << "\n";
                 i++;
             }
             // add last hop (source MAC) to hopList
             cacheEntry->hopList.push_back(macAddressToNodeIndex(omnetDataMsg->getSourceAddress()));
             EV << "Newest Entry " << (i) << ": " << macAddressToNodeIndex(omnetDataMsg->getSourceAddress()) << " added to message " << omnetDataMsg->getMsgUniqueID() << "\n";

             cacheList.push_back(cacheEntry);

             currentCacheSize += cacheEntry->realPayloadSize;

        }

        cacheEntry->hopsTravelled = omnetDataMsg->getHopsTravelled();
        cacheEntry->hopCount = omnetDataMsg->getHopCount();
        cacheEntry->lastAccessedTime = simTime().dbl();

        // log cache update or add
        if (found) {
            emit(cacheBytesUpdatedSignal, cacheEntry->realPayloadSize);
        } else {
            emit(cacheBytesAddedSignal, cacheEntry->realPayloadSize);
        }
        emit(currentCacheSizeBytesSignal, currentCacheSize);
        emit(currentCacheSizeReportedCountSignal, (int) 1);

        emit(currentCacheSizeBytesSignal2, currentCacheSize);
    }

    // if registered app exist, send data msg to app
    AppInfo *appInfo = NULL;
    found = FALSE;
    list<AppInfo*>::iterator iteratorRegisteredApps = registeredAppList.begin();
    while (iteratorRegisteredApps != registeredAppList.end()) {
        appInfo = *iteratorRegisteredApps;
        if (strstr(omnetDataMsg->getDataName(), appInfo->prefixName.c_str()) != NULL
                && ((omnetDataMsg->getDestinationOriented()
                      && strstr(omnetDataMsg->getFinalDestinationAddress(), ownMACAddress.c_str()) != NULL)
                      || (!omnetDataMsg->getDestinationOriented()))) {
            found = TRUE;
            break;
        }
        iteratorRegisteredApps++;
    }
    if (found) {
        send(msg, "upperLayerOut");
        // create new ACK message and store in own cache.
        // only add ack if its not already in cache (we can receive the same message several times)
        Ack *ackCacheEntry;
        list<Ack*>::iterator iteratorAckCache;
        bool found = FALSE;
        iteratorAckCache = ackCacheList.begin();
        while (iteratorAckCache != ackCacheList.end()) {
            ackCacheEntry = *iteratorAckCache;
            if (ackCacheEntry->msgUniqueID == omnetDataMsg->getMsgUniqueID()) {
                found = TRUE;
                break;
            }

            iteratorAckCache++;
        }
        if (!found) {
            Ack *newAckCacheEntry = new Ack();
            newAckCacheEntry->msgUniqueID = omnetDataMsg->getMsgUniqueID();
            newAckCacheEntry->ttl = ackTtl;
            ackCacheList.push_back(newAckCacheEntry);
            EV << ownMACAddress << ": Added new ACK for message " << newAckCacheEntry->msgUniqueID << " to cache, with ttl of " << newAckCacheEntry->ttl << "\n";
        }
        else
            EV << ownMACAddress << ": Multiply received message: ACK is already in cache \n";


    } else {
        delete msg;
    }
}


/* ********************handleAckMsgFromLowerLayer()**************************
 *
 * upon reception of an ack message:
 * - search if ack has been seen before (if yes, do nothing, just keep the existing ack, dispose the new)
 * - in case of new ack:
 *   - store in ack cache
 *   - search data cache for uniqueID that was ack'd, if found, delete data cache entry.
 *
 * */
void KMaxPropRoutingLayer::handleAckMsgFromLowerLayer(cMessage *msg)
{
    KAckMsg *ackMsg = dynamic_cast<KAckMsg*>(msg);

    // todo: statistic for Ack bytes
    //emit(ackBytesReceivedSignal, (long) ackMsg->getByteLength());
    emit(totalBytesReceivedSignal, (long) ackMsg->getByteLength());

    EV << ownMACAddress << ": received ACK vector message from " << ackMsg->getSourceAddress() << "\n";
    // go through all Acks in Ack List
    for(int i = 0 ; i < ackMsg->getAckListArraySize() ; i++) {

        Ack ack = ackMsg->getAckList(i);
        int msgUniqueIDAckd = ack.msgUniqueID;

        // search own ACK cache, to see if this ack has been seen before (if yes, we have deleted a data entry already)
        Ack *ackCacheEntry;
        list<Ack*>::iterator iteratorAckCache;
        bool found = FALSE;
        iteratorAckCache = ackCacheList.begin();
        while (iteratorAckCache != ackCacheList.end()) {
            ackCacheEntry = *iteratorAckCache;
            if (ackCacheEntry->msgUniqueID == msgUniqueIDAckd) {
                found = TRUE;
                break;
            }

            iteratorAckCache++;
        }
        if (!found) {
            EV << ownMACAddress << ": Stored ACK for MsgID: " << msgUniqueIDAckd << "\n";
            // store ACK to propagate further
            // store ack'd ID in local cache
            // ack with ttl = 0 will not be stored, but we still search our cache once, if we find the ack'd package
            if(ack.ttl > 0) {
                Ack *newAckCacheEntry = new Ack();
                newAckCacheEntry->msgUniqueID = msgUniqueIDAckd;
                newAckCacheEntry->ttl = ack.ttl;
                ackCacheList.push_back(newAckCacheEntry);
            }

            // search for ack'd packet in own data cache
            CacheEntry *cacheEntry;
            list<CacheEntry*>::iterator iteratorCache;
            found = FALSE;
            iteratorCache = cacheList.begin();
            while (iteratorCache != cacheList.end()) {
                cacheEntry = *iteratorCache;
                if (cacheEntry->msgUniqueID == msgUniqueIDAckd) {
                    found = TRUE;
                    break;
                }
                iteratorCache++;
            }
            // delete delivered (ack'd) cache entry
            if (found) {
                EV << ownMACAddress << ": Found ACK'd message in own Cache... \n";
                currentCacheSize -= cacheEntry->realPacketSize;

                emit(cacheBytesRemovedSignal, cacheEntry->realPayloadSize);
                emit(currentCacheSizeBytesSignal, currentCacheSize);
                emit(currentCacheSizeReportedCountSignal, (int) 1);

                emit(currentCacheSizeBytesSignal2, currentCacheSize);

                cacheList.erase(iteratorCache);
                EV << ownMACAddress << ": Deleted message from cache, ID: " << msgUniqueIDAckd << " \n";
                delete cacheEntry;
            }

        }
    }

    // we're done with processing Acks now

    // Proceed with sync Process
    string nodeBMacAddress = ackMsg->getSourceAddress();
    SyncedNeighbour *syncedNeighbour = getSyncingNeighbourInfo(nodeBMacAddress.c_str());
    if (syncedNeighbour->sendDataNext){
        // we can send Data next
        sendDataMsgs(nodeBMacAddress.c_str());
        syncedNeighbour->sendDataNext = FALSE;
    }
    else{
        // routing info is send, but sendDataNext is False
        // -> we did not send Ack Vector so we send it now
        EV << ownMACAddress << ": Call sendAckVectorMessage from handleAckVectorMessage, send to " << nodeBMacAddress << "\n";
        sendAckVectorMessage(nodeBMacAddress.c_str());
        syncedNeighbour->sendDataNext = TRUE;
    }
    delete msg;
}

/* *********************sendAckVectorMessage(string destinationAddress)****************************
 *
 * sends out the current ack cache to a destination address.
 * before sending, decrease all ack ttl by one, to clear out cache afer time.
 * an ack is deleted from cache before sending, if the ttl is 0.
 *
 * */
void KMaxPropRoutingLayer::sendAckVectorMessage(string destinationAddress) {

    int ackListSize = ackCacheList.size();

    KAckMsg *ackMsg = new KAckMsg();

    ackMsg->setSourceAddress(ownMACAddress.c_str());
    ackMsg->setDestinationAddress(destinationAddress.c_str());
    ackMsg->setAckListArraySize(ackListSize);

    // before sending, we reduce the ttl of all list entries by 1
    Ack *ackCacheEntry;
    list<Ack*>::iterator iteratorAckCache;
    bool found = FALSE;
    iteratorAckCache = ackCacheList.begin();
    int i = 0;
    // reduce ttl in local Cache and add local cache entry to KAckMessage
    if(iteratorAckCache != ackCacheList.end())
        EV << ownMACAddress << ": Decrease ACK ttl before sending ACK Vector \n";
    else
        EV << ownMACAddress << ": Sending empty ACK Vector \n";

    while (iteratorAckCache != ackCacheList.end()) {
        ackCacheEntry = *iteratorAckCache;
        ackCacheEntry->ttl -= 1;
        ackMsg->setAckList(i, *ackCacheEntry);

        if(ackCacheEntry->ttl == 0) { // erase from own ACK list if ttl is expired
            EV << "Removing Ack cache entry " << i << " from cache, for msg " << ackCacheEntry->msgUniqueID << " with ttl of: " << ackCacheEntry->ttl << "\n";
            ackCacheList.erase(iteratorAckCache++);
        }
        else
            iteratorAckCache++;

        if(ackCacheList.size()==0) {break;}
        i++;
        EV << "Ack cache entry: " << i << " for msg " << ackCacheEntry->msgUniqueID << " has ttl of: " << ackCacheEntry->ttl << "\n";
    }
    send(ackMsg, "lowerLayerOut");
    EV << ownMACAddress << ": Ack vector msg was sent to: " << destinationAddress << "\n";

}

/**********************handleRoutingInfoMsgFromLowerLayer()*************************
 *
 * current node ("node A") receives a vector of peerLikelihoods from a known MacAddress ("node B")
 * node A searches in his own routingInfoList, if it already has a vector from node B
 * if yes: it replaces it, if no, it adds the new vector to the local list
 * node A keeps his own routingInfo at position 0 of his routingInfoList.
 * in his own routingInfo it searches for the MAC Address of node B,
 * if it is found: add 1 to the current likelihood and divide all entries by 2
 * if it is not found: add new entry containing 1 as likelihood, and divide all entries by 2
 * skip division by 2, if the added entry was the very first node encountered
 */
void KMaxPropRoutingLayer::handleRoutingInfoMsgFromLowerLayer(cMessage *msg) {
    KRoutingInfoMsg *routingInfoMsg = dynamic_cast<KRoutingInfoMsg*>(msg);



    // 0. Extract data from the message
    string nodeBMacAddress = routingInfoMsg->getSourceAddress();
    int nodeBIndex = routingInfoMsg->getSourceNodeIndex();
    EV << ownMACAddress << ": RoutingInfo was received from: " << nodeBMacAddress << "\n";

    // create new RoutingInfo object which we get out of the message
    RoutingInfo nodeBRoutingInfo;
    nodeBRoutingInfo.nodeMACAddress = nodeBMacAddress;
    nodeBRoutingInfo.nodeIndex = nodeBIndex;
    for(int i = 0; i < routingInfoMsg->getPeerLikelihoodsArraySize(); i++) {
        nodeBRoutingInfo.peerLikelihoods.push_back(routingInfoMsg->getPeerLikelihoods(i));
    }

    // 1. check if we have met the node before and already have its peerLikelihoods
    bool found = false;
    vector<RoutingInfo>::size_type index = 0;
    while(index != routingInfoList.size()) {
        if(routingInfoList[index].nodeIndex == nodeBIndex) {
            found = true;
            break;
        }
        index++;
    }
    if(found) { // copy the routingInfo to the existing location, replacing the old info
        routingInfoList[index] = nodeBRoutingInfo;
        EV << "we REPLACED routing info \n";
    }
    else { // add new routing info
        routingInfoList.push_back(nodeBRoutingInfo);
        EV << "we added new routing info \n";
    }


    // Update own peerLikelihoods based on meeting nodeB

    //search if we have encountered nodeB before.
    found = false;
    vector<PeerLikelihood>::size_type indexPl = 0;
    vector<PeerLikelihood>::size_type totalSizePl = routingInfoList[0].peerLikelihoods.size();
    while(indexPl != totalSizePl) {
        if(routingInfoList[0].peerLikelihoods[indexPl].nodeIndex == nodeBIndex) {
            found = true;
            break;
        }
        indexPl++;
    }
    if(found) { // update peerLikelihood
        routingInfoList[0].peerLikelihoods[indexPl].likelihood += 1;
        EV << "we increased peerLikelihood for peer " << nodeBMacAddress << " by one \n";
    }
    else { // add new peerLikelihood
        PeerLikelihood newPL;
        newPL.nodeMACAddress = nodeBMacAddress;
        newPL.nodeIndex = nodeBIndex;
        newPL.likelihood = 1.0;
        routingInfoList[0].peerLikelihoods.push_back(newPL);
        EV << ownMACAddress << ": we added new peer to own peerLikelihood list \n";
    }

    // only re-normalize, if the added entry was not the first one.
    totalSizePl = routingInfoList[0].peerLikelihoods.size();
    if(totalSizePl > 1 || found) {
        EV << ownMACAddress <<": Re-normalizing the local peerLikelihood list. \n";
        for(indexPl = 0; indexPl != totalSizePl; indexPl++) {
            routingInfoList[0].peerLikelihoods[indexPl].likelihood /= 2.0;
            EV << ownMACAddress << ": Node: " << routingInfoList[0].peerLikelihoods[indexPl].nodeMACAddress << ", likelihood: " << routingInfoList[0].peerLikelihoods[indexPl].likelihood << "\n";
        }
    }
    else {
        EV << ownMACAddress << ": Node: " << routingInfoList[0].peerLikelihoods[0].nodeMACAddress << ", likelihood: " << routingInfoList[0].peerLikelihoods[0].likelihood << "\n";
    }


    // Proceed with sync Process
    SyncedNeighbour *syncedNeighbour = getSyncingNeighbourInfo(nodeBMacAddress.c_str());
    if (syncedNeighbour->sendRoutingNext){
        EV << ownMACAddress << ": Call sendRoutingInfoMessage from handleRoutingInfoMessage, send to " << nodeBMacAddress << "\n";
        sendRoutingInfoMessage(nodeBMacAddress.c_str());
        syncedNeighbour->sendRoutingNext = FALSE;
    }
    else{
        //
        EV << ownMACAddress << ": Call sendAckVectorMessage from handleRoutingInfoMessage, send to " << nodeBMacAddress << "\n";
        sendAckVectorMessage(nodeBMacAddress.c_str());
        syncedNeighbour->sendDataNext = TRUE;
    }

    delete msg;
}


/**********************sendRoutingInfoMessage()*************************
 *
 * take own routing info which is stored at routingInfoList[0]
 * and send it as a routingInfoMsg (array-like peer likelihoods)
 *
 * */
void KMaxPropRoutingLayer::sendRoutingInfoMessage(string destinationAddress){


    KRoutingInfoMsg *routingInfoMsg = new KRoutingInfoMsg();

    vector<PeerLikelihood>::size_type totalSizePl = routingInfoList[0].peerLikelihoods.size();
    vector<PeerLikelihood>::size_type indexPl;
    routingInfoMsg->setSourceNodeIndex(ownNodeIndex);
    routingInfoMsg->setSourceAddress(ownMACAddress.c_str());
    routingInfoMsg->setDestinationAddress(destinationAddress.c_str());
    routingInfoMsg->setPeerLikelihoodsArraySize(totalSizePl);

    for(indexPl = 0; indexPl != totalSizePl; indexPl++) {
        PeerLikelihood pL;
        pL.nodeIndex = routingInfoList[0].peerLikelihoods[indexPl].nodeIndex;
        pL.nodeMACAddress = routingInfoList[0].peerLikelihoods[indexPl].nodeMACAddress;
        pL.likelihood = routingInfoList[0].peerLikelihoods[indexPl].likelihood;
        routingInfoMsg->setPeerLikelihoods(indexPl, pL);
        EV << "MAC: " << pL.nodeMACAddress << " NodeIndex: " << pL.nodeIndex << "\n";
        EV << "PL : " << pL.likelihood << "\n";
    }
    send(routingInfoMsg, "lowerLayerOut");
    EV << ownMACAddress << ": routing info was sent to: " << destinationAddress << "\n";
}


/***********************computePathCostsToFinalDest()***************************
 * computes the path cost to the final destination for all messages in cache,
 * upon meeting a neighbor
 * The delivery likelihood is stored as pathCost in the cacheEntries.
 */
void KMaxPropRoutingLayer::computePathCostsToFinalDest(int neighbourNodeIndex){
// iterate through cache. For each message look at their final destination,
// and compute the lowest path cost, if there are several possible paths starting with
// the current neighbour neighbourNodeIndex.

    CacheEntry *cacheEntry;
    list<CacheEntry*>::iterator iteratorCache;
    bool found = FALSE;
    iteratorCache = cacheList.begin();

    if(iteratorCache != cacheList.end())
        EV << ownMACAddress << ": computePathCosts: start iterating through cache. \n";
    else
        EV << ownMACAddress << ": computePathCosts: cache is empty. \n";
    int n = 0;
    while (iteratorCache != cacheList.end()) {
        cacheEntry = *iteratorCache;

        cacheEntry->pathCost = computePathCost(neighbourNodeIndex, cacheEntry->finalDestinationNodeIndex);

        iteratorCache++;
    }

}

/************************+computePathCost()****************************
 * uses the routing info list to compute the lowest path cost from startNodeIndex to destinationNodeIndex.
 * If the current routing info does not contain a possible path (destinationNodeIndex has never been seen),
 * the function will return the highest possible double value.
 *
 */
double KMaxPropRoutingLayer::computePathCost(int startNodeIndex, int destinationNodeIndex){
    return std::numeric_limits<double>::max();
}

// comparison, based on hopsTravelled
bool KMaxPropRoutingLayer::compare_hopcount (const CacheEntry *first, const CacheEntry *second)
{
  return ( first->hopsTravelled < second->hopsTravelled );
}

// comparison, based on pathCost
bool KMaxPropRoutingLayer::compare_pathcost (const CacheEntry *first, const CacheEntry *second)
{
  return ( first->pathCost < second->pathCost );
}

/********************sortBuffer()**************************
 *
 * sort the local buffer (cacheList) by criterion:
 * mode = 0: only hopcount
 * mode = 1: only peerLikelihood
 * todo: mode = 2: maxprop: split the buffer, first half is sorted by hopcount, 2nd half
 * is by peer likelihood, the splitpoint is dynamic.
 * */
void KMaxPropRoutingLayer::sortBuffer(int mode){
    switch(mode) {
    case 0: // sort only by hopcount
        cacheList.sort(compare_hopcount);
        break;
    case 1:
        cacheList.sort(compare_pathcost);
    default:
        break;
    }
}


KMaxPropRoutingLayer::SyncedNeighbour* KMaxPropRoutingLayer::getSyncingNeighbourInfo(string nodeMACAddress)
{
    // check if sync entry is there
    SyncedNeighbour *syncedNeighbour = NULL;
    list<SyncedNeighbour*>::iterator iteratorSyncedNeighbour;
    bool found = FALSE;
    iteratorSyncedNeighbour = syncedNeighbourList.begin();
    while (iteratorSyncedNeighbour != syncedNeighbourList.end()) {
        syncedNeighbour = *iteratorSyncedNeighbour;
        if (syncedNeighbour->nodeMACAddress == nodeMACAddress) {
            found = TRUE;
            break;
        }

        iteratorSyncedNeighbour++;
    }

    if (!found) {

        // if sync entry not there, create an entry with initial values
        syncedNeighbour = new SyncedNeighbour;

        syncedNeighbour->nodeMACAddress = nodeMACAddress.c_str();
        syncedNeighbour->syncCoolOffEndTime = 0.0;
        syncedNeighbour->randomBackoffStarted = FALSE;
        syncedNeighbour->randomBackoffEndTime = 0.0;
        syncedNeighbour->neighbourSyncing = FALSE;
        syncedNeighbour->neighbourSyncEndTime = 0.0;
        syncedNeighbour->nodeConsidered = FALSE;
        syncedNeighbour->sendRoutingNext = FALSE;
        syncedNeighbour->sendDataNext = FALSE;

        syncedNeighbourList.push_back(syncedNeighbour);
    }

    return syncedNeighbour;
}

void KMaxPropRoutingLayer::setSyncingNeighbourInfoForNextRound()
{
    // loop thru syncing neighbor list and set for next round
    list<SyncedNeighbour*>::iterator iteratorSyncedNeighbour;
    iteratorSyncedNeighbour = syncedNeighbourList.begin();
    while (iteratorSyncedNeighbour != syncedNeighbourList.end()) {
        SyncedNeighbour *syncedNeighbour = *iteratorSyncedNeighbour;

        if (!syncedNeighbour->nodeConsidered) {

            // if neighbour not considered this time, then it means the
            // neighbour was not in my neighbourhood - so init all flags and timers

            syncedNeighbour->randomBackoffStarted = FALSE;
            syncedNeighbour->randomBackoffEndTime = 0.0;
            syncedNeighbour->neighbourSyncing = FALSE;
            syncedNeighbour->neighbourSyncEndTime = 0.0;
        }

        // setup for next time
        syncedNeighbour->nodeConsidered = FALSE;

        iteratorSyncedNeighbour++;
    }
}

void KMaxPropRoutingLayer::setSyncingNeighbourInfoForNoNeighboursOrEmptyCache()
{
    // loop thru syncing neighbor list and set for next round
    list<SyncedNeighbour*>::iterator iteratorSyncedNeighbour;
    iteratorSyncedNeighbour = syncedNeighbourList.begin();
    while (iteratorSyncedNeighbour != syncedNeighbourList.end()) {
        SyncedNeighbour *syncedNeighbour = *iteratorSyncedNeighbour;
        syncedNeighbour->randomBackoffStarted = FALSE;
        syncedNeighbour->randomBackoffEndTime = 0.0;
        syncedNeighbour->neighbourSyncing = FALSE;
        syncedNeighbour->neighbourSyncEndTime = 0.0;
        syncedNeighbour->nodeConsidered = FALSE;

        iteratorSyncedNeighbour++;
    }
}

/********************sendDataMsgs(string destinationAddress)**************************
 *
 * sort the buffer and then send out all messages that the neighbor has not received yet,
 * i.e. that don't have the neighbor's MAC address in their hop list.
 *
 * */
void KMaxPropRoutingLayer::sendDataMsgs(string destinationAddress)
{
    // compute and store pathCosts for all messages in buffer for the current neighbor
    computePathCostsToFinalDest(macAddressToNodeIndex(destinationAddress));

    // sort Buffer
    EV << ownMACAddress << ": sendDataMsgs(): Sorting Buffer \n";
    sortBuffer(0);   // 0: sort by hopcount


    // iterate through the whole cacheList
    CacheEntry *cacheEntry;
    list<CacheEntry*>::iterator iteratorCache;
    bool found = FALSE;
    iteratorCache = cacheList.begin();

    if(iteratorCache != cacheList.end())
        EV << ownMACAddress << ": sendDataMsgs(): start iterating through cache. \n";
    else
        EV << ownMACAddress << ": sendDataMsgs(): cache is empty, no data to send. \n";
    int n = 0;
    while (iteratorCache != cacheList.end()) {
        cacheEntry = *iteratorCache;

        // iterate through the hop_list of the current message to find, if the packet should be sent to current neighbor
        n++;
        EV << "CacheEntry#" << n << ": msg#: " << cacheEntry->msgUniqueID << "; HopCount: " << cacheEntry->hopCount << " PathCost: " << cacheEntry->pathCost << "\n";
        int destinationNodeIndex = macAddressToNodeIndex(destinationAddress);
        list<int>::iterator iteratorHopList;
        bool found = FALSE;
        iteratorHopList = cacheEntry->hopList.begin();
        while (iteratorHopList != cacheEntry->hopList.end()) {
            if(*iteratorHopList == destinationNodeIndex) {
                EV << "    " << ownMACAddress << ": Neighbour " << destinationAddress << " is already in the hop list of message " << cacheEntry->msgUniqueID << "\n";
                found = TRUE;
                break;
            }
            iteratorHopList++;
        }

        if(!found) { // only send data message if neighbor was not found in hopList
            EV << "    " << ownMACAddress << ": Neighbour not found in hop list, sending data to " << destinationAddress << "\n";
            createAndSendDataMessage(cacheEntry, destinationAddress);
        }

        iteratorCache++;
    }
}


int KMaxPropRoutingLayer::sendDataDestinedToNeighbor(string destinationAddress)
{
    // sends all messages destined to the neighbor
    // returns number of sent messages
    // deletes these messages from the cache

    int sentMessages = 0;
    // iterate through the whole cacheList
    CacheEntry *cacheEntry;
    list<CacheEntry*>::iterator iteratorCache;
    bool found = FALSE;
    iteratorCache = cacheList.begin();
    while (iteratorCache != cacheList.end()) {
        cacheEntry = *iteratorCache;
        // check if cache entry is destination oriented and
        // the current neighbor is the final destination
        if((cacheEntry->destinationOriented && strstr(destinationAddress.c_str(), cacheEntry->finalDestinationAddress.c_str()) != NULL)) {
            EV << ownMACAddress << " starts sending Data Destined To Neighbor " << destinationAddress << "\n";
            createAndSendDataMessage(cacheEntry, destinationAddress);
            sentMessages++;
            // remove the cache entry from cache.
            cacheList.erase(iteratorCache++);
            delete cacheEntry; // todo?! why delete here
        }
        else
            iteratorCache ++;

        if (cacheList.size()==0){break;}

    }
    return sentMessages;
}

void KMaxPropRoutingLayer::createAndSendDataMessage(CacheEntry *cacheEntry, string destinationAddress) {
    KDataMsg *dataMsg = new KDataMsg();

    dataMsg->setSourceAddress(ownMACAddress.c_str());
    dataMsg->setDestinationAddress(destinationAddress.c_str());
    dataMsg->setDataName(cacheEntry->dataName.c_str());
    dataMsg->setDummyPayloadContent(cacheEntry->dummyPayloadContent.c_str());
    dataMsg->setValidUntilTime(cacheEntry->validUntilTime);
    dataMsg->setRealPayloadSize(cacheEntry->realPayloadSize);
    // check KOPSMsg.msg on sizing mssages
    int realPacketSize = 6 + 6 + 2 + cacheEntry->realPayloadSize + 4 + 6 + 1;
    dataMsg->setRealPacketSize(realPacketSize);
    dataMsg->setByteLength(realPacketSize);
    dataMsg->setInitialOriginatorAddress(cacheEntry->initialOriginatorAddress.c_str());
    dataMsg->setDestinationOriented(cacheEntry->destinationOriented);
    if (cacheEntry->destinationOriented) {
        dataMsg->setFinalDestinationAddress(cacheEntry->finalDestinationAddress.c_str());
        dataMsg->setFinalDestinationNodeIndex(cacheEntry->finalDestinationNodeIndex);
        //EV << ownMACAddress << ": sending Message with destination MAC: " << dataMsg->getFinalDestinationAddress() << " and node Index " << dataMsg->getFinalDestinationNodeIndex() << "\n";
    }
    dataMsg->setMessageID(cacheEntry->messageID.c_str());
    dataMsg->setHopCount(cacheEntry->hopCount);
    dataMsg->setGoodnessValue(cacheEntry->goodnessValue);
    dataMsg->setHopsTravelled(cacheEntry->hopsTravelled);
    dataMsg->setMsgUniqueID(cacheEntry->msgUniqueID);
    dataMsg->setInitialInjectionTime(cacheEntry->initialInjectionTime);

    // hop list: create array in message from list in cache entry
    dataMsg->setHopListArraySize(cacheEntry->hopList.size());

    list<int>::iterator iteratorHopList;
    iteratorHopList = cacheEntry->hopList.begin();
    int i = 0;
    while (iteratorHopList != cacheEntry->hopList.end()) {
        int hop = *iteratorHopList;
        dataMsg->setHopList(i, hop);
        iteratorHopList++;
        i++;
    }

    send(dataMsg, "lowerLayerOut");

    emit(dataBytesSentSignal, (long) dataMsg->getByteLength());
    emit(totalBytesSentSignal, (long) dataMsg->getByteLength());
}

KSummaryVectorMsg* KMaxPropRoutingLayer::makeSummaryVectorMessage()
{
    // identify the entries of the summary vector
    vector<string> selectedMessageIDList;
    CacheEntry *cacheEntry;
    list<CacheEntry*>::iterator iteratorCache;
    iteratorCache = cacheList.begin();
    while (iteratorCache != cacheList.end()) {
        cacheEntry = *iteratorCache;
        if ((cacheEntry->hopCount + 1) < maximumHopCount) {
            selectedMessageIDList.push_back(cacheEntry->messageID);
        }
        iteratorCache++;
    }

    // make a summary vector message
    KSummaryVectorMsg *summaryVectorMsg = new KSummaryVectorMsg();
    summaryVectorMsg->setSourceAddress(ownMACAddress.c_str());
    summaryVectorMsg->setMessageIDHashVectorArraySize(selectedMessageIDList.size());
    vector<string>::iterator iteratorMessageIDList;
    int i = 0;
    iteratorMessageIDList = selectedMessageIDList.begin();
    while (iteratorMessageIDList != selectedMessageIDList.end()) {
        string messageID = *iteratorMessageIDList;

        summaryVectorMsg->setMessageIDHashVector(i, messageID.c_str());

        i++;
        iteratorMessageIDList++;
    }
    int realPacketSize = 6 + 6 + (selectedMessageIDList.size() * KMAXPROPROUTINGLAYER_MSG_ID_HASH_SIZE);
    summaryVectorMsg->setRealPacketSize(realPacketSize);
    summaryVectorMsg->setByteLength(realPacketSize);

    return summaryVectorMsg;
}

void KMaxPropRoutingLayer::finish()
{

    recordScalar("numEventsHandled", numEventsHandled);

    // clear resgistered app list
    while (registeredAppList.size() > 0) {
        list<AppInfo*>::iterator iteratorRegisteredApp = registeredAppList.begin();
        AppInfo *appInfo= *iteratorRegisteredApp;
        registeredAppList.erase(iteratorRegisteredApp);
        delete appInfo;
    }

    // clear registered app list
    while (cacheList.size() > 0) {
        list<CacheEntry*>::iterator iteratorCache = cacheList.begin();
        CacheEntry *cacheEntry= *iteratorCache;
        cacheList.erase(iteratorCache);
        delete cacheEntry;
    }

    // clear synced neighbour info list
    list<SyncedNeighbour*> syncedNeighbourList;
    while (syncedNeighbourList.size() > 0) {
        list<SyncedNeighbour*>::iterator iteratorSyncedNeighbour = syncedNeighbourList.begin();
        SyncedNeighbour *syncedNeighbour = *iteratorSyncedNeighbour;
        syncedNeighbourList.remove(syncedNeighbour);
        delete syncedNeighbour;
    }

    // remove triggers
    cancelEvent(cacheSizeReportingTimeoutEvent);
    delete cacheSizeReportingTimeoutEvent;

}

void KMaxPropRoutingLayer::handleSummaryVectorMsgFromLowerLayer(cMessage *msg)
{
    KSummaryVectorMsg *summaryVectorMsg = dynamic_cast<KSummaryVectorMsg*>(msg);

    emit(sumVecBytesReceivedSignal, (long) summaryVectorMsg->getByteLength());
    emit(totalBytesReceivedSignal, (long) summaryVectorMsg->getByteLength());

    // when a summary vector is received, it means that the neighbour started the syncing
    // so send the data request message with the required data items


    // check and build a list of missing data items
    string messageID;
    vector<string> selectedMessageIDList;
    int i = 0;
    while (i < summaryVectorMsg->getMessageIDHashVectorArraySize()) {
        messageID = summaryVectorMsg->getMessageIDHashVector(i);

        // see if data item exist in cache
        CacheEntry *cacheEntry;
        list<CacheEntry*>::iterator iteratorCache;
        bool found = FALSE;
        iteratorCache = cacheList.begin();
        while (iteratorCache != cacheList.end()) {
            cacheEntry = *iteratorCache;
            if (cacheEntry->messageID == messageID) {
                found = TRUE;
                break;
            }

            iteratorCache++;
        }

        if (!found) {
            selectedMessageIDList.push_back(messageID);
        }
        i++;
    }

    // build a KDataRequestMsg with missing data items (i.e.,  message IDs)
    KDataRequestMsg *dataRequestMsg = new KDataRequestMsg();
    dataRequestMsg->setSourceAddress(ownMACAddress.c_str());
    dataRequestMsg->setDestinationAddress(summaryVectorMsg->getSourceAddress());
    int realPacketSize = 6 + 6 + (selectedMessageIDList.size() * KMAXPROPROUTINGLAYER_MSG_ID_HASH_SIZE);
    dataRequestMsg->setRealPacketSize(realPacketSize);
    dataRequestMsg->setByteLength(realPacketSize);
    dataRequestMsg->setMessageIDHashVectorArraySize(selectedMessageIDList.size());
    i = 0;
    vector<string>::iterator iteratorMessageIDList;
    iteratorMessageIDList = selectedMessageIDList.begin();
    while (iteratorMessageIDList != selectedMessageIDList.end()) {
        messageID = *iteratorMessageIDList;

        dataRequestMsg->setMessageIDHashVector(i, messageID.c_str());

        i++;
        iteratorMessageIDList++;
    }

    send(dataRequestMsg, "lowerLayerOut");

    emit(dataReqBytesSentSignal, (long) dataRequestMsg->getByteLength());
    emit(totalBytesSentSignal, (long) dataRequestMsg->getByteLength());


    // cancel the random backoff timer (because neighbour started syncing)
    string nodeMACAddress = summaryVectorMsg->getSourceAddress();
    SyncedNeighbour *syncedNeighbour = getSyncingNeighbourInfo(nodeMACAddress);
    syncedNeighbour->randomBackoffStarted = FALSE;
    syncedNeighbour->randomBackoffEndTime = 0.0;

    // second - start wait timer until neighbour has finished syncing
    syncedNeighbour->neighbourSyncing = TRUE;
    double delayPerDataMessage = 0.5; // assume 500 milli seconds per data message
    syncedNeighbour->neighbourSyncEndTime = simTime().dbl() + (selectedMessageIDList.size() * delayPerDataMessage);

    // synched neighbour list must be updated in next round
    // as there were changes
    syncedNeighbourListIHasChanged = TRUE;


    delete msg;
}

void KMaxPropRoutingLayer::handleDataRequestMsgFromLowerLayer(cMessage *msg)
{
    KDataRequestMsg *dataRequestMsg = dynamic_cast<KDataRequestMsg*>(msg);

    emit(dataReqBytesReceivedSignal, (long) dataRequestMsg->getByteLength());
    emit(totalBytesReceivedSignal, (long) dataRequestMsg->getByteLength());

    int i = 0;
    while (i < dataRequestMsg->getMessageIDHashVectorArraySize()) {
        string messageID = dataRequestMsg->getMessageIDHashVector(i);

        CacheEntry *cacheEntry;
        list<CacheEntry*>::iterator iteratorCache;
        bool found = FALSE;
        iteratorCache = cacheList.begin();
        while (iteratorCache != cacheList.end()) {
            cacheEntry = *iteratorCache;
            if (cacheEntry->messageID == messageID) {
                found = TRUE;
                break;
            }

            iteratorCache++;
        }

        if (found) {

            KDataMsg *dataMsg = new KDataMsg();

            dataMsg->setSourceAddress(ownMACAddress.c_str());
            dataMsg->setDestinationAddress(dataRequestMsg->getSourceAddress());
            dataMsg->setDataName(cacheEntry->dataName.c_str());
            dataMsg->setDummyPayloadContent(cacheEntry->dummyPayloadContent.c_str());
            dataMsg->setValidUntilTime(cacheEntry->validUntilTime);
            dataMsg->setRealPayloadSize(cacheEntry->realPayloadSize);
            // check KOPSMsg.msg on sizing mssages
            int realPacketSize = 6 + 6 + 2 + cacheEntry->realPayloadSize + 4 + 6 + 1;
            dataMsg->setRealPacketSize(realPacketSize);
            dataMsg->setByteLength(realPacketSize);
            dataMsg->setInitialOriginatorAddress(cacheEntry->initialOriginatorAddress.c_str());
            dataMsg->setDestinationOriented(cacheEntry->destinationOriented);
            if (cacheEntry->destinationOriented) {
                dataMsg->setFinalDestinationAddress(cacheEntry->finalDestinationAddress.c_str());
                dataMsg->setFinalDestinationNodeIndex(cacheEntry->finalDestinationNodeIndex);
            }
            dataMsg->setMessageID(cacheEntry->messageID.c_str());
            dataMsg->setHopCount(cacheEntry->hopCount);
            dataMsg->setGoodnessValue(cacheEntry->goodnessValue);
            dataMsg->setHopsTravelled(cacheEntry->hopsTravelled);
            dataMsg->setMsgUniqueID(cacheEntry->msgUniqueID);
            dataMsg->setInitialInjectionTime(cacheEntry->initialInjectionTime);

            send(dataMsg, "lowerLayerOut");

            emit(dataBytesSentSignal, (long) dataMsg->getByteLength());
            emit(totalBytesSentSignal, (long) dataMsg->getByteLength());


            ///Fix 2: remove cache entry after sending to destination
            if (strstr(cacheEntry->finalDestinationAddress.c_str(), dataRequestMsg->getSourceAddress()) != NULL
                    && cacheEntry->destinationOriented) {

                currentCacheSize -= cacheEntry->realPacketSize;

                emit(cacheBytesRemovedSignal, cacheEntry->realPayloadSize);
                emit(currentCacheSizeBytesSignal, currentCacheSize);
                emit(currentCacheSizeReportedCountSignal, (int) 1);

                emit(currentCacheSizeBytesSignal2, currentCacheSize);

                cacheList.erase(iteratorCache);
                delete cacheEntry;
            }
        }

        i++;
    }
    delete msg;
}

