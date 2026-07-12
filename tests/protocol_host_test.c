#include <tumovgm/protocol.h>

#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                             \
    do {                                                                             \
        if(!(condition)) {                                                           \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            return false;                                                            \
        }                                                                            \
    } while(false)

static void rewrite_crc(uint8_t* encoded, size_t length) {
    const uint16_t crc = tumovgm_crc16_ccitt_false(encoded + 4, length - 6);
    encoded[length - 2] = (uint8_t)(crc & UINT16_C(0xFF));
    encoded[length - 1] = (uint8_t)(crc >> 8);
}

static bool test_crc_vector(void) {
    static const uint8_t input[] = "123456789";
    CHECK(tumovgm_crc16_ccitt_false(input, sizeof(input) - 1) == UINT16_C(0x29B1));
    CHECK(tumovgm_crc16_ccitt_false(NULL, 0) == UINT16_C(0xFFFF));
    return true;
}

static bool test_round_trip_and_truncation(void) {
    static const uint8_t payload[] = {0x10, 0x20, 0x30, 0x40};
    const TumovgmFrame source = {
        .major = TUMOVGM_PROTOCOL_MAJOR,
        .minor = TUMOVGM_PROTOCOL_MINOR,
        .kind = TumovgmFrameKindRequest,
        .flags = TumovgmFrameFlagAckRequired,
        .sequence = 42,
        .message = TumovgmMessageHello,
        .payload_length = sizeof(payload),
        .payload = payload,
    };
    uint8_t encoded[TumovgmFrameMaxSize] = {0};
    size_t written = 0;
    CHECK(
        tumovgm_frame_encode(&source, encoded, sizeof(encoded), &written) ==
        TumovgmCodecStatusOk);
    CHECK(written == TumovgmFrameHeaderSize + sizeof(payload) + TumovgmFrameCrcSize);

    for(size_t length = 0; length < written; length++) {
        TumovgmFrame partial = {0};
        size_t consumed = 99;
        CHECK(
            tumovgm_frame_decode(encoded, length, &partial, &consumed) ==
            TumovgmCodecStatusNeedMore);
        CHECK(consumed == 0);
    }

    TumovgmFrame decoded = {0};
    size_t consumed = 0;
    CHECK(
        tumovgm_frame_decode(encoded, written, &decoded, &consumed) ==
        TumovgmCodecStatusOk);
    CHECK(consumed == written);
    CHECK(decoded.major == source.major);
    CHECK(decoded.minor == source.minor);
    CHECK(decoded.kind == source.kind);
    CHECK(decoded.flags == source.flags);
    CHECK(decoded.sequence == source.sequence);
    CHECK(decoded.message == source.message);
    CHECK(decoded.payload_length == sizeof(payload));
    CHECK(memcmp(decoded.payload, payload, sizeof(payload)) == 0);
    return true;
}

static bool test_max_payload_and_output_limit(void) {
    uint8_t payload[TUMOVGM_PROTOCOL_MAX_PAYLOAD];
    memset(payload, 0xA5, sizeof(payload));
    const TumovgmFrame source = {
        .major = TUMOVGM_PROTOCOL_MAJOR,
        .minor = TUMOVGM_PROTOCOL_MINOR,
        .kind = TumovgmFrameKindResponse,
        .sequence = 1,
        .message = TumovgmMessageCapabilities,
        .payload_length = sizeof(payload),
        .payload = payload,
    };
    uint8_t encoded[TumovgmFrameMaxSize];
    size_t written = 0;
    CHECK(
        tumovgm_frame_encode(&source, encoded, sizeof(encoded) - 1, &written) ==
        TumovgmCodecStatusOutputTooSmall);
    CHECK(written == 0);
    CHECK(
        tumovgm_frame_encode(&source, encoded, sizeof(encoded), &written) ==
        TumovgmCodecStatusOk);
    CHECK(written == sizeof(encoded));
    return true;
}

static bool test_bad_magic_crc_oversize_and_semantics(void) {
    const TumovgmFrame source = {
        .major = TUMOVGM_PROTOCOL_MAJOR,
        .minor = TUMOVGM_PROTOCOL_MINOR,
        .kind = TumovgmFrameKindRequest,
        .sequence = 7,
        .message = TumovgmMessagePing,
    };
    uint8_t encoded[TumovgmFrameMaxSize] = {0};
    size_t written = 0;
    CHECK(
        tumovgm_frame_encode(&source, encoded, sizeof(encoded), &written) ==
        TumovgmCodecStatusOk);

    uint8_t prefixed[TumovgmFrameMaxSize + 3] = {0x99, 0x88, 0x77};
    memcpy(prefixed + 3, encoded, written);
    TumovgmFrame decoded = {0};
    size_t consumed = 0;
    CHECK(
        tumovgm_frame_decode(prefixed, written + 3, &decoded, &consumed) ==
        TumovgmCodecStatusBadMagic);
    CHECK(consumed == 3);

    encoded[written - 1] ^= 0x80;
    CHECK(
        tumovgm_frame_decode(encoded, written, &decoded, &consumed) ==
        TumovgmCodecStatusBadCrc);
    CHECK(consumed == 1);
    encoded[written - 1] ^= 0x80;

    encoded[12] = 0x01;
    encoded[13] = 0x02;
    CHECK(
        tumovgm_frame_decode(encoded, written, &decoded, &consumed) ==
        TumovgmCodecStatusPayloadTooLarge);
    CHECK(consumed == 1);
    encoded[12] = 0;
    encoded[13] = 0;

    encoded[6] = TumovgmFrameKindEvent;
    rewrite_crc(encoded, written);
    CHECK(
        tumovgm_frame_decode(encoded, written, &decoded, &consumed) ==
        TumovgmCodecStatusBadSemantics);
    CHECK(consumed == written);
    return true;
}

static bool test_unknown_message_and_versions(void) {
    const TumovgmFrame source = {
        .major = 2,
        .minor = 9,
        .kind = TumovgmFrameKindRequest,
        .sequence = 1,
        .message = UINT16_C(0x7FFF),
    };
    uint8_t encoded[TumovgmFrameMaxSize] = {0};
    size_t written = 0;
    CHECK(
        tumovgm_frame_encode(&source, encoded, sizeof(encoded), &written) ==
        TumovgmCodecStatusOk);

    TumovgmFrame decoded = {0};
    size_t consumed = 0;
    CHECK(
        tumovgm_frame_decode(encoded, written, &decoded, &consumed) ==
        TumovgmCodecStatusOk);
    CHECK(decoded.message == UINT16_C(0x7FFF));

    TumovgmNegotiatedProtocol negotiated = {0};
    CHECK(
        tumovgm_protocol_negotiate(2, 0, 128, &negotiated) ==
        TumovgmNegotiationStatusMajorMismatch);
    CHECK(
        tumovgm_protocol_negotiate(1, 9, 1024, &negotiated) ==
        TumovgmNegotiationStatusOk);
    CHECK(negotiated.minor == TUMOVGM_PROTOCOL_MINOR);
    CHECK(negotiated.max_payload == TUMOVGM_PROTOCOL_MAX_PAYLOAD);
    CHECK(
        tumovgm_protocol_negotiate(1, 0, 0, &negotiated) ==
        TumovgmNegotiationStatusInvalidLimit);
    return true;
}

static bool test_session_lifecycle(void) {
    TumovgmSession session;
    tumovgm_session_init(&session);
    CHECK(session.state == TumovgmSessionStateDisconnected);
    CHECK(tumovgm_session_connect(&session) == TumovgmSessionResultOk);
    CHECK(tumovgm_session_ready(&session) == TumovgmSessionResultOk);
    CHECK(tumovgm_session_open(&session, 100) == TumovgmSessionResultOk);
    CHECK(tumovgm_session_close(&session, 101) == TumovgmSessionResultNotOwner);
    CHECK(session.state == TumovgmSessionStateActive);
    CHECK(tumovgm_session_close(&session, 100) == TumovgmSessionResultOk);
    CHECK(tumovgm_session_close(&session, 100) == TumovgmSessionResultAlreadyClosed);
    CHECK(!tumovgm_session_disconnect(&session));
    CHECK(!tumovgm_session_disconnect(&session));

    CHECK(tumovgm_session_connect(&session) == TumovgmSessionResultOk);
    CHECK(tumovgm_session_ready(&session) == TumovgmSessionResultOk);
    CHECK(tumovgm_session_open(&session, 200) == TumovgmSessionResultOk);
    CHECK(tumovgm_session_disconnect(&session));
    CHECK(session.last_closed_id == 200);
    CHECK(session.active_id == 0);
    CHECK(session.state == TumovgmSessionStateDisconnected);
    return true;
}

static bool test_stream_bounds(void) {
    TumovgmStreamWindow window;
    tumovgm_stream_window_init(&window, 2);
    CHECK(!tumovgm_stream_consume(&window));
    CHECK(tumovgm_stream_grant(&window, 2));
    CHECK(tumovgm_stream_consume(&window));
    CHECK(tumovgm_stream_consume(&window));
    CHECK(!tumovgm_stream_consume(&window));
    CHECK(tumovgm_stream_queue_push(&window));
    CHECK(tumovgm_stream_queue_push(&window));
    CHECK(!tumovgm_stream_queue_push(&window));
    CHECK(tumovgm_stream_queue_pop(&window));
    CHECK(tumovgm_stream_queue_pop(&window));
    CHECK(!tumovgm_stream_queue_pop(&window));

    window.credits = UINT16_MAX;
    CHECK(!tumovgm_stream_grant(&window, 1));
    CHECK(window.credits == UINT16_MAX);
    return true;
}

static uint32_t fuzz_next(uint32_t* state) {
    uint32_t value = *state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *state = value;
    return value;
}

static bool test_fuzzed_parser(void) {
    uint32_t state = UINT32_C(0x51A7F00D);
    uint8_t input[640];
    for(size_t iteration = 0; iteration < 100000; iteration++) {
        const size_t length = fuzz_next(&state) % (sizeof(input) + 1);
        for(size_t index = 0; index < length; index++) {
            input[index] = (uint8_t)fuzz_next(&state);
        }
        if((iteration & 1U) != 0 && length >= 4) {
            input[0] = TUMOVGM_PROTOCOL_MAGIC_0;
            input[1] = TUMOVGM_PROTOCOL_MAGIC_1;
            input[2] = TUMOVGM_PROTOCOL_MAGIC_2;
            input[3] = TUMOVGM_PROTOCOL_MAGIC_3;
        }
        TumovgmFrame decoded = {0};
        size_t consumed = 0;
        const TumovgmCodecStatus status =
            tumovgm_frame_decode(input, length, &decoded, &consumed);
        CHECK(status >= TumovgmCodecStatusOk && status <= TumovgmCodecStatusOutputTooSmall);
        CHECK(consumed <= length);
        if(status == TumovgmCodecStatusOk) {
            CHECK(consumed >= TumovgmFrameHeaderSize + TumovgmFrameCrcSize);
            CHECK(decoded.payload_length <= TUMOVGM_PROTOCOL_MAX_PAYLOAD);
        }
    }
    return true;
}

int main(void) {
    if(!test_crc_vector() || !test_round_trip_and_truncation() ||
       !test_max_payload_and_output_limit() ||
       !test_bad_magic_crc_oversize_and_semantics() ||
       !test_unknown_message_and_versions() || !test_session_lifecycle() ||
       !test_stream_bounds() || !test_fuzzed_parser()) {
        return 1;
    }
    puts("protocol_host_test: PASS");
    return 0;
}
