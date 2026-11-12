// See CRSFHandset.h. Also provides Handset_device (replaces lib/Handset/devHandset.cpp).
#include "CRSFHandset.h"
#include "CRSFEndpoint.h"
#include "CRSFRouter.h"
#include "FIFO.h"
#include "logging.h"
#include "device.h"
#include "tango2_platform.h"

uint32_t CRSFHandset::GoodPktsCountResult = 0;
uint32_t CRSFHandset::BadPktsCountResult = 0;

static constexpr int32_t OpenTXsyncPacketInterval = 200;    // ms
static constexpr int32_t OpenTXsyncOffsetSafeMargin = 1000; // 100us, in 0.1 us units
static constexpr uint32_t HandsetTimeoutMs = 1000;           // no frames this long -> disconnected

// Frames queued for the handset: [len][frame...], written whole from loop().
static FIFO<1024> outFIFO;

void CRSFHandset::Begin()
{
    addDevice(CRSF_ADDRESS_ELRS_LUA);
    addDevice(CRSF_ADDRESS_RADIO_TRANSMITTER);
    crsfRouter.addConnector(this);
    statsLastMs = millis();
}

void CRSFHandset::End()
{
    flushOutput();
}

void CRSFHandset::forwardMessage(const crsf_header_t *message)
{
    if (!controllerConnected)
        return;
    uint8_t size = CRSF_FRAME_SIZE(message->frame_size);
    if (size > CRSF_MAX_PACKET_LEN)
        return;
    outFIFO.lock();
    if (outFIFO.ensure(size + 1))
    {
        outFIFO.push(size);
        outFIFO.pushBytes((const uint8_t *)message, size);
    }
    outFIFO.unlock();
}

void CRSFHandset::flushOutput()
{
    uint8_t buf[CRSF_MAX_PACKET_LEN];
    while (outFIFO.size() > 0)
    {
        outFIFO.lock();
        uint8_t n = outFIFO.pop();
        outFIFO.popBytes(buf, n);
        outFIFO.unlock();
        // On a serial line CRSF always starts with the sync byte, not the device address
        buf[0] = CRSF_SYNC_BYTE;
        host->crsf_write(buf, n);
    }
}

void ICACHE_RAM_ATTR CRSFHandset::setPacketInterval(int32_t PacketInterval)
{
    RequestedRCpacketInterval = PacketInterval;
    OpenTXsyncOffset = 0;
    OpenTXsyncWindow = 0;
    OpenTXsyncWindowSize = std::max((int32_t)1, (int32_t)(20000 / RequestedRCpacketInterval));
    OpenTXsyncLastSent -= OpenTXsyncPacketInterval;
}

void ICACHE_RAM_ATTR CRSFHandset::JustSentRFpacket()
{
    uint32_t last = dataLastRecv;
    uint32_t m = micros();
    auto delta = (int32_t)(m - last);

    if (delta >= (int32_t)RequestedRCpacketInterval)
    {
        OpenTXsyncOffset = -(delta % RequestedRCpacketInterval) * 10;
        OpenTXsyncWindow = 0;
        OpenTXsyncLastSent -= OpenTXsyncPacketInterval;
    }
    else
    {
        OpenTXsyncWindow = std::min(OpenTXsyncWindow + 1, (int32_t)OpenTXsyncWindowSize);
        OpenTXsyncOffset = ((OpenTXsyncOffset * (OpenTXsyncWindow - 1)) + delta * 10) / OpenTXsyncWindow;
    }
}

void CRSFHandset::sendSyncPacketToTX()
{
    const uint32_t now = millis();
    if (now - OpenTXsyncLastSent >= (uint32_t)OpenTXsyncPacketInterval)
    {
        int32_t packetRate = RequestedRCpacketInterval * 10;
        int32_t offset = OpenTXsyncOffset - OpenTXsyncOffsetSafeMargin;

        CRSF_MK_EXT_FRAME_T(crsf_sync_packet_t) sync_packet = {
            .h = {
                CRSF_ADDRESS_RADIO_TRANSMITTER,
                CRSF_EXT_FRAME_SIZE(sizeof(crsf_sync_packet_t)),
                CRSF_FRAMETYPE_HANDSET,
                CRSF_ADDRESS_RADIO_TRANSMITTER,
                CRSF_ADDRESS_CRSF_TRANSMITTER,
            },
            .p = {
                .subType = CRSF_HANDSET_SUBCMD_TIMING,
                .rate = htobe32(packetRate),
                .offset = htobe32(offset)
            },
            .crc = 0
        };
        sync_packet.crc = crsfRouter.crsf_crc.calc((uint8_t *)&sync_packet + CRSF_TELEMETRY_TYPE_INDEX, sizeof(sync_packet) - 3);
        crsfRouter.deliverMessageTo(CRSF_ADDRESS_RADIO_TRANSMITTER, (crsf_header_t *)&sync_packet);
        OpenTXsyncLastSent = now;
    }
}

void CRSFHandset::watchdog()
{
    const uint32_t now = millis();
    if (controllerConnected && now - lastFrameMs > HandsetTimeoutMs)
    {
        DBGLN("handset frames stopped");
        controllerConnected = false;
        outFIFO.lock();
        outFIFO.flush();
        outFIFO.unlock();
        if (disconnected) disconnected();
    }
    if (now - statsLastMs >= 1000)
    {
        GoodPktsCountResult = GoodPktsCount;
        BadPktsCountResult = BadPktsCount;
        GoodPktsCount = BadPktsCount = 0;
        statsLastMs = now;
    }
}

void CRSFHandset::handleInput()
{
    // Whole frames only: [sync/addr][len][type][payload][crc]
    for (int i = 0; i < 8; i++)
    {
        uint16_t n = host->crsf_read(inBuffer, sizeof(inBuffer));
        if (n == 0)
            break;
        const uint32_t totalLen = inBuffer[1] + 2;
        if (n < 4 || totalLen != n || totalLen > CRSF_MAX_PACKET_LEN ||
            crsfRouter.crsf_crc.calc(&inBuffer[2], totalLen - 3) != inBuffer[totalLen - 1])
        {
            BadPktsCount++;
            continue;
        }
        GoodPktsCount++;
        dataLastRecv = micros();
        lastFrameMs = millis();
        if (!controllerConnected)
        {
            controllerConnected = true;
            DBGLN("handset connected");
            if (connected) connected();
        }
        crsfRouter.processMessage(this, (crsf_header_t *)inBuffer);
        // like CRSFHandset::handleOutput(): the mixer sync goes out when nothing else is queued
        if (outFIFO.size() == 0)
            sendSyncPacketToTX();
    }
    flushOutput();
    watchdog();
}

// ---- device glue (lib/Handset/devHandset.cpp) ----
Handset *handset;
static CRSFHandset hostHandset;

static bool initialize()
{
    handset = &hostHandset;
    return true;
}

static int start()
{
    handset->Begin();
    return DURATION_IMMEDIATELY;
}

static int timeout()
{
    handset->handleInput();
    return DURATION_IMMEDIATELY;
}

device_t Handset_device = {
    .initialize = initialize,
    .start = start,
    .event = nullptr,
    .timeout = timeout,
    .subscribe = EVENT_NONE,
};
