#include "settings.h"

#include "log.h"
#include "qpack.h"
#include "varint.h"

void wtf_settings_init(wtf_settings* settings)
{
    if (!settings)
        return;

    memset(settings, 0, sizeof(*settings));
    settings->max_field_section_size = 65536;
    settings->qpack_max_table_capacity = WTF_QPACK_DYNAMIC_TABLE_SIZE;
    settings->qpack_blocked_streams = WTF_QPACK_MAX_BLOCKED_STREAMS;
    settings->webtransport_max_sessions = WTF_DEFAULT_MAX_SESSIONS;
    settings->h3_datagram_enabled = true;
    settings->enable_connect_protocol = true;
    settings->enable_webtransport = true;
    settings->settings_sent = false;
    settings->settings_received = false;
}

bool wtf_settings_send(wtf_connection* conn)
{
    if (!conn || !conn->control_stream || !conn->control_stream->quic_stream) {
        return false;
    }

    size_t buffer_size = 512;

    void* send_buffer_raw = malloc(sizeof(QUIC_BUFFER) + buffer_size);
    if (!send_buffer_raw) {
        WTF_LOG_ERROR(conn->server->context, "settings", "Failed to allocate settings buffer");
        return false;
    }

    QUIC_BUFFER* send_buffer = (QUIC_BUFFER*)send_buffer_raw;
    uint8_t* data = (uint8_t*)send_buffer_raw + sizeof(QUIC_BUFFER);
    size_t settings_length;

    if (!wtf_settings_encode_frame(conn, data, buffer_size, &settings_length)) {
        WTF_LOG_ERROR(conn->server->context, "settings", "Failed to write settings frame");
        free(send_buffer_raw);
        return false;
    }

    send_buffer->Buffer = data;
    send_buffer->Length = (uint32_t)settings_length;

    QUIC_STATUS status = conn->server->context->quic_api->StreamSend(
        conn->control_stream->quic_stream, send_buffer, 1, QUIC_SEND_FLAG_NONE, send_buffer_raw);

    if (QUIC_SUCCEEDED(status)) {
        conn->local_settings.settings_sent = true;
        WTF_LOG_INFO(conn->server->context, "settings",
                     "Server settings sent after receiving client settings "
                     "(version negotiation)");
        return true;
    } else {
        WTF_LOG_ERROR(conn->server->context, "settings", "Failed to send settings frame: 0x%x",
                      status);
        free(send_buffer_raw);
        return false;
    }
}

bool wtf_settings_decode_frame(wtf_connection* conn, const uint8_t* data, size_t data_len)
{
    if (!conn || !data)
        return false;

    uint16_t offset = 0;

    WTF_LOG_DEBUG(conn->server->context, "http3", "Parsing settings frame: %zu bytes", data_len);

    while (offset < data_len) {
        uint64_t setting_id, setting_value;

        if (!wtf_varint_decode((uint16_t)data_len, data, &offset, &setting_id)) {
            WTF_LOG_ERROR(conn->server->context, "http3",
                          "Failed to decode setting ID at offset %zu", offset);
            return false;
        }

        if (!wtf_varint_decode((uint16_t)data_len, data, &offset, &setting_value)) {
            WTF_LOG_ERROR(conn->server->context, "http3",
                          "Failed to decode setting value for ID %llu at offset %zu",
                          (unsigned long long)setting_id, offset);
            return false;
        }

        WTF_LOG_DEBUG(conn->server->context, "http3", "Setting ID: %llu (0x%llx), Value: %llu",
                      (unsigned long long)setting_id, (unsigned long long)setting_id,
                      (unsigned long long)setting_value);

        switch (setting_id) {
            case WTF_SETTING_ENABLE_CONNECT_PROTOCOL:
                conn->peer_settings.enable_connect_protocol = (setting_value != 0);
                WTF_LOG_DEBUG(conn->server->context, "http3", "CONNECT protocol enabled: %s",
                              conn->peer_settings.enable_connect_protocol ? "yes" : "no");
                break;

            case WTF_SETTING_ENABLE_WEBTRANSPORT:
                conn->peer_settings.enable_webtransport = (setting_value != 0);
                WTF_LOG_TRACE(conn->server->context, "http3", "WebTransport enabled: %s",
                              conn->peer_settings.enable_webtransport ? "yes" : "no");
                break;

            case WTF_SETTING_H3_DATAGRAM:
                conn->peer_settings.h3_datagram_enabled = (setting_value != 0);
                WTF_LOG_TRACE(conn->server->context, "http3", "H3 datagrams enabled: %s",
                              conn->peer_settings.h3_datagram_enabled ? "yes" : "no");
                break;

            case WTF_SETTING_QPACK_MAX_TABLE_CAPACITY:
                conn->peer_settings.qpack_max_table_capacity = (uint32_t)setting_value;
                conn->qpack.peer_max_table_capacity = (uint32_t)setting_value;
                WTF_LOG_TRACE(conn->server->context, "http3", "Peer QPACK max table capacity: %u",
                              (uint32_t)setting_value);
                break;

            case WTF_SETTING_MAX_FIELD_SECTION_SIZE:
                conn->peer_settings.max_field_section_size = (uint32_t)setting_value;
                WTF_LOG_TRACE(conn->server->context, "http3", "Max field section size: %u",
                              (uint32_t)setting_value);
                break;

            case WTF_SETTING_QPACK_BLOCKED_STREAMS:
                conn->peer_settings.qpack_blocked_streams = (uint32_t)setting_value;
                conn->qpack.peer_blocked_streams = setting_value;
                WTF_LOG_TRACE(conn->server->context, "http3", "Peer QPACK blocked streams: %u",
                              (uint32_t)setting_value);
                break;

            case WTF_SETTING_WEBTRANSPORT_MAX_SESSIONS:
                conn->peer_settings.webtransport_max_sessions = (uint32_t)setting_value;
                WTF_LOG_TRACE(conn->server->context, "http3", "Peer WebTransport max sessions: %u",
                              (uint32_t)setting_value);
                break;

            default:
                WTF_LOG_DEBUG(conn->server->context, "http3",
                              "Ignoring unknown setting %llu (0x%llx) = %llu",
                              (unsigned long long)setting_id, (unsigned long long)setting_id,
                              (unsigned long long)setting_value);
                break;
        }
    }
    return true;
}

void wtf_session_set_context(wtf_session_t* session, void* user_context)
{
    if (!session) {
        return;
    }
    ((wtf_session*)session)->user_context = user_context;
}

bool wtf_settings_encode_frame(wtf_connection* conn, uint8_t* buffer, size_t buffer_size,
                               size_t* frame_length)
{
    if (!conn || !buffer || !frame_length)
        return false;

    uint8_t* current_pos = buffer;
    uint8_t* buffer_end = buffer + buffer_size;

    // Encode frame type
    current_pos = wtf_varint_encode(WTF_FRAME_SETTINGS, current_pos);
    if (current_pos >= buffer_end) {
        return false;
    }

    // Calculate settings size
    size_t settings_size = 0;
    settings_size += wtf_varint_size(WTF_SETTING_QPACK_MAX_TABLE_CAPACITY)
        + wtf_varint_size(conn->local_settings.qpack_max_table_capacity);
    settings_size += wtf_varint_size(WTF_SETTING_QPACK_BLOCKED_STREAMS)
        + wtf_varint_size(conn->local_settings.qpack_blocked_streams);
    settings_size += wtf_varint_size(WTF_SETTING_ENABLE_CONNECT_PROTOCOL) + wtf_varint_size(1);
    settings_size += wtf_varint_size(WTF_SETTING_ENABLE_WEBTRANSPORT) + wtf_varint_size(1);
    settings_size += wtf_varint_size(WTF_SETTING_H3_DATAGRAM) + wtf_varint_size(1);
    settings_size += wtf_varint_size(WTF_SETTING_WEBTRANSPORT_MAX_SESSIONS)
        + wtf_varint_size(conn->local_settings.webtransport_max_sessions);
    settings_size += wtf_varint_size(WTF_SETTING_MAX_FIELD_SECTION_SIZE)
        + wtf_varint_size(conn->local_settings.max_field_section_size);

    // Encode settings size
    current_pos = wtf_varint_encode(settings_size, current_pos);
    if (current_pos >= buffer_end) {
        return false;
    }

    struct {
        uint64_t id;
        uint64_t value;
    } settings_list[] = {
        {WTF_SETTING_QPACK_MAX_TABLE_CAPACITY, conn->local_settings.qpack_max_table_capacity},
        {WTF_SETTING_QPACK_BLOCKED_STREAMS, conn->local_settings.qpack_blocked_streams},
        {WTF_SETTING_ENABLE_CONNECT_PROTOCOL, 1},
        {WTF_SETTING_ENABLE_WEBTRANSPORT, 1},
        {WTF_SETTING_H3_DATAGRAM, 1},
        {WTF_SETTING_WEBTRANSPORT_MAX_SESSIONS, conn->local_settings.webtransport_max_sessions},
        {WTF_SETTING_MAX_FIELD_SECTION_SIZE, conn->local_settings.max_field_section_size}};

    for (size_t i = 0; i < ARRAYSIZE(settings_list); i++) {
        // Encode setting ID
        current_pos = wtf_varint_encode(settings_list[i].id, current_pos);
        if (current_pos >= buffer_end) {
            return false;
        }

        // Encode setting value
        current_pos = wtf_varint_encode(settings_list[i].value, current_pos);
        if (current_pos >= buffer_end) {
            return false;
        }
    }

    *frame_length = current_pos - buffer;
    return true;
}
