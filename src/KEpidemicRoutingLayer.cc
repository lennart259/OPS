//
// The model implementation for the Epidemic Routing layer
//
// @author : Asanga Udugama (adu@comnets.uni-bremen.de)
// @date   : 02-may-2017
//
//

#include "KEpidemicRoutingLayer.h"

Define_Module(KEpidemicRoutingLayer);

void KEpidemicRoutingLayer::initialize(int stage)
{
    if (stage == 0) {
        // get parameters
        ownMACAddress = par("ownMACAddress").stringValue();
        nextAppID = 1;
        maximumCacheSize = par("maximumCacheSize");
        currentCacheSize = 0;
        antiEntropyInterval = par("antiEntropyInterval");
        maximumHopCount = par("maximumHopCount");
        maximumRandomBackoffDuration = par("maximumRandomBackoffDuration");

    } else if (stage == 1) {


    } else if (stage == 2) {

        // setup the trigger to age data
        ageDataTimeoutEvent = new cMessage("Age Data Timeout Event");
        ageDataTimeoutEvent->setKind(108);
        scheduleAt(simTime() + 1.0, ageDataTimeoutEvent);

    } else {
        EV_FATAL << KEPIDEMICROUTINGLAYER_SIMMODULEINFO << "Something is radically wrong in initialisation \n";
    }
}

int KEpidemicRoutingLayer::numInitStages() const
{
    return 3;
}

void KEpidemicRoutingLayer::handleMessage(cMessage *msg)
{
    cGate *gate;
    char gateName[64];
    KNeighbourListMsg *neighListMsg;

    // self messages
    if (msg->isSelfMessage()) {

        // age data trigger fired
        if (msg->getKind() == 108) {

            handleDataAgingTrigger(msg);

        } else {
            EV_INFO << KEPIDEMICROUTINGLAYER_SIMMODULEINFO << "Received unexpected self message" << "\n";
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

        // feedback message arrived from the upper layer (app layer)
        } else if (strstr(gateName, "upperLayerIn") != NULL && dynamic_cast<KFeedbackMsg*>(msg) != NULL) {

            // with Epidemic Routing, no feedback is considered

            delete msg;

        // neighbour list message arrived from the lower layer (link layer)
        } else if (strstr(gateName, "lowerLayerIn") != NULL && dynamic_cast<KNeighbourListMsg*>(msg) != NULL) {

            handleNeighbourListMsgFromLowerLayer(msg);

        // data message arrived from the lower layer (link layer)
        } else if (strstr(gateName, "lowerLayerIn") != NULL && dynamic_cast<KDataMsg*>(msg) != NULL) {

            handleDataMsgFromLowerLayer(msg);

        // feedback message arrived from the lower layer (link layer)
        } else if (strstr(gateName, "lowerLayerIn") != NULL && dynamic_cast<KFeedbackMsg*>(msg) != NULL) {

            // with Epidemic Routing, no feedback is considered
            delete msg;

        // summary vector message arrived from the lower layer (link layer)
        } else if (strstr(gateName, "lowerLayerIn") != NULL && dynamic_cast<KSummaryVectorMsg*>(msg) != NULL) {

            handleSummaryVectorMsgFromLowerLayer(msg);

        // data request message arrived from the lower layer (link layer)
        } else if (strstr(gateName, "lowerLayerIn") != NULL && dynamic_cast<KDataRequestMsg*>(msg) != NULL) {

            handleDataRequestMsgFromLowerLayer(msg);

        // received some unexpected packet
        } else {

            EV_INFO << KEPIDEMICROUTINGLAYER_SIMMODULEINFO << "Received unexpected packet" << "\n";
            delete msg;
        }
    }
}

void KEpidemicRoutingLayer::handleDataAgingTrigger(cMessage *msg)
{

    // remove expired data items
    int originalSize = cacheList.size();
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
            cacheList.remove(cacheEntry);
            delete cacheEntry;

        }
    }

    // setup next age data trigger
    scheduleAt(simTime() + 1.0, msg);

}

void KEpidemicRoutingLayer::handleAppRegistrationMsg(cMessage *msg)
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

void KEpidemicRoutingLayer::handleDataMsgFromUpperLayer(cMessage *msg)
{
    KDataMsg *omnetDataMsg = dynamic_cast<KDataMsg*>(msg);

    EV_INFO << KEPIDEMICROUTINGLAYER_SIMMODULEINFO << " :: " << ownMACAddress << " :: Upper In :: Data Msg :: " << omnetDataMsg->getSourceAddress() << " :: "
        << omnetDataMsg->getDestinationAddress() << " :: " << omnetDataMsg->getDataName() << " :: " << omnetDataMsg->getGoodnessValue() <<"\n";

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
        if (maximumCacheSize != 0
                && (currentCacheSize + omnetDataMsg->getRealPacketSize()) > maximumCacheSize
                && cacheList.size() > 0) {
            iteratorCache = cacheList.begin();
            advance(iteratorCache, 0);
            CacheEntry *removingCacheEntry = *iteratorCache;
            iteratorCache = cacheList.begin();
            while (iteratorCache != cacheList.end()) {
                cacheEntry = *iteratorCache;
                if (cacheEntry->validUntilTime < removingCacheEntry->validUntilTime) {
                    removingCacheEntry = cacheEntry;
                }
                iteratorCache++;
            }
            currentCacheSize -= removingCacheEntry->realPacketSize;
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
        cacheEntry->originatorNodeName = omnetDataMsg->getOriginatorNodeName();
        cacheEntry->destinationOriented = omnetDataMsg->getDestinationOriented();
        if (omnetDataMsg->getDestinationOriented()) {
            cacheEntry->finalDestinationNodeName = omnetDataMsg->getFinalDestinationNodeName();
        }
        cacheEntry->goodnessValue = omnetDataMsg->getGoodnessValue();
        cacheEntry->createdTime = simTime().dbl();
        cacheEntry->updatedTime = simTime().dbl();

        cacheList.push_back(cacheEntry);

        currentCacheSize += cacheEntry->realPacketSize;

    }

    cacheEntry->lastAccessedTime = simTime().dbl();

    delete msg;
}

void KEpidemicRoutingLayer::handleNeighbourListMsgFromLowerLayer(cMessage *msg)
{
    KNeighbourListMsg *neighListMsg = dynamic_cast<KNeighbourListMsg*>(msg);

    EV_INFO << KEPIDEMICROUTINGLAYER_SIMMODULEINFO << " :: Got neighbourhood msg :: Neigh Count :: " <<
                        neighListMsg->getNeighbourNameListArraySize() << " :: Cache Size :: "
                            << cacheList.size() << "\n";

    // cout << " me " << ownMACAddress << " got neighbourhood msg with " << neighListMsg->getNeighbourNameListArraySize() << " neighbours, cache "
    //     << cacheList.size() << " - " << simTime().dbl() <<"\n";

    // if no neighbours or cache is empty, just return
    if (neighListMsg->getNeighbourNameListArraySize() == 0 || cacheList.size() == 0) {

        // setup sync neighbour list for the next time
        setSyncingNeighbourInfoForNoNeighboursOrEmptyCache();

        delete msg;
        return;
    }

    // make a summary vector msg that can be replicated to send to each of the neighbors
    KSummaryVectorMsg *baseSummaryVectorMsg = makeSummaryVectorMessage();

    // send summary vector messages (if appropriate) to all nodes to sync in a loop
    int i = 0;
    while (i < neighListMsg->getNeighbourNameListArraySize()) {
        string nodeMACAddress = neighListMsg->getNeighbourNameList(i);

        // get syncing info of neighbor
        SyncedNeighbour *syncedNeighbour = getSyncingNeighbourInfo(nodeMACAddress);

        // indicate that this node was considered this time
        syncedNeighbour->nodeConsidered = TRUE;

        bool syncWithNeighbour = FALSE;

        if (syncedNeighbour->syncCoolOffEndTime >= simTime().dbl()) {
            // if the sync was done recently, don't sync again until the anti-entropy interval
            // has elapsed
            syncWithNeighbour = FALSE;

            // cout << " me " << ownMACAddress << " in cool off with " << nodeMACAddress << " cool off time " << syncedNeighbour->syncCoolOffEndTime
            //     << " - " << simTime().dbl() << "\n";

        } else if (syncedNeighbour->randomBackoffStarted && syncedNeighbour->randomBackoffEndTime >= simTime().dbl()) {
            // if random backoff to sync is still active, then wait until time expires
            syncWithNeighbour = FALSE;

            // cout << " me " << ownMACAddress << " in random backoff with " << nodeMACAddress << " - " << simTime().dbl() << "\n";


        } else if (syncedNeighbour->neighbourSyncing && syncedNeighbour->neighbourSyncEndTime >= simTime().dbl()) {
            // if this neighbour has started syncing with me, then wait until this neighbour finishes
            syncWithNeighbour = FALSE;

            // cout << " me " << ownMACAddress << " waiting to sync with " << nodeMACAddress << " - " << simTime().dbl() << "\n";


        } else if (syncedNeighbour->randomBackoffStarted && syncedNeighbour->randomBackoffEndTime < simTime().dbl()) {
            // has the random backoff just finished - if so, then my turn to start the syncing process
            syncWithNeighbour = TRUE;

            // cout << " me " << ownMACAddress << " sending summ vec to " << nodeMACAddress << " because random backoff ended - " << simTime().dbl() << "\n";

        } else if (syncedNeighbour->neighbourSyncing && syncedNeighbour->neighbourSyncEndTime < simTime().dbl()) {
            // has the neighbours syncing period elapsed - if so, my turn to sync
            syncWithNeighbour = TRUE;

            // cout << " me " << ownMACAddress << " sending summ vec to " << nodeMACAddress << " because neighbour finished syncing - " << simTime().dbl() << "\n";

        } else {
            // neighbour seen for the first time (could also be after the cool off period)
            // then start the random backoff
            syncedNeighbour->randomBackoffStarted = TRUE;
            double randomBackoffDuration = uniform(1.0, maximumRandomBackoffDuration);
            syncedNeighbour->randomBackoffEndTime = simTime().dbl() + randomBackoffDuration;

            syncWithNeighbour = FALSE;

            // cout << " me " << ownMACAddress << " starting random backoff with " << nodeMACAddress << " for " << randomBackoffDuration << " sec - " << simTime().dbl() << "\n";
        }

        // from previous questions - if syncing required
        if (syncWithNeighbour) {

            // set the cooloff period
            syncedNeighbour->syncCoolOffEndTime = simTime().dbl() + antiEntropyInterval;

            // initialize all other checks
            syncedNeighbour->randomBackoffStarted = FALSE;
            syncedNeighbour->randomBackoffEndTime = 0.0;
            syncedNeighbour->neighbourSyncing = FALSE;
            syncedNeighbour->neighbourSyncEndTime = 0.0;

            // send summary vector (to start syncing)
            KSummaryVectorMsg *summaryVectorMsg = baseSummaryVectorMsg->dup();
            summaryVectorMsg->setDestinationAddress(nodeMACAddress.c_str());
            send(summaryVectorMsg, "lowerLayerOut");

            EV_INFO << KEPIDEMICROUTINGLAYER_SIMMODULEINFO << " :: " << ownMACAddress << " :: Lower Out :: Summary Vector Msg :: "
                << summaryVectorMsg->getSourceAddress() << " :: " << summaryVectorMsg->getDestinationAddress()
                    << " :: Cached Entries " << summaryVectorMsg->getMessageIDHashVectorArraySize() << "\n";

            // cout << " me " << ownMACAddress << " sending actual summ vec to " << nodeMACAddress << " now - " << simTime().dbl() << "\n";
        }

        i++;
    }

    // setup sync neighbour list for the next time
    setSyncingNeighbourInfoForNextRound();

    // remove the original summary vector msg (from which duplicates were made)
    delete baseSummaryVectorMsg;

    // delete the received neighbor list msg
    delete msg;
}

void KEpidemicRoutingLayer::handleDataMsgFromLowerLayer(cMessage *msg)
{
    KDataMsg *omnetDataMsg = dynamic_cast<KDataMsg*>(msg);

    // cout << " me " << ownMACAddress << " got data from " << omnetDataMsg->getSourceAddress() << " - " << simTime().dbl() << "\n";

    bool found;

    EV_INFO << KEPIDEMICROUTINGLAYER_SIMMODULEINFO << " :: " << ownMACAddress << " :: Lower In :: Data Msg :: " << omnetDataMsg->getSourceAddress() << " :: "
        << omnetDataMsg->getDestinationAddress() << " :: " << omnetDataMsg->getDataName() << " :: " << omnetDataMsg->getGoodnessValue() << "\n";

    // if destination oriented data sent around and this node is the destination
    // or if maximum hop count is reached
    // then cache or else don't cache
    bool cacheData = TRUE;
    if ((omnetDataMsg->getDestinationOriented()
         && strstr(getParentModule()->getFullName(), omnetDataMsg->getFinalDestinationNodeName()) != NULL)
        || omnetDataMsg->getHopCount() >= maximumHopCount) {

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
                && (currentCacheSize + omnetDataMsg->getRealPacketSize()) > maximumCacheSize
                && cacheList.size() > 0) {
                iteratorCache = cacheList.begin();
                advance(iteratorCache, 0);
                CacheEntry *removingCacheEntry = *iteratorCache;
                iteratorCache = cacheList.begin();
                while (iteratorCache != cacheList.end()) {
                    cacheEntry = *iteratorCache;
                    if (cacheEntry->validUntilTime < removingCacheEntry->validUntilTime) {
                        removingCacheEntry = cacheEntry;
                    }
                    iteratorCache++;
                }
                currentCacheSize -= removingCacheEntry->realPacketSize;
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
            cacheEntry->originatorNodeName = omnetDataMsg->getOriginatorNodeName();
            cacheEntry->destinationOriented = omnetDataMsg->getDestinationOriented();
            if (omnetDataMsg->getDestinationOriented()) {
                cacheEntry->finalDestinationNodeName = omnetDataMsg->getFinalDestinationNodeName();
            }
            cacheEntry->goodnessValue = omnetDataMsg->getGoodnessValue();
            cacheEntry->createdTime = simTime().dbl();
            cacheEntry->updatedTime = simTime().dbl();

            cacheList.push_back(cacheEntry);

            currentCacheSize += cacheEntry->realPacketSize;

        } else {

        }

        cacheEntry->hopCount = omnetDataMsg->getHopCount() + 1;
        cacheEntry->lastAccessedTime = simTime().dbl();
    }

    // if registered app exist, send data msg to app
    AppInfo *appInfo = NULL;
    found = FALSE;
    list<AppInfo*>::iterator iteratorRegisteredApps = registeredAppList.begin();
    while (iteratorRegisteredApps != registeredAppList.end()) {
        appInfo = *iteratorRegisteredApps;
        if (strstr(omnetDataMsg->getDataName(), appInfo->prefixName.c_str()) != NULL) {
            found = TRUE;
            break;
        }
        iteratorRegisteredApps++;
    }
    if (found) {
        send(msg, "upperLayerOut");

        EV_INFO << KEPIDEMICROUTINGLAYER_SIMMODULEINFO << " :: " << ownMACAddress << " :: Upper Out :: Data Msg :: " << omnetDataMsg->getSourceAddress() << " :: "
            << omnetDataMsg->getDestinationAddress() << " :: " << omnetDataMsg->getDataName() << " :: " << omnetDataMsg->getGoodnessValue() << "\n";

    } else {
        delete msg;
    }
}

void KEpidemicRoutingLayer::handleSummaryVectorMsgFromLowerLayer(cMessage *msg)
{
    KSummaryVectorMsg *summaryVectorMsg = dynamic_cast<KSummaryVectorMsg*>(msg);

    // cout << " me " << ownMACAddress << " got summ vec from " << summaryVectorMsg->getSourceAddress() << " - " << simTime().dbl() << "\n";

    EV_INFO << KEPIDEMICROUTINGLAYER_SIMMODULEINFO << " :: " << ownMACAddress << " :: Lower In :: Summary Vector Msg :: " << summaryVectorMsg->getSourceAddress() << " :: "
        << summaryVectorMsg->getDestinationAddress() << "\n";

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
    int realPacketSize = 6 + 6 + (selectedMessageIDList.size() * KEPIDEMICROUTINGLAYER_MSG_ID_HASH_SIZE);
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

    EV_INFO << KEPIDEMICROUTINGLAYER_SIMMODULEINFO << " :: " << ownMACAddress << " :: Lower Out :: Data Request Msg :: " << dataRequestMsg->getSourceAddress() << " :: "
        << dataRequestMsg->getDestinationAddress() << "\n";


    // cancel the random backoff timer (because neighbour started syncing)
    string nodeMACAddress = summaryVectorMsg->getSourceAddress();
    SyncedNeighbour *syncedNeighbour = getSyncingNeighbourInfo(nodeMACAddress);
    // if (syncedNeighbour->randomBackoffStarted) {
    //     cout << " me " << ownMACAddress << " stopping random backoff with " << summaryVectorMsg->getSourceAddress() << " - " << simTime().dbl() << "\n";
    // }
    syncedNeighbour->randomBackoffStarted = FALSE;
    syncedNeighbour->randomBackoffEndTime = 0.0;

    // second - start wait timer until neighbour has finished syncing
    syncedNeighbour->neighbourSyncing = TRUE;
    double delayPerDataMessage = 0.5; // assume 500 milli seconds per data message
    syncedNeighbour->neighbourSyncEndTime = simTime().dbl() + (selectedMessageIDList.size() * delayPerDataMessage);

    // cout << " me " << ownMACAddress << " waiting to start sync with "
    //     << summaryVectorMsg->getSourceAddress() << " for " << (selectedMessageIDList.size() * delayPerDataMessage) << " sec - " << simTime().dbl() << "\n";


    delete msg;
}

void KEpidemicRoutingLayer::handleDataRequestMsgFromLowerLayer(cMessage *msg)
{
    KDataRequestMsg *dataRequestMsg = dynamic_cast<KDataRequestMsg*>(msg);


    EV_INFO << KEPIDEMICROUTINGLAYER_SIMMODULEINFO << " :: " << ownMACAddress << " :: Lower In :: Data Request Msg :: " << dataRequestMsg->getSourceAddress() << " :: "
        << dataRequestMsg->getDestinationAddress() << "\n";

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
            dataMsg->setOriginatorNodeName(cacheEntry->originatorNodeName.c_str());
            dataMsg->setDestinationOriented(cacheEntry->destinationOriented);
            if (cacheEntry->destinationOriented) {
                dataMsg->setFinalDestinationNodeName(cacheEntry->finalDestinationNodeName.c_str());
            }
            dataMsg->setMessageID(cacheEntry->messageID.c_str());
            dataMsg->setHopCount(cacheEntry->hopCount);
            dataMsg->setGoodnessValue(cacheEntry->goodnessValue);

            send(dataMsg, "lowerLayerOut");

            EV_INFO << KEPIDEMICROUTINGLAYER_SIMMODULEINFO << " :: " << ownMACAddress << " :: Lower Out :: Data Msg :: " << dataMsg->getSourceAddress() << " :: "
                << dataMsg->getDestinationAddress() << "\n";


            // EV_INFO << KEPIDEMICROUTINGLAYER_SIMMODULEINFO << KEPIDEMICROUTINGLAYER_DEBUG << " :: Sending Requested Data :: "
            //     << cacheEntry->dataName << " :: To :: " << dataRequestMsg->getSourceAddress() << "\n";

        }

        i++;
    }

    // cout << " me " << ownMACAddress << " got data request from " << dataRequestMsg->getSourceAddress() << " - " << simTime().dbl() << "\n";

    delete msg;
}

KEpidemicRoutingLayer::SyncedNeighbour* KEpidemicRoutingLayer::getSyncingNeighbourInfo(string nodeMACAddress)
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

        syncedNeighbourList.push_back(syncedNeighbour);
    }

    return syncedNeighbour;
}

void KEpidemicRoutingLayer::setSyncingNeighbourInfoForNextRound()
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

void KEpidemicRoutingLayer::setSyncingNeighbourInfoForNoNeighboursOrEmptyCache()
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

KSummaryVectorMsg* KEpidemicRoutingLayer::makeSummaryVectorMessage()
{
    // make a summary vector message to start the syncing process
    KSummaryVectorMsg *summaryVectorMsg = new KSummaryVectorMsg();
    summaryVectorMsg->setSourceAddress(ownMACAddress.c_str());
    summaryVectorMsg->setMessageIDHashVectorArraySize(cacheList.size());
    int i = 0;
    CacheEntry *cacheEntry;
    list<CacheEntry*>::iterator iteratorCache;
    iteratorCache = cacheList.begin();
    while (iteratorCache != cacheList.end()) {
        cacheEntry = *iteratorCache;
        summaryVectorMsg->setMessageIDHashVector(i, cacheEntry->messageID.c_str());

        iteratorCache++;
        i++;
    }
    int realPacketSize = 6 + 6 + (cacheList.size() * KEPIDEMICROUTINGLAYER_MSG_ID_HASH_SIZE);
    summaryVectorMsg->setRealPacketSize(realPacketSize);
    summaryVectorMsg->setByteLength(realPacketSize);

    return summaryVectorMsg;
}

void KEpidemicRoutingLayer::finish()
{

    // remove age data trigger
    cancelEvent(ageDataTimeoutEvent);
    delete ageDataTimeoutEvent;

}

