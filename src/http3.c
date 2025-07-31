#include "http3.h"

#include "conn.h"
#include "log.h"
#include "qpack.h"
#include "session.h"
#include "settings.h"
#include "utils.h"
#include "varint.h"

typedef struct {
    wtf_varint_t type;
    wtf_varint_t length;
    uint32_t header_size;
    bool complete;
} wtf_frame_info;

typedef enum {
    WTF_FRAME_RESULT_SUCCESS,
    WTF_FRAME_RESULT_NEED_MORE_DATA,
    WTF_FRAME_RESULT_INVALID_FRAME,
    WTF_FRAME_RESULT_PROTOCOL_ERROR
} wtf_frame_result_t;

wtf_http3_stream* wtf_http3_stream_create(wtf_connection* conn, HQUIC quic_stream,
                                          uint64_t stream_id)
{
    wtf_http3_stream* stream = malloc(sizeof(wtf_http3_stream));
    if (!stream) {
        return NULL;
    }

    memset(stream, 0, sizeof(*stream));
    stream->id = stream_id;
    stream->quic_stream = quic_stream;
    stream->connection = conn;
    stream->state = WTF_INTERNAL_STREAM_STATE_IDLE;

    // Only add to streams map if it's not a placeholder stream
    if (stream_id != UINT64_MAX) {
        mtx_lock(&conn->streams_mutex);
        http3_stream_map_itr itr = http3_stream_map_insert(&conn->streams, stream_id, stream);
        if (http3_stream_map_is_end(itr)) {
            mtx_unlock(&conn->streams_mutex);
            free(stream);
            return NULL;
        }
        mtx_unlock(&conn->streams_mutex);
    }

    return stream;
}

void wtf_http3_stream_destroy(wtf_http3_stream* stream)
{
    if (!stream)
        return;

    if (stream->header_buffer) {
        free(stream->header_buffer);
    }
    if (stream->capsule_buffer) {
        free(stream->capsule_buffer);
    }
    free(stream);
}

bool wtf_http3_create_control_stream(wtf_connection* conn)
{
    WTF_LOG_INFO(conn->server->context, "http3", "Creating control stream...");

    // Control streams are NOT associated with WebTransport sessions
    wtf_http3_stream* stream = wtf_http3_stream_create(conn, NULL, UINT64_MAX);
    if (!stream) {
        WTF_LOG_ERROR(conn->server->context, "conn", "Failed to create control stream context");
        return false;
    }

    stream->type = WTF_STREAM_TYPE_CONTROL;
    // Control stream is connection infrastructure, not session-specific
    conn->control_stream = stream;

    HQUIC control_stream;
    QUIC_STATUS status = conn->server->context->quic_api->StreamOpen(
        conn->quic_connection, QUIC_STREAM_OPEN_FLAG_UNIDIRECTIONAL, wtf_http3_stream_callback,
        stream, &control_stream);

    if (QUIC_FAILED(status)) {
        WTF_LOG_ERROR(conn->server->context, "conn", "Failed to create control stream: 0x%x",
                      status);
        free(stream);
        return false;
    }

    stream->quic_stream = control_stream;

    status = conn->server->context->quic_api->StreamStart(
        control_stream, QUIC_STREAM_START_FLAG_NONE);
    if (QUIC_FAILED(status) && status != QUIC_STATUS_PENDING) {
        WTF_LOG_ERROR(conn->server->context, "conn", "StreamStart failed for control stream: 0x%x",
                      status);
        conn->server->context->quic_api->StreamClose(control_stream);
        return false;
    }

    WTF_LOG_INFO(conn->server->context, "http3", "Control stream creation initiated");
    return true;
}

bool wtf_http3_create_qpack_streams(wtf_connection* conn)
{
    WTF_LOG_INFO(conn->server->context, "http3", "Creating QPACK streams...");

    // QPACK encoder stream - connection infrastructure, not session-specific
    wtf_http3_stream* enc_stream = wtf_http3_stream_create(conn, NULL, UINT64_MAX);
    if (!enc_stream) {
        WTF_LOG_ERROR(conn->server->context, "conn", "Failed to create encoder stream context");
        return false;
    }

    enc_stream->type = WTF_STREAM_TYPE_QPACK_ENCODER;
    conn->qpack_encoder_stream = enc_stream;

    HQUIC encoder_stream = NULL;
    QUIC_STATUS status = conn->server->context->quic_api->StreamOpen(
        conn->quic_connection, QUIC_STREAM_OPEN_FLAG_UNIDIRECTIONAL, wtf_http3_stream_callback,
        enc_stream, &encoder_stream);

    if (QUIC_FAILED(status)) {
        WTF_LOG_ERROR(conn->server->context, "conn", "StreamOpen failed for encoder stream: 0x%x",
                      status);
        free(enc_stream);
        return false;
    }

    enc_stream->quic_stream = encoder_stream;

    status = conn->server->context->quic_api->StreamStart(
        encoder_stream, QUIC_STREAM_START_FLAG_NONE);
    if (QUIC_FAILED(status) && status != QUIC_STATUS_PENDING) {
        WTF_LOG_ERROR(conn->server->context, "conn", "StreamStart failed for encoder stream: 0x%x",
                      status);
        conn->server->context->quic_api->StreamClose(encoder_stream);
        return false;
    }

    WTF_LOG_DEBUG(conn->server->context, "http3", "QPACK encoder stream creation initiated");

    // QPACK decoder stream - connection infrastructure, not session-specific
    wtf_http3_stream* dec_stream = wtf_http3_stream_create(conn, NULL, UINT64_MAX);
    if (!dec_stream) {
        WTF_LOG_ERROR(conn->server->context, "conn", "Failed to create decoder stream context");
        return false;
    }

    dec_stream->type = WTF_STREAM_TYPE_QPACK_DECODER;
    conn->qpack_decoder_stream = dec_stream;

    HQUIC decoder_stream = NULL;
    status = conn->server->context->quic_api->StreamOpen(
        conn->quic_connection, QUIC_STREAM_OPEN_FLAG_UNIDIRECTIONAL, wtf_http3_stream_callback,
        dec_stream, &decoder_stream);

    if (QUIC_FAILED(status)) {
        WTF_LOG_ERROR(conn->server->context, "conn", "StreamOpen failed for decoder stream: 0x%x",
                      status);
        free(dec_stream);
        return false;
    }

    dec_stream->quic_stream = decoder_stream;

    status = conn->server->context->quic_api->StreamStart(
        decoder_stream, QUIC_STREAM_START_FLAG_NONE);
    if (QUIC_FAILED(status) && status != QUIC_STATUS_PENDING) {
        WTF_LOG_ERROR(conn->server->context, "conn", "StreamStart failed for decoder stream: 0x%x",
                      status);
        conn->server->context->quic_api->StreamClose(decoder_stream);
        return false;
    }

    WTF_LOG_DEBUG(conn->server->context, "http3", "QPACK decoder stream creation initiated");
    WTF_LOG_INFO(conn->server->context, "http3", "QPACK streams creation initiated successfully");
    return true;
}

static bool wtf_http3_parse_frame_header(const uint8_t* data, size_t length, uint32_t offset,
                                         wtf_frame_info* frame)
{
    if (!data || !frame) {
        return false;
    }

    uint16_t decode_offset = (uint16_t)offset;

    if (!wtf_varint_decode((uint16_t)length, data, &decode_offset, &frame->type)
        || !wtf_varint_decode((uint16_t)length, data, &decode_offset, &frame->length)) {
        frame->complete = false;
        return false;
    }

    frame->header_size = decode_offset - offset;
    frame->complete = true;
    return true;
}

static bool wtf_http3_validate_settings(wtf_connection* conn)
{
    if (!conn->peer_settings.enable_connect_protocol) {
        WTF_LOG_TRACE(conn->server->context, "settings", "CONNECT protocol not enabled by peer");
        return false;
    }

    if (!conn->peer_settings.h3_datagram_enabled) {
        WTF_LOG_TRACE(conn->server->context, "settings", "H3 datagrams not enabled by peer");
        return false;
    }

    if (!conn->peer_settings.enable_webtransport) {
        WTF_LOG_TRACE(conn->server->context, "settings", "WebTransport not enabled by peer");
        return false;
    }

    return true;
}

static bool wtf_http3_handle_settings_exchange(wtf_connection* conn)
{
    if (!conn->peer_settings.settings_sent) {
        WTF_LOG_TRACE(conn->server->context, "settings",
                      "Received client settings - now sending server settings");
        if (!wtf_settings_send(conn)) {
            WTF_LOG_ERROR(conn->server->context, "settings", "Failed to send server settings");
            return false;
        }
        conn->peer_settings.settings_sent = true;
    }
    return true;
}

static wtf_frame_result_t wtf_http3_process_settings_frame(wtf_http3_stream* stream,
                                                           const uint8_t* data, size_t data_len)
{
    if (!stream || !data) {
        return WTF_FRAME_RESULT_INVALID_FRAME;
    }

    // SETTINGS frames can only be sent on control streams
    if (stream->type != WTF_STREAM_TYPE_CONTROL) {
        WTF_LOG_ERROR(stream->connection->server->context, "http3",
                      "SETTINGS frame received on non-control stream %llu",
                      (unsigned long long)stream->id);
        return WTF_FRAME_RESULT_PROTOCOL_ERROR;
    }

    if (!wtf_settings_decode_frame(stream->connection, data, data_len)) {
        WTF_LOG_ERROR(stream->connection->server->context, "http3",
                      "Failed to decode SETTINGS frame on stream %llu",
                      (unsigned long long)stream->id);
        return WTF_FRAME_RESULT_INVALID_FRAME;
    }

    if (!wtf_http3_validate_settings(stream->connection)) {
        return WTF_FRAME_RESULT_PROTOCOL_ERROR;
    }

    stream->connection->peer_settings.settings_received = true;

    if (!wtf_qpack_init_encoder(stream->connection->server->context, &stream->connection->qpack)) {
        WTF_LOG_ERROR(stream->connection->server->context, "settings",
                      "Failed to initialize QPACK encoder");
        return WTF_FRAME_RESULT_INVALID_FRAME;
    }

    if (!wtf_http3_handle_settings_exchange(stream->connection)) {
        return WTF_FRAME_RESULT_INVALID_FRAME;
    }

    return WTF_FRAME_RESULT_SUCCESS;
}

static wtf_frame_result_t wtf_http3_process_goaway_frame(wtf_http3_stream* stream,
                                                         const uint8_t* data, size_t data_len)
{
    if (!stream || !data) {
        return WTF_FRAME_RESULT_INVALID_FRAME;
    }

    // GOAWAY frames can only be sent on control streams
    if (stream->type != WTF_STREAM_TYPE_CONTROL) {
        WTF_LOG_ERROR(stream->connection->server->context, "http3",
                      "GOAWAY frame received on non-control stream %llu",
                      (unsigned long long)stream->id);
        return WTF_FRAME_RESULT_PROTOCOL_ERROR;
    }

    uint16_t offset = 0;
    uint64_t stream_id;

    if (!wtf_varint_decode((uint16_t)data_len, data, &offset, &stream_id)) {
        WTF_LOG_ERROR(stream->connection->server->context, "http3",
                      "Failed to decode GOAWAY stream ID");
        return WTF_FRAME_RESULT_INVALID_FRAME;
    }

    WTF_LOG_INFO(stream->connection->server->context, "http3", "Received GOAWAY for stream ID %llu",
                 (unsigned long long)stream_id);

    stream->connection->state = WTF_CONNECTION_STATE_CLOSING;

    wtf_connection* conn = stream->connection;
    mtx_lock(&conn->sessions_mutex);

    for (session_map_itr itr = session_map_first(&conn->sessions); !session_map_is_end(itr);
         itr = session_map_next(itr)) {
        wtf_session* session = itr.data->val;

        if (session->state == WTF_SESSION_CONNECTED) {
            WTF_LOG_INFO(conn->server->context, "session", "Draining session %llu due to GOAWAY",
                         (unsigned long long)session->id);

            wtf_session_send_capsule(session, WTF_CAPSULE_DRAIN_WEBTRANSPORT_SESSION, NULL, 0);
            session->state = WTF_SESSION_DRAINING;

            if (session->callback) {
                wtf_session_event_t event = {.type = WTF_SESSION_EVENT_DRAINING,
                                             .session = (wtf_session_t*)session,
                                             .user_context = session->user_context};
                session->callback(&event);
            }
        }
    }

    mtx_unlock(&conn->sessions_mutex);
    return WTF_FRAME_RESULT_SUCCESS;
}

static bool wtf_http3_parse_capsule(wtf_http3_stream* stream, const uint8_t* data, size_t length,
                                    wtf_capsule* capsule)
{
    if (!stream || !data || !capsule || length == 0)
        return false;

    uint16_t offset = 0;

    if (!stream->capsule_header_complete) {
        if (!wtf_varint_decode((uint16_t)length, data, &offset, &stream->capsule_type)) {
            return false;
        }

        if (!wtf_varint_decode((uint16_t)length, data, &offset, &stream->capsule_length)) {
            return false;
        }

        stream->capsule_header_complete = true;
        stream->capsule_bytes_read = 0;

        if (stream->capsule_length > 0) {
            if (stream->capsule_length > WTF_MAX_DATAGRAM_SIZE) {
                WTF_LOG_ERROR(stream->connection->server->context, "capsule",
                              "Capsule too large: %llu bytes",
                              (unsigned long long)stream->capsule_length);
                return false;
            }

            if (stream->capsule_buffer) {
                free(stream->capsule_buffer);
            }

            stream->capsule_buffer = malloc((size_t)stream->capsule_length);
            if (!stream->capsule_buffer) {
                return false;
            }
        }
    }

    size_t available_data = length - offset;
    size_t bytes_needed = (size_t)stream->capsule_length - stream->capsule_bytes_read;
    size_t bytes_to_copy = min(available_data, bytes_needed);

    if (bytes_to_copy > 0 && stream->capsule_buffer) {
        memcpy(stream->capsule_buffer + stream->capsule_bytes_read, data + offset, bytes_to_copy);
        stream->capsule_bytes_read += bytes_to_copy;
    }

    if (stream->capsule_bytes_read >= (size_t)stream->capsule_length) {
        capsule->type = stream->capsule_type;
        capsule->length = stream->capsule_length;
        capsule->data = stream->capsule_buffer;

        stream->capsule_header_complete = false;
        stream->capsule_buffer = NULL;

        return true;
    }

    return false;
}

static bool wtf_http3_process_webtransport_capsules(wtf_http3_stream* stream, const uint8_t* data,
                                                    uint32_t length, uint32_t* processed_bytes)
{
    // Only process capsules on WebTransport streams, never on control/QPACK streams
    if (!stream->webtransport_session || WTF_STREAM_IS_UNIDIRECTIONAL(stream->id)) {
        return false;
    }

    if (stream->webtransport_session->state == WTF_SESSION_CLOSED) {
        return false;
    }

    wtf_capsule capsule;
    if (wtf_http3_parse_capsule(stream, data + *processed_bytes, length - *processed_bytes,
                                &capsule)) {
        wtf_session_process_capsule(stream->webtransport_session, &capsule);

        if (capsule.data) {
            free(capsule.data);
        }

        size_t capsule_total_size = wtf_varint_size(capsule.type) + wtf_varint_size(capsule.length)
            + (size_t)capsule.length;
        *processed_bytes += (uint32_t)capsule_total_size;
        return true;
    } else {
        uint32_t remaining = length - *processed_bytes;
        if (remaining > 0 && remaining <= sizeof(stream->buffered_headers)) {
            memcpy(stream->buffered_headers, data + *processed_bytes, remaining);
            stream->buffered_headers_length = remaining;
        }
        return false;
    }
}

static wtf_frame_result_t wtf_http3_process_headers_frame(wtf_http3_stream* stream,
                                                          const uint8_t* data, uint32_t length,
                                                          wtf_connect_request* pending_request)
{
    // HEADERS frames should only be processed on bidirectional streams (CONNECT requests)
    if (!WTF_STREAM_IS_UNIDIRECTIONAL(stream->id)) {
        wtf_context* ctx = stream->connection->server->context;
        if (wtf_qpack_parse_connect_headers(ctx, stream, data, length, pending_request)
            == WTF_SUCCESS) {
            return WTF_FRAME_RESULT_SUCCESS;
        }
        return WTF_FRAME_RESULT_INVALID_FRAME;
    }
    
    WTF_LOG_ERROR(stream->connection->server->context, "http3",
                  "HEADERS frame received on unidirectional stream %llu",
                  (unsigned long long)stream->id);
    return WTF_FRAME_RESULT_PROTOCOL_ERROR;
}

static wtf_frame_result_t wtf_http3_process_webtransport_stream_frame(
    wtf_http3_stream* stream, const uint8_t* data, uint32_t length)
{
    if (WTF_STREAM_IS_UNIDIRECTIONAL(stream->id)) {
        WTF_LOG_ERROR(stream->connection->server->context, "http3",
                      "WEBTRANSPORT_STREAM frame received on unidirectional stream %llu",
                      (unsigned long long)stream->id);
        return WTF_FRAME_RESULT_PROTOCOL_ERROR;
    }

    stream->is_webtransport = true;
    uint64_t session_id = 0;
    bool found_session = false;

    if (length > 0) {
        uint16_t session_offset = 0;
        if (wtf_varint_decode((uint16_t)length, data, &session_offset, &session_id)) {
            found_session = true;
        }
    } else {
        // If no session ID specified, use the first available session
        mtx_lock(&stream->connection->sessions_mutex);
        session_map_itr first_session_itr = session_map_first(&stream->connection->sessions);
        if (!session_map_is_end(first_session_itr)) {
            session_id = first_session_itr.data->val->id;
            found_session = true;
        }
        mtx_unlock(&stream->connection->sessions_mutex);
    }

    if (found_session) {
        wtf_session* session = wtf_connection_find_session(stream->connection, session_id);
        if (session) {
            stream->webtransport_session = session;
            wtf_connection_associate_stream_with_session(stream->connection, stream, session);
        }
    }

    return WTF_FRAME_RESULT_SUCCESS;
}

static wtf_frame_result_t wtf_http3_process_single_frame(
    wtf_http3_stream* stream, const wtf_frame_info* frame, const uint8_t* frame_data,
    wtf_connect_request* pending_request, bool* has_connect_headers)
{
    switch (frame->type) {
        case WTF_FRAME_SETTINGS:
            // SETTINGS frames only valid on control streams
            if (stream->type != WTF_STREAM_TYPE_CONTROL) {
                WTF_LOG_ERROR(stream->connection->server->context, "http3",
                              "SETTINGS frame received on non-control stream %llu",
                              (unsigned long long)stream->id);
                return WTF_FRAME_RESULT_PROTOCOL_ERROR;
            }
            return wtf_http3_process_settings_frame(stream, frame_data, (size_t)frame->length);

        case WTF_FRAME_HEADERS: {
            wtf_frame_result_t result = wtf_http3_process_headers_frame(
                stream, frame_data, (uint32_t)frame->length, pending_request);
            if (result == WTF_FRAME_RESULT_SUCCESS && !WTF_STREAM_IS_UNIDIRECTIONAL(stream->id)) {
                *has_connect_headers = true;
            }
            return result;
        }

        case WTF_FRAME_DATA:
            // DATA frames only valid on bidirectional streams
            if (WTF_STREAM_IS_UNIDIRECTIONAL(stream->id)) {
                WTF_LOG_ERROR(stream->connection->server->context, "http3",
                              "DATA frame received on unidirectional stream %llu",
                              (unsigned long long)stream->id);
                return WTF_FRAME_RESULT_PROTOCOL_ERROR;
            }
            return WTF_FRAME_RESULT_SUCCESS;

        case WTF_FRAME_WEBTRANSPORT_STREAM:
            return wtf_http3_process_webtransport_stream_frame(stream, frame_data,
                                                               (uint32_t)frame->length);

        case WTF_FRAME_GOAWAY:
            // GOAWAY frames only valid on control streams
            if (stream->type == WTF_STREAM_TYPE_CONTROL) {
                return wtf_http3_process_goaway_frame(stream, frame_data, (uint32_t)frame->length);
            } else {
                WTF_LOG_ERROR(stream->connection->server->context, "http3",
                              "GOAWAY frame received on non-control stream %llu",
                              (unsigned long long)stream->id);
                return WTF_FRAME_RESULT_PROTOCOL_ERROR;
            }

        default:
            // Unknown frame types are ignored per HTTP/3 spec
            WTF_LOG_DEBUG(stream->connection->server->context, "http3",
                          "Ignoring unknown frame type %llu on stream %llu",
                          (unsigned long long)frame->type, (unsigned long long)stream->id);
            return WTF_FRAME_RESULT_SUCCESS;
    }
}

static wtf_frame_result_t wtf_http3_process_frames(
    wtf_http3_stream* stream, const uint8_t* data, uint32_t length, uint32_t offset,
    wtf_connect_request* pending_request, bool* has_connect_headers)
{
    uint32_t processed_bytes = offset;

    while (processed_bytes < length) {
        if (wtf_http3_process_webtransport_capsules(stream, data, length, &processed_bytes)) {
            continue;
        }

        wtf_frame_info frame;
        uint32_t frame_start = processed_bytes;

        if (!wtf_http3_parse_frame_header(data, length, processed_bytes, &frame)) {
            uint32_t remaining = length - frame_start;
            if (remaining > 0 && remaining <= sizeof(stream->buffered_headers)) {
                memcpy(stream->buffered_headers, data + frame_start, remaining);
                stream->buffered_headers_length = remaining;
            }
            break;
        }

        uint32_t frame_header_end = processed_bytes + frame.header_size;

        if (frame_header_end + (uint32_t)frame.length > length) {
            uint32_t remaining = length - frame_start;
            if (remaining > 0 && remaining <= sizeof(stream->buffered_headers)) {
                memcpy(stream->buffered_headers, data + frame_start, remaining);
                stream->buffered_headers_length = remaining;
            }
            break;
        }

        wtf_frame_result_t result = wtf_http3_process_single_frame(
            stream, &frame, data + frame_header_end, pending_request, has_connect_headers);

        if (result != WTF_FRAME_RESULT_SUCCESS) {
            return result;
        }

        processed_bytes = frame_header_end + (uint32_t)frame.length;
    }

    return WTF_FRAME_RESULT_SUCCESS;
}

static wtf_result_t wtf_http3_encode_response(wtf_connection* conn, uint16_t status_code,
                                              uint8_t** response_data, uint32_t* response_length)
{
    if (!conn || !response_data || !response_length || status_code < 100 || status_code > 599) {
        return WTF_ERROR_INVALID_PARAMETER;
    }

    uint32_t max_response_size = 1024;
    uint8_t* data = malloc(max_response_size);
    if (!data) {
        return WTF_ERROR_OUT_OF_MEMORY;
    }

    uint8_t* current_pos = data;
    current_pos = wtf_varint_encode(WTF_FRAME_HEADERS, current_pos);

    uint8_t status_data[64];
    uint32_t status_len = 0;

    if (conn->qpack.initialized) {
        mtx_lock(&conn->qpack.mutex);
        struct lsxpack_header status_header;
        memset(&status_header, 0, sizeof(status_header));
        char status_value[4];
        snprintf(status_value, sizeof(status_value), "%u", status_code);

        status_header.buf = (char*)(status_data + 2);
        status_header.name_offset = 0;
        status_header.name_len = 7;
        status_header.val_offset = 7;
        status_header.val_len = (lsxpack_strlen_t)strlen(status_value);
        memcpy(status_header.buf, ":status", 7);
        memcpy(status_header.buf + 7, status_value, status_header.val_len);

        if (lsqpack_enc_start_header(&conn->qpack.encoder, 0, 0) == 0) {
            size_t prefix_len = 2;
            size_t header_len = sizeof(status_data) - 2;
            uint8_t enc_stream_buf[256];
            size_t enc_stream_size = sizeof(enc_stream_buf);

            if (lsqpack_enc_encode(&conn->qpack.encoder, enc_stream_buf, &enc_stream_size,
                                   status_data + 2, &header_len, &status_header,
                                   (enum lsqpack_enc_flags)0)
                == LQES_OK) {
                enum lsqpack_enc_header_flags hflags;
                size_t pref_sz = lsqpack_enc_end_header(&conn->qpack.encoder, status_data,
                                                        prefix_len, &hflags);
                if (pref_sz >= 0) {
                    status_len = (uint32_t)(pref_sz + header_len);
                    if (enc_stream_size > 0 && enc_stream_size < sizeof(enc_stream_buf)) {
                        if (conn->qpack.tsu_buf_sz + enc_stream_size
                            <= sizeof(conn->qpack.tsu_buf)) {
                            memcpy(conn->qpack.tsu_buf + conn->qpack.tsu_buf_sz, enc_stream_buf,
                                   enc_stream_size);
                            conn->qpack.tsu_buf_sz += enc_stream_size;
                        }
                    }
                }
            }
        }
        mtx_unlock(&conn->qpack.mutex);
    }

    if (status_len == 0) {
        free(data);
        return WTF_ERROR_INTERNAL;
    }

    current_pos = wtf_varint_encode(status_len, current_pos);

    if (current_pos - data + status_len > max_response_size) {
        free(data);
        return WTF_ERROR_BUFFER_TOO_SMALL;
    }

    memcpy(current_pos, status_data, status_len);
    current_pos += status_len;

    *response_data = data;
    *response_length = (uint32_t)(current_pos - data);
    return WTF_SUCCESS;
}

static wtf_result_t wtf_http3_send_response(wtf_http3_stream* stream, uint16_t status_code)
{
    if (!stream || !stream->connection) {
        return WTF_ERROR_INVALID_PARAMETER;
    }

    uint8_t* response_data = NULL;
    uint32_t response_length = 0;

    wtf_result_t encode_result = wtf_http3_encode_response(
        stream->connection, status_code, &response_data, &response_length);
    if (encode_result != WTF_SUCCESS) {
        WTF_LOG_ERROR(stream->connection->server->context, "connect",
                      "Failed to encode status code %u", status_code);
        return encode_result;
    }

    void* send_buffer_raw = malloc(sizeof(QUIC_BUFFER) + response_length);
    if (!send_buffer_raw) {
        free(response_data);
        return WTF_ERROR_OUT_OF_MEMORY;
    }

    QUIC_BUFFER* send_buffer = (QUIC_BUFFER*)send_buffer_raw;
    uint8_t* buffer_data = (uint8_t*)send_buffer_raw + sizeof(QUIC_BUFFER);

    memcpy(buffer_data, response_data, response_length);
    free(response_data);

    send_buffer->Buffer = buffer_data;
    send_buffer->Length = response_length;

    QUIC_STATUS quic_status = stream->connection->server->context->quic_api->StreamSend(
        stream->quic_stream, send_buffer, 1, QUIC_SEND_FLAG_NONE, send_buffer_raw);

    if (QUIC_FAILED(quic_status)) {
        WTF_LOG_ERROR(stream->connection->server->context, "connect", "StreamSend failed: 0x%x",
                      quic_status);
        free(send_buffer_raw);
        return wtf_quic_status_to_result(quic_status);
    }

    return WTF_SUCCESS;
}

static wtf_result_t wtf_session_establish(wtf_http3_stream* stream)
{
    if (!stream || !stream->webtransport_session) {
        return WTF_ERROR_INVALID_STATE;
    }

    wtf_session* session = stream->webtransport_session;
    session->state = WTF_SESSION_CONNECTED;
    session->id = stream->id;

    WTF_LOG_INFO(stream->connection->server->context, "webtransport",
                 "WebTransport session %llu established on CONNECT stream %llu",
                 (unsigned long long)session->id, (unsigned long long)stream->id);

    if (session->callback) {
        wtf_session_event_t event = {.type = WTF_SESSION_EVENT_CONNECTED,
                                     .session = (wtf_session_t*)session,
                                     .user_context = session->user_context};
        session->callback(&event);
    }

    wtf_connection_process_buffered_data(stream->connection, session);
    return WTF_SUCCESS;
}

static bool wtf_http3_process_complete_connect_request(wtf_http3_stream* stream,
                                                       wtf_connect_request* request)
{
    if (!stream || !request) {
        return false;
    }

    wtf_connection* conn = stream->connection;
    bool success = false;

    WTF_LOG_INFO(conn->server->context, "connect",
                 "Processing complete CONNECT request on stream %llu",
                 (unsigned long long)stream->id);

    if (!conn->local_settings.settings_sent || !conn->peer_settings.settings_received) {
        WTF_LOG_ERROR(conn->server->context, "connect",
                      "HEADERS received before settings exchange complete");
        wtf_http3_send_response(stream, 400);
        goto cleanup;
    }

    if (!request->valid) {
        WTF_LOG_ERROR(conn->server->context, "connect", "Invalid CONNECT request");
        wtf_http3_send_response(stream, 400);
        goto cleanup;
    }

    if (!request->method || strcmp(request->method, "CONNECT") != 0 || !request->protocol
        || strcmp(request->protocol, "webtransport") != 0 || !request->scheme
        || strcmp(request->scheme, "https") != 0) {
        WTF_LOG_ERROR(conn->server->context, "connect", "Invalid CONNECT request");
        wtf_http3_send_response(stream, 400);
        goto cleanup;
    }

    WTF_LOG_INFO(conn->server->context, "connect", "Valid WebTransport CONNECT request received");

    bool accept_connection = true;
    if (conn->server->config.connection_validator) {
        wtf_connection_request_t conn_request = {
            .origin = request->origin,
            .path = request->path,
            .authority = request->authority,
            .headers = NULL,
            .header_count = 0,
            .peer_address = &conn->peer_address,
            .address_length = sizeof(conn->peer_address)};

        wtf_connection_decision_t decision = conn->server->config.connection_validator(
            &conn_request, conn->server->config.user_context);

        accept_connection = (decision == WTF_CONNECTION_ACCEPT);
    }

    if (!accept_connection) {
        WTF_LOG_INFO(conn->server->context, "connect", "Connection rejected by validator");
        wtf_http3_send_response(stream, 403);
        goto cleanup;
    }

    wtf_session* session = wtf_session_create(conn, stream);
    if (!session) {
        WTF_LOG_ERROR(conn->server->context, "connect", "Failed to allocate session");
        wtf_http3_send_response(stream, 500);
        goto cleanup;
    }

    session->id = stream->id;
    session->callback = conn->server->config.session_callback;
    session->user_context = conn->server->config.user_context;

    mtx_lock(&conn->sessions_mutex);
    session_map_itr itr = session_map_insert(&conn->sessions, session->id, session);
    if (session_map_is_end(itr)) {
        mtx_unlock(&conn->sessions_mutex);
        WTF_LOG_ERROR(conn->server->context, "connect", "Failed to add session to map");
        wtf_session_destroy(session);
        wtf_http3_send_response(stream, 500);
        goto cleanup;
    }
    mtx_unlock(&conn->sessions_mutex);

    stream->webtransport_session = session;

    wtf_result_t response_result = wtf_http3_send_response(stream, 200);
    if (response_result != WTF_SUCCESS) {
        WTF_LOG_ERROR(conn->server->context, "connect", "Failed to send CONNECT response");
        mtx_lock(&conn->sessions_mutex);
        session_map_erase(&conn->sessions, session->id);
        mtx_unlock(&conn->sessions_mutex);
        wtf_session_destroy(session);
        stream->webtransport_session = NULL;
        goto cleanup;
    }

    if (wtf_session_establish(stream) == WTF_SUCCESS) {
        WTF_LOG_INFO(conn->server->context, "connect", "WebTransport session established");
        success = true;
    }

cleanup:
    if (request->method)
        free(request->method);
    if (request->protocol)
        free(request->protocol);
    if (request->scheme)
        free(request->scheme);
    if (request->authority)
        free(request->authority);
    if (request->path)
        free(request->path);
    if (request->origin)
        free(request->origin);

    return success;
}

static bool wtf_http3_parse_uni_stream_type(wtf_http3_stream* stream, const uint8_t* data,
                                            size_t length, uint32_t* offset)
{
    if (stream->type != 0)
        return true;

    wtf_varint_t stream_type;
    uint16_t type_offset = (uint16_t)*offset;

    if (!wtf_varint_decode((uint16_t)length, data, &type_offset, &stream_type)) {
        if (stream->buffered_headers_length + length <= sizeof(stream->buffered_headers)) {
            memcpy(stream->buffered_headers + stream->buffered_headers_length, data, length);
            stream->buffered_headers_length += (uint32_t)length;
        }
        return false;
    }

    stream->type = stream_type;
    *offset = type_offset;

    switch (stream_type) {
        case WTF_STREAM_TYPE_CONTROL:
            stream->connection->peer_control_stream = stream;
            WTF_LOG_INFO(stream->connection->server->context, "http3",
                        "Peer control stream identified: %llu", (unsigned long long)stream->id);
            break;
        case WTF_STREAM_TYPE_QPACK_ENCODER:
            stream->connection->peer_encoder_stream = stream;
            WTF_LOG_DEBUG(stream->connection->server->context, "http3",
                         "Peer QPACK encoder stream identified: %llu", (unsigned long long)stream->id);
            break;
        case WTF_STREAM_TYPE_QPACK_DECODER:
            stream->connection->peer_decoder_stream = stream;
            WTF_LOG_DEBUG(stream->connection->server->context, "http3",
                         "Peer QPACK decoder stream identified: %llu", (unsigned long long)stream->id);
            break;
        case WTF_STREAM_TYPE_WEBTRANSPORT_STREAM:
            stream->is_webtransport = true;
            WTF_LOG_DEBUG(stream->connection->server->context, "http3",
                         "WebTransport unidirectional stream identified: %llu", (unsigned long long)stream->id);
            break;
        default:
            WTF_LOG_DEBUG(stream->connection->server->context, "http3",
                         "Unknown unidirectional stream type %llu on stream %llu",
                         (unsigned long long)stream_type, (unsigned long long)stream->id);
            break;
    }
    return true;
}

static bool wtf_http3_combine_stream_data(wtf_http3_stream* stream, const uint8_t** data,
                                          uint32_t* length, uint8_t** combined_data,
                                          bool* allocated_buffer)
{
    if (stream->buffered_headers_length == 0) {
        *allocated_buffer = false;
        return true;
    }

    uint32_t combined_length = stream->buffered_headers_length + *length;
    *combined_data = malloc(combined_length);
    if (!*combined_data) {
        return false;
    }

    memcpy(*combined_data, stream->buffered_headers, stream->buffered_headers_length);
    memcpy(*combined_data + stream->buffered_headers_length, *data, *length);

    // Clear buffered data
    stream->buffered_headers_length = 0;
    *data = *combined_data;
    *length = combined_length;
    *allocated_buffer = true;
    return true;
}

static bool wtf_emit_stream_data_event(wtf_stream* stream, const uint8_t* data, size_t length,
                                       bool fin)
{
    if (!stream || !data || length == 0)
        return false;

    if (!stream->receive_enabled || stream->state == WTF_INTERNAL_STREAM_STATE_CLOSED
        || stream->state == WTF_INTERNAL_STREAM_STATE_RESET) {
        return false;
    }

    if (stream->callback) {
        wtf_buffer_t receive_buffers[] = {{(uint32_t)length, (uint8_t*)data}};
        wtf_stream_event_t event = {
            .type = WTF_STREAM_EVENT_DATA_RECEIVED,
            .stream = (wtf_stream_t*)stream,
            .user_context = stream->user_context,
            .data_received = {.buffers = receive_buffers, .buffer_count = 1, .fin = fin}};
        stream->callback(&event);
    }

    return true;
}

static bool wtf_associate_webtransport_session(wtf_http3_stream* stream, const uint8_t* data,
                                               uint32_t length, uint32_t* offset)
{
    if (stream->webtransport_session) {
        return true;
    }

    wtf_varint_t session_id;
    uint16_t session_offset = (uint16_t)*offset;

    if (!wtf_varint_decode((uint16_t)length, data, &session_offset, &session_id)) {
        // Buffer incomplete data
        if (stream->buffered_headers_length + length <= sizeof(stream->buffered_headers)) {
            memcpy(stream->buffered_headers + stream->buffered_headers_length, data, length);
            stream->buffered_headers_length += length;
        }
        return true;
    }

    *offset = session_offset;
    wtf_session* session = wtf_connection_find_session(stream->connection, session_id);
    if (!session) {
        WTF_LOG_WARN(stream->connection->server->context, "webtransport",
                       "WebTransport stream %llu references unknown session %llu",
                       (unsigned long long)stream->id, (unsigned long long)session_id);
        *offset = 0;
        return false;
    }

    stream->webtransport_session = session;
    return wtf_connection_associate_stream_with_session(stream->connection, stream, session);
}

static bool wtf_process_webtransport_stream_data(wtf_http3_stream* stream, const uint8_t* data,
                                                 uint32_t length, uint32_t* offset, bool fin)
{
    // Check if this is a WebTransport stream
    bool is_webtransport_stream = false;
    
    if (WTF_STREAM_IS_UNIDIRECTIONAL(stream->id)) {
        is_webtransport_stream = (stream->type == WTF_STREAM_TYPE_WEBTRANSPORT_STREAM);
    } else {
        is_webtransport_stream = (stream->is_webtransport && stream->webtransport_session);
    }
    
    if (!is_webtransport_stream) {
        return false;
    }

    // For bidirectional WebTransport streams
    if (!WTF_STREAM_IS_UNIDIRECTIONAL(stream->id)) {
        if (*offset < length && stream->webtransport_session) {
            wtf_session* session = stream->webtransport_session;
            mtx_lock(&session->streams_mutex);
            stream_map_itr wt_itr = stream_map_get(&session->streams, stream->id);
            wtf_stream* wt_stream = !stream_map_is_end(wt_itr) ? wt_itr.data->val : NULL;
            mtx_unlock(&session->streams_mutex);

            if (wt_stream) {
                return wtf_emit_stream_data_event(wt_stream, data + *offset, length - *offset, fin);
            }
        }
        return true;
    }

    // For unidirectional WebTransport streams, need to associate with session first
    if (!wtf_associate_webtransport_session(stream, data, length, offset)) {
        return false;
    }

    if (*offset < length && stream->webtransport_session) {
        wtf_session* session = stream->webtransport_session;
        mtx_lock(&session->streams_mutex);
        stream_map_itr wt_itr = stream_map_get(&session->streams, stream->id);
        wtf_stream* wt_stream = !stream_map_is_end(wt_itr) ? wt_itr.data->val : NULL;
        mtx_unlock(&session->streams_mutex);

        if (wt_stream) {
            return wtf_emit_stream_data_event(wt_stream, data + *offset, length - *offset, fin);
        }
    }

    return true;
}

static bool wtf_http3_process_qpack_data(wtf_http3_stream* stream, const uint8_t* data,
                                         uint32_t length, uint32_t offset)
{
    wtf_context* context = stream->connection->server->context;
    wtf_qpack_context* qpack_context = &stream->connection->qpack;

    if (stream->type == WTF_STREAM_TYPE_QPACK_ENCODER && offset < length) {
        return wtf_qpack_process_encoder(context, qpack_context, data + offset, length - offset);
    } else if (stream->type == WTF_STREAM_TYPE_QPACK_DECODER && offset < length) {
        return wtf_qpack_process_decoder(context, qpack_context, data + offset, length - offset);
    }

    return true;
}

static bool wtf_http3_should_process_frames(wtf_http3_stream* stream)
{
    if (WTF_STREAM_IS_UNIDIRECTIONAL(stream->id)) {
        // Only control streams process HTTP/3 frames on unidirectional streams
        return (stream->type == WTF_STREAM_TYPE_CONTROL);
    } else {
        // Only non-WebTransport bidirectional streams process HTTP/3 frames
        return !stream->is_webtransport;
    }
}

static bool wtf_http3_process_stream_receive(wtf_http3_stream* stream, const QUIC_BUFFER* buffer,
                                             bool fin)
{
    const uint8_t* data = buffer->Buffer;
    uint32_t length = buffer->Length;
    uint32_t offset = 0;

    if (!data || length == 0) {
        return true;
    }

    // Combine with any buffered data
    uint8_t* combined_data = NULL;
    bool allocated_buffer = false;
    if (!wtf_http3_combine_stream_data(stream, &data, &length, &combined_data, &allocated_buffer)) {
        return false;
    }

    // Handle unidirectional stream type parsing
    if (WTF_STREAM_IS_UNIDIRECTIONAL(stream->id)) {
        if (!wtf_http3_parse_uni_stream_type(stream, data, length, &offset)) {
            if (allocated_buffer)
                free(combined_data);
            return false;  // Need more data
        }

        // Process QPACK streams (these are infrastructure, not user-visible)
        if (!wtf_http3_process_qpack_data(stream, data, length, offset)) {
            if (allocated_buffer)
                free(combined_data);
            return false;
        }

        // QPACK streams are fully handled above, don't process further
        if (stream->type == WTF_STREAM_TYPE_QPACK_ENCODER
            || stream->type == WTF_STREAM_TYPE_QPACK_DECODER) {
            if (allocated_buffer)
                free(combined_data);
            return true;
        }
    }

    // Handle WebTransport stream data (user-visible streams)
    if (wtf_process_webtransport_stream_data(stream, data, length, &offset, fin)) {
        if (allocated_buffer)
            free(combined_data);
        return true;
    }

    // Process HTTP/3 frames (only on appropriate streams)
    if (!wtf_http3_should_process_frames(stream)) {
        if (allocated_buffer)
            free(combined_data);
        return true;
    }

    wtf_connect_request pending_connect_request = {0};
    bool has_connect_headers = false;

    wtf_frame_result_t frame_result = wtf_http3_process_frames(
        stream, data, length, offset, &pending_connect_request, &has_connect_headers);

    if (allocated_buffer) {
        free(combined_data);
    }

    if (frame_result != WTF_FRAME_RESULT_SUCCESS) {
        // Clean up pending request on error
        if (pending_connect_request.method)
            free(pending_connect_request.method);
        if (pending_connect_request.protocol)
            free(pending_connect_request.protocol);
        if (pending_connect_request.scheme)
            free(pending_connect_request.scheme);
        if (pending_connect_request.authority)
            free(pending_connect_request.authority);
        if (pending_connect_request.path)
            free(pending_connect_request.path);
        if (pending_connect_request.origin)
            free(pending_connect_request.origin);
        return false;
    }

    // Process complete CONNECT requests (creates user-visible sessions)
    if (has_connect_headers) {
        return wtf_http3_process_complete_connect_request(stream, &pending_connect_request);
    }

    return true;
}

static QUIC_STATUS wtf_handle_stream_start_complete(wtf_http3_stream* stream,
                                                    QUIC_STREAM_EVENT* Event)
{
    wtf_connection* conn = stream->connection;

    WTF_LOG_DEBUG(conn->server->context, "stream", "Stream start complete, status=0x%x",
                  Event->START_COMPLETE.Status);

    if (QUIC_FAILED(Event->START_COMPLETE.Status)) {
        WTF_LOG_ERROR(conn->server->context, "stream", "Stream start failed: 0x%x",
                      Event->START_COMPLETE.Status);
        return QUIC_STATUS_SUCCESS;
    }

    stream->id = Event->START_COMPLETE.ID;
    stream->state = WTF_INTERNAL_STREAM_STATE_OPEN;

    // Add to connection stream map
    mtx_lock(&conn->streams_mutex);
    http3_stream_map_itr itr = http3_stream_map_insert(&conn->streams, stream->id, stream);
    if (http3_stream_map_is_end(itr)) {
        WTF_LOG_ERROR(conn->server->context, "stream",
                      "Failed to add stream to map after START_COMPLETE");
    }
    mtx_unlock(&conn->streams_mutex);

    WTF_LOG_INFO(conn->server->context, "stream", "Stream ID ready: %llu, type: %llu",
                 (unsigned long long)stream->id, (unsigned long long)stream->type);

    // Send stream type for infrastructure streams (not user-visible)
    if (stream->type == WTF_STREAM_TYPE_CONTROL || stream->type == WTF_STREAM_TYPE_QPACK_ENCODER
        || stream->type == WTF_STREAM_TYPE_QPACK_DECODER) {
        uint32_t total_size = 512;
        void* send_buffer_raw = malloc(sizeof(QUIC_BUFFER) + total_size);
        if (!send_buffer_raw) {
            WTF_LOG_ERROR(conn->server->context, "stream", "Failed to allocate send buffer");
            conn->server->context->quic_api->StreamShutdown(
                stream->quic_stream,
                QUIC_STREAM_SHUTDOWN_FLAG_ABORT_SEND | QUIC_STREAM_SHUTDOWN_FLAG_ABORT_RECEIVE,
                WTF_H3_FRAME_ERROR);
            return QUIC_STATUS_SUCCESS;
        }

        QUIC_BUFFER* send_buffer = (QUIC_BUFFER*)send_buffer_raw;
        uint8_t* data = (uint8_t*)send_buffer_raw + sizeof(QUIC_BUFFER);
        uint8_t* current_pos = data;
        uint8_t* buffer_end = data + total_size;

        // Encode stream type
        current_pos = wtf_varint_encode(stream->type, current_pos);
        if (current_pos > buffer_end) {
            WTF_LOG_ERROR(conn->server->context, "stream", "Failed to encode stream type");
            free(send_buffer_raw);
            conn->server->context->quic_api->StreamShutdown(
                stream->quic_stream,
                QUIC_STREAM_SHUTDOWN_FLAG_ABORT_SEND | QUIC_STREAM_SHUTDOWN_FLAG_ABORT_RECEIVE,
                WTF_H3_FRAME_ERROR);
            return QUIC_STATUS_SUCCESS;
        }

        if (stream->type == WTF_STREAM_TYPE_CONTROL) {
            WTF_LOG_INFO(conn->server->context, "http3",
                         "Control stream ready - waiting for client settings before sending ours");
        }

        send_buffer->Buffer = data;
        send_buffer->Length = (uint32_t)(current_pos - data);

        QUIC_STATUS status = conn->server->context->quic_api->StreamSend(
            stream->quic_stream, send_buffer, 1, QUIC_SEND_FLAG_NONE, send_buffer_raw);

        if (QUIC_SUCCEEDED(status)) {
            WTF_LOG_INFO(conn->server->context, "http3", "Stream type %llu sent on stream %llu",
                         (unsigned long long)stream->type, (unsigned long long)stream->id);
        } else {
            WTF_LOG_ERROR(conn->server->context, "stream",
                          "Failed to send stream type and data: 0x%x", status);
            free(send_buffer_raw);
            conn->server->context->quic_api->StreamShutdown(
                stream->quic_stream,
                QUIC_STREAM_SHUTDOWN_FLAG_ABORT_SEND | QUIC_STREAM_SHUTDOWN_FLAG_ABORT_RECEIVE,
                WTF_H3_INTERNAL_ERROR);
            return QUIC_STATUS_SUCCESS;
        }
    }

    return QUIC_STATUS_SUCCESS;
}

static QUIC_STATUS wtf_handle_stream_receive(wtf_http3_stream* stream, HQUIC Stream,
                                             QUIC_STREAM_EVENT* Event)
{
    wtf_connection* conn = stream->connection;

    uint64_t stream_id;
    uint32_t stream_id_size = sizeof(stream_id);
    QUIC_STATUS status = conn->server->context->quic_api->GetParam(
        Stream, QUIC_PARAM_STREAM_ID, &stream_id_size, &stream_id);
    if (QUIC_FAILED(status)) {
        WTF_LOG_ERROR(conn->server->context, "stream", "Failed to get stream ID for receive: 0x%x",
                      status);
        conn->server->context->quic_api->StreamShutdown(
            Stream, QUIC_STREAM_SHUTDOWN_FLAG_ABORT_SEND | QUIC_STREAM_SHUTDOWN_FLAG_ABORT_RECEIVE,
            WTF_H3_INTERNAL_ERROR);
        return QUIC_STATUS_SUCCESS;
    }

    if (stream->id == UINT64_MAX) {
        stream->id = stream_id;

        mtx_lock(&conn->streams_mutex);
        http3_stream_map_itr itr = http3_stream_map_insert(&conn->streams, stream_id, stream);
        if (http3_stream_map_is_end(itr)) {
            mtx_unlock(&conn->streams_mutex);
            WTF_LOG_ERROR(conn->server->context, "stream",
                          "Failed to add peer stream to map with stream ID %llu",
                          (unsigned long long)stream_id);
            conn->server->context->quic_api->StreamShutdown(
                Stream,
                QUIC_STREAM_SHUTDOWN_FLAG_ABORT_SEND | QUIC_STREAM_SHUTDOWN_FLAG_ABORT_RECEIVE,
                WTF_H3_INTERNAL_ERROR);
            return QUIC_STATUS_SUCCESS;
        }
        mtx_unlock(&conn->streams_mutex);

        WTF_LOG_DEBUG(conn->server->context, "stream", "Updated peer stream with ID %llu",
                      (unsigned long long)stream_id);
    }

    bool is_fin = (Event->RECEIVE.Flags & QUIC_RECEIVE_FLAG_FIN) != 0;
    
    for (uint32_t i = 0; i < Event->RECEIVE.BufferCount; i++) {
        if (!wtf_http3_process_stream_receive(stream, &Event->RECEIVE.Buffers[i], is_fin)) {
            WTF_LOG_ERROR(conn->server->context, "stream",
                          "Failed to process stream data on stream %llu",
                          (unsigned long long)stream_id);

            uint64_t error_code = WTF_H3_GENERAL_PROTOCOL_ERROR;
            if (WTF_STREAM_IS_UNIDIRECTIONAL(stream_id)) {
                switch (stream->type) {
                    case WTF_STREAM_TYPE_CONTROL:
                        error_code = WTF_H3_CLOSED_CRITICAL_STREAM;
                        break;
                    case WTF_STREAM_TYPE_QPACK_ENCODER:
                        error_code = WTF_QPACK_ENCODER_STREAM_ERROR;
                        break;
                    case WTF_STREAM_TYPE_QPACK_DECODER:
                        error_code = WTF_QPACK_DECODER_STREAM_ERROR;
                        break;
                    default:
                        error_code = WTF_H3_FRAME_ERROR;
                        break;
                }
            } else {
                error_code = WTF_H3_MESSAGE_ERROR;
            }

            conn->server->context->quic_api->StreamShutdown(
                Stream,
                QUIC_STREAM_SHUTDOWN_FLAG_ABORT_SEND | QUIC_STREAM_SHUTDOWN_FLAG_ABORT_RECEIVE,
                error_code);

            // Critical streams require connection shutdown
            if (stream->type == WTF_STREAM_TYPE_CONTROL) {
                WTF_LOG_ERROR(conn->server->context, "stream",
                              "Critical control stream error - closing connection");
                conn->server->context->quic_api->ConnectionShutdown(
                    conn->quic_connection, QUIC_CONNECTION_SHUTDOWN_FLAG_NONE,
                    WTF_H3_CLOSED_CRITICAL_STREAM);
            }

            return QUIC_STATUS_SUCCESS;
        }
    }

    if (is_fin) {
        if (stream->state == WTF_INTERNAL_STREAM_STATE_HALF_CLOSED_LOCAL) {
            stream->state = WTF_INTERNAL_STREAM_STATE_CLOSED;
        } else {
            stream->state = WTF_INTERNAL_STREAM_STATE_HALF_CLOSED_REMOTE;
        }
        conn->server->context->quic_api->StreamClose(Stream);
    }
    return QUIC_STATUS_SUCCESS;
}

static QUIC_STATUS wtf_handle_stream_shutdown_events(wtf_http3_stream* stream, HQUIC Stream,
                                                     QUIC_STREAM_EVENT* Event)
{
    wtf_connection* conn = stream->connection;
    uint64_t stream_id;
    uint32_t stream_id_size = sizeof(stream_id);
    QUIC_STATUS status = conn->server->context->quic_api->GetParam(
        Stream, QUIC_PARAM_STREAM_ID, &stream_id_size, &stream_id);

    if (QUIC_SUCCEEDED(status)) {
        mtx_lock(&conn->streams_mutex);
        http3_stream_map_itr itr = http3_stream_map_get(&conn->streams, stream_id);
        if (!http3_stream_map_is_end(itr)) {
            wtf_http3_stream* current = itr.data->val;

            switch (Event->Type) {
                case QUIC_STREAM_EVENT_PEER_SEND_SHUTDOWN:
                    if (current->state == WTF_INTERNAL_STREAM_STATE_HALF_CLOSED_LOCAL) {
                        current->state = WTF_INTERNAL_STREAM_STATE_CLOSED;
                    } else {
                        current->state = WTF_INTERNAL_STREAM_STATE_HALF_CLOSED_REMOTE;
                    }
                    WTF_LOG_DEBUG(conn->server->context, "stream",
                                 "Peer send shutdown on stream %llu", (unsigned long long)stream_id);
                    break;

                case QUIC_STREAM_EVENT_PEER_SEND_ABORTED:
                    current->state = WTF_INTERNAL_STREAM_STATE_RESET;
                    WTF_LOG_DEBUG(conn->server->context, "stream",
                                 "Peer send aborted on stream %llu", (unsigned long long)stream_id);
                    break;

                case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
                    WTF_LOG_DEBUG(conn->server->context, "stream",
                                 "Stream %llu shutdown complete", (unsigned long long)stream_id);
                    
                    if (current->id != UINT64_MAX) {
                        http3_stream_map_erase(&conn->streams, stream_id);
                    }
                    
                    // Clear connection references for infrastructure streams
                    if (current == conn->control_stream) {
                        conn->control_stream = NULL;
                    } else if (current == conn->peer_control_stream) {
                        conn->peer_control_stream = NULL;
                    } else if (current == conn->qpack_encoder_stream) {
                        conn->qpack_encoder_stream = NULL;
                    } else if (current == conn->qpack_decoder_stream) {
                        conn->qpack_decoder_stream = NULL;
                    } else if (current == conn->peer_encoder_stream) {
                        conn->peer_encoder_stream = NULL;
                    } else if (current == conn->peer_decoder_stream) {
                        conn->peer_decoder_stream = NULL;
                    }
                    
                    wtf_http3_stream_destroy(current);
                    break;

                default:
                    break;
            }
        }
        mtx_unlock(&conn->streams_mutex);
    }

    return QUIC_STATUS_SUCCESS;
}

QUIC_STATUS QUIC_API wtf_http3_stream_callback(HQUIC Stream, void* Context,
                                               QUIC_STREAM_EVENT* Event)
{
    wtf_http3_stream* stream = (wtf_http3_stream*)Context;

    if (!stream || !stream->connection || !stream->connection->server
        || !stream->connection->server->context) {
        return QUIC_STATUS_INTERNAL_ERROR;
    }

    switch (Event->Type) {
        case QUIC_STREAM_EVENT_START_COMPLETE:
            return wtf_handle_stream_start_complete(stream, Event);

        case QUIC_STREAM_EVENT_RECEIVE:
            return wtf_handle_stream_receive(stream, Stream, Event);

        case QUIC_STREAM_EVENT_SEND_COMPLETE:
            if (Event->SEND_COMPLETE.ClientContext) {
                free(Event->SEND_COMPLETE.ClientContext);
            }
            return QUIC_STATUS_SUCCESS;

        case QUIC_STREAM_EVENT_PEER_SEND_SHUTDOWN:
        case QUIC_STREAM_EVENT_PEER_SEND_ABORTED:
        case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
            return wtf_handle_stream_shutdown_events(stream, Stream, Event);

        case QUIC_STREAM_EVENT_SEND_SHUTDOWN_COMPLETE:
            WTF_LOG_DEBUG(stream->connection->server->context, "stream",
                         "Send shutdown complete on stream %llu", (unsigned long long)stream->id);
            return QUIC_STATUS_SUCCESS;

        default:
            WTF_LOG_DEBUG(stream->connection->server->context, "stream",
                         "Unhandled stream event %d on stream %llu", 
                         Event->Type, (unsigned long long)stream->id);
            return QUIC_STATUS_SUCCESS;
    }
}