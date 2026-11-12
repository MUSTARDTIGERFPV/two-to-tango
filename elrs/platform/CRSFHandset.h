// Tango 2 replacement for ExpressLRS lib/Handset/CRSFHandset.h: the handset
// (EdgeTX's internal-module stack, or the PC tool through the loader) is a
// queue of whole CRSF frames behind host->crsf_read / host->crsf_write, so
// there is no UART, baud detection or half-duplex switching. The rest
// (mixer sync frames, connect/disconnect watchdog) follows CRSFHandset.cpp.
#ifndef H_CRSF_CONTROLLER
#define H_CRSF_CONTROLLER

#ifdef TARGET_TX

#include "handset.h"
#include "crsf_protocol.h"
#include "CRSFConnector.h"
#include "common.h"

class CRSFHandset final : public Handset, public CRSFConnector
{
public:
    static uint32_t GoodPktsCountResult;
    static uint32_t BadPktsCountResult;

    void Begin() override;
    void End() override;

    void forwardMessage(const crsf_header_t *message) override;
    void handleInput() override;

    void setPacketInterval(int32_t PacketInterval) override;
    void JustSentRFpacket() override;

    uint8_t GetMaxPacketBytes() const override { return CRSF_MAX_PACKET_LEN; }
    int getMinPacketInterval() const override { return 1; }

private:
    uint8_t inBuffer[CRSF_MAX_PACKET_LEN + 2] = {};

    volatile uint32_t dataLastRecv = 0;
    volatile int32_t OpenTXsyncOffset = 0;
    volatile int32_t OpenTXsyncWindow = 0;
    volatile int32_t OpenTXsyncWindowSize = 0;
    uint32_t OpenTXsyncLastSent = 0;

    uint32_t GoodPktsCount = 0;
    uint32_t BadPktsCount = 0;
    uint32_t lastFrameMs = 0;
    uint32_t statsLastMs = 0;

    void sendSyncPacketToTX();
    void flushOutput();
    void watchdog();
};

#endif
#endif
