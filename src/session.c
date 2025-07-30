#include "session.h"

#include "log.h"
#include "stream.h"
#include "utils.h"
#include "varint.h"

bool wtf_session_process_datagram(wtf_session* session, const uint8_t* data, size_t length)
{
    if (!session || !data || length == 0)
        return false;

    if (session->callback) {
        wtf_session_event_t event = {
            .type = WTF_SESSION_EVENT_DATAGRAM_RECEIVED,
            .session = (wtf_session_t*)session,
            .user_context = session->user_context,
            .datagram_received = {.length = (const uint32_t)length, .data = (const uint8_t*)data}};
        session->callback(&event);
    }
    return true;
}

void wtf_session_destroy(wtf_session* session)
{
    if (!session)
        return;

    mtx_lock(&session->streams_mutex);
    for (stream_map_itr itr = stream_map_first(&session->streams); !stream_map_is_end(itr);
         itr = stream_map_next(itr)) {
        wtf_stream_destroy(itr.data->val);
    }
    stream_map_cleanup(&session->streams);
    mtx_unlock(&session->streams_mutex);

    if (session->close_reason) {
        free(session->close_reason);
    }

    mtx_destroy(&session->streams_mutex);

    free(session);
}

wtf_result_t wtf_session_send_capsule(wtf_session* session, uint64_t type, const uint8_t* data,
                                      size_t length)
{
    if (!session || !session->connect_stream) {
        return WTF_ERROR_INVALID_PARAMETER;
    }

    size_t type_size = wtf_varint_size(type);
    size_t length_size = wtf_varint_size(length);
    size_t total_size = type_size + length_size + length;

    void* send_buffer_raw = malloc(sizeof(QUIC_BUFFER) + total_size);
    if (!send_buffer_raw) {
        return WTF_ERROR_OUT_OF_MEMORY;
    }

    QUIC_BUFFER* send_buffer = (QUIC_BUFFER*)send_buffer_raw;
    uint8_t* capsule_data = (uint8_t*)send_buffer_raw + sizeof(QUIC_BUFFER);

    uint8_t* current_pos = capsule_data;
    uint8_t* buffer_end = capsule_data + total_size;

    current_pos = wtf_varint_encode(type, current_pos);
    if (current_pos > buffer_end) {
        free(send_buffer_raw);
        return WTF_ERROR_INTERNAL;
    }

    current_pos = wtf_varint_encode(length, current_pos);
    if (current_pos > buffer_end) {
        free(send_buffer_raw);
        return WTF_ERROR_INTERNAL;
    }

    if (data && length > 0) {
        memcpy(current_pos, data, length);
    }

    send_buffer->Buffer = capsule_data;
    send_buffer->Length = (uint32_t)total_size;

    QUIC_STATUS status = session->connection->server->context->quic_api->StreamSend(
        session->connect_stream->quic_stream, send_buffer, 1, QUIC_SEND_FLAG_NONE, send_buffer_raw);

    if (QUIC_FAILED(status)) {
        free(send_buffer_raw);
        return wtf_quic_status_to_result(status);
    }

    return WTF_SUCCESS;
}

bool wtf_session_process_capsule(wtf_session* session, const wtf_capsule* capsule)
{
    if (!session || !capsule)
        return false;

    WTF_LOG_TRACE(session->connection->server->context, "capsule",
                  "Processing capsule type %llu, length %llu for session %llu",
                  (unsigned long long)capsule->type, (unsigned long long)capsule->length,
                  (unsigned long long)session->id);

    switch (capsule->type) {
        case WTF_CAPSULE_DATAGRAM: {
            return true;
        }

        case WTF_CAPSULE_DRAIN_WEBTRANSPORT_SESSION: {
            WTF_LOG_INFO(session->connection->server->context, "session",
                         "Session %llu received DRAIN capsule", (unsigned long long)session->id);

            if (session->state == WTF_SESSION_CONNECTED) {
                session->state = WTF_SESSION_DRAINING;

                if (session->callback) {
                    wtf_session_event_t event = {.type = WTF_SESSION_EVENT_DRAINING,
                                                 .session = (wtf_session_t*)session,
                                                 .user_context = session->user_context};
                    session->callback(&event);
                }
            } else {
                WTF_LOG_TRACE(session->connection->server->context, "session",
                              "Ignoring DRAIN capsule - session %llu is in state %d",
                              (unsigned long long)session->id, session->state);
            }

            return true;
        }

        case WTF_CAPSULE_CLOSE_WEBTRANSPORT_SESSION: {
            if (capsule->length < 4) {
                WTF_LOG_ERROR(session->connection->server->context, "capsule",
                              "CLOSE capsule too short: %llu bytes",
                              (unsigned long long)capsule->length);
                return false;
            }

            uint32_t error_code = 0;
            if (capsule->data && capsule->length >= 4) {
                error_code = (uint32_t)(((uint32_t)capsule->data[0] << 24)
                                        | ((uint32_t)capsule->data[1] << 16)
                                        | ((uint32_t)capsule->data[2] << 8)
                                        | (uint32_t)capsule->data[3]);
            }

            char* reason = NULL;
            size_t reason_len = (size_t)capsule->length - 4;
            if (reason_len > 0 && capsule->data) {
                if (reason_len > 1024) {
                    reason_len = 1024;
                }
                reason = wtf_strndup((const char*)(capsule->data + 4), reason_len);
            }

            WTF_LOG_INFO(session->connection->server->context, "session",
                         "Session %llu received CLOSE capsule: error=%u, reason='%s'",
                         (unsigned long long)session->id, error_code, reason ? reason : "");

            session->close_error_code = error_code;
            if (session->close_reason) {
                free(session->close_reason);
            }
            session->close_reason = reason;
            session->state = WTF_SESSION_CLOSED;

            if (session->callback) {
                wtf_session_event_t event = {
                    .type = WTF_SESSION_EVENT_DISCONNECTED,
                    .session = (wtf_session_t*)session,
                    .user_context = session->user_context,
                    .disconnected = {.error_code = error_code, .reason = reason}};
                session->callback(&event);
            }

            return true;
        }

        default:

            WTF_LOG_DEBUG(session->connection->server->context, "capsule",
                          "Ignoring unknown capsule type %llu", (unsigned long long)capsule->type);
            return true;
    }
}

wtf_session* wtf_session_create(wtf_connection* conn, wtf_http3_stream* connect_stream)
{
    if (!conn || !connect_stream)
        return NULL;

    wtf_session* session = malloc(sizeof(wtf_session));
    if (!session) {
        return NULL;
    }

    memset(session, 0, sizeof(*session));
    session->connection = conn;
    session->connect_stream = connect_stream;
    session->state = WTF_SESSION_HANDSHAKING;
    session->id = connect_stream->id;
    session->max_streams = conn->server->config.max_streams_per_session;
    if (session->max_streams == 0) {
        session->max_streams = 1000;
    }

    stream_map_init(&session->streams);

    if (mtx_init(&session->streams_mutex, mtx_plain) != thrd_success) {
        stream_map_cleanup(&session->streams);
        free(session);
        return NULL;
    }

    return session;
}

wtf_result_t wtf_session_close(wtf_session_t* session, uint32_t error_code, const char* reason)
{
    if (!session) {
        return WTF_ERROR_INVALID_PARAMETER;
    }

    wtf_session* sess = (wtf_session*)session;

    if (sess->state == WTF_SESSION_CLOSED) {
        return WTF_ERROR_INVALID_STATE;
    }

    WTF_LOG_INFO(sess->connection->server->context, "session",
                 "Closing session %llu with error %u: %s", (unsigned long long)sess->id, error_code,
                 reason ? reason : "");

    sess->state = WTF_SESSION_CLOSED;
    sess->close_error_code = error_code;

    if (sess->close_reason) {
        free(sess->close_reason);
    }
    sess->close_reason = reason ? wtf_strndup(reason, 1024) : NULL;

    uint8_t close_data[1028];
    uint32_t close_len = 4;

    close_data[0] = (uint8_t)(error_code >> 24);
    close_data[1] = (uint8_t)(error_code >> 16);
    close_data[2] = (uint8_t)(error_code >> 8);
    close_data[3] = (uint8_t)(error_code);

    if (reason) {
        size_t reason_len = strlen(reason);
        if (reason_len > 1024) {
            WTF_LOG_WARN(sess->connection->server->context, "session",
                         "Close reason truncated from %zu to 1024 bytes", reason_len);
            reason_len = 1024;
        }
        memcpy(close_data + 4, reason, reason_len);
        close_len += (uint32_t)reason_len;
    }

    wtf_result_t result = wtf_session_send_capsule(sess, WTF_CAPSULE_CLOSE_WEBTRANSPORT_SESSION,
                                                   close_data, close_len);

    if (result == WTF_SUCCESS && sess->connect_stream && sess->connect_stream->quic_stream) {
        WTF_LOG_DEBUG(sess->connection->server->context, "session",
                      "Sending FIN on CONNECT stream after CLOSE_WEBTRANSPORT_SESSION");

        QUIC_BUFFER empty_buffer = {0};
        empty_buffer.Buffer = NULL;
        empty_buffer.Length = 0;

        QUIC_STATUS status = sess->connection->server->context->quic_api->StreamSend(
            sess->connect_stream->quic_stream, &empty_buffer, 1, QUIC_SEND_FLAG_FIN, NULL);

        if (QUIC_FAILED(status)) {
            WTF_LOG_WARN(sess->connection->server->context, "session",
                         "Failed to send FIN after CLOSE capsule: 0x%x", status);
        }
    }

    mtx_lock(&sess->streams_mutex);
    for (stream_map_itr itr = stream_map_first(&sess->streams); !stream_map_is_end(itr);
         itr = stream_map_next(itr)) {
        wtf_stream* stream = itr.data->val;

        if (stream->callback) {
            wtf_stream_event_t event = {.type = WTF_STREAM_EVENT_CLOSED,
                                        .stream = (wtf_stream_t*)stream,
                                        .user_context = stream->user_context};
            stream->callback(&event);
        }

        if (stream->quic_stream && sess->connection && sess->connection->server
            && sess->connection->server->context) {
            sess->connection->server->context->quic_api->StreamShutdown(
                stream->quic_stream,
                QUIC_STREAM_SHUTDOWN_FLAG_ABORT_SEND | QUIC_STREAM_SHUTDOWN_FLAG_ABORT_RECEIVE,
                WTF_WEBTRANSPORT_SESSION_GONE);
        }
    }
    mtx_unlock(&sess->streams_mutex);

    if (sess->callback) {
        wtf_session_event_t event = {.type = WTF_SESSION_EVENT_DISCONNECTED,
                                     .session = session,
                                     .user_context = sess->user_context,
                                     .disconnected = {.error_code = error_code, .reason = reason}};
        sess->callback(&event);
    }

    return result;
}

wtf_result_t wtf_session_send_datagram(wtf_session* session, const wtf_buffer_t* data,
                                       uint32_t buffer_count)
{
    wtf_result_t result = WTF_SUCCESS;
    wtf_internal_send_context* send_ctx = NULL;
    wtf_connection* conn = NULL;
    wtf_buffer_t* new_buffers = NULL;
    uint8_t* header_buffer = NULL;

    if (!session || !data || buffer_count == 0) {
        result = WTF_ERROR_INVALID_PARAMETER;
        goto cleanup;
    }

    uint32_t total_data_size = 0;
    for (uint32_t i = 0; i < buffer_count; i++) {
        if (data[i].data == NULL && data[i].length > 0) {
            result = WTF_ERROR_INVALID_PARAMETER;
            goto cleanup;
        }
        total_data_size += data[i].length;
    }

    if (session->state != WTF_SESSION_CONNECTED) {
        result = WTF_ERROR_INVALID_STATE;
        goto cleanup;
    }

    conn = session->connection;
    if (!conn->peer_settings.h3_datagram_enabled) {
        result = WTF_ERROR_PROTOCOL_VIOLATION;
        goto cleanup;
    }

    uint64_t quarter_stream_id = session->id / 4;
    size_t header_size = wtf_varint_size(quarter_stream_id);
    size_t total_size = header_size + total_data_size;

    if (total_size > conn->max_datagram_size) {
        result = WTF_ERROR_BUFFER_TOO_SMALL;
        goto cleanup;
    }

    header_buffer = malloc(header_size);
    if (!header_buffer) {
        result = WTF_ERROR_OUT_OF_MEMORY;
        goto cleanup;
    }

    new_buffers = malloc((buffer_count + 1) * sizeof(wtf_buffer_t));
    if (!new_buffers) {
        result = WTF_ERROR_OUT_OF_MEMORY;
        goto cleanup;
    }

    send_ctx = malloc(sizeof(wtf_internal_send_context));
    if (!send_ctx) {
        result = WTF_ERROR_OUT_OF_MEMORY;
        goto cleanup;
    }

    send_ctx->session = session;

    // Encode quarter stream ID into header buffer
    uint8_t* end_pos = wtf_varint_encode(quarter_stream_id, header_buffer);
    if (end_pos - header_buffer != header_size) {
        result = WTF_ERROR_INTERNAL;
        goto cleanup;
    }

    new_buffers[0].data = header_buffer;
    new_buffers[0].length = (uint32_t)header_size;

    for (uint32_t i = 0; i < buffer_count; i++) {
        new_buffers[i + 1].data = data[i].data;
        new_buffers[i + 1].length = data[i].length;
    }

    send_ctx->buffers = new_buffers;
    send_ctx->count = buffer_count + 1;

    QUIC_STATUS status = conn->server->context->quic_api->DatagramSend(
        conn->quic_connection, (QUIC_BUFFER*)new_buffers, buffer_count + 1, QUIC_SEND_FLAG_NONE,
        send_ctx);

    if (QUIC_SUCCEEDED(status)) {
        return WTF_SUCCESS;
    }

    result = wtf_quic_status_to_result(status);
    goto cleanup;

cleanup:
    if (send_ctx) {
        free(send_ctx);
    }
    if (new_buffers) {
        free(new_buffers);
    }
    if (header_buffer) {
        free(header_buffer);
    }
    return result;
}

wtf_result_t wtf_session_create_stream(wtf_session_t* session, wtf_stream_type_t type,
                                       wtf_stream_t** stream)
{
    if (!session || !stream) {
        return WTF_ERROR_INVALID_PARAMETER;
    }

    wtf_session* sess = (wtf_session*)session;

    if (sess->state != WTF_SESSION_CONNECTED) {
        return WTF_ERROR_INVALID_STATE;
    }

    mtx_lock(&sess->streams_mutex);
    if (stream_map_size(&sess->streams) >= sess->max_streams) {
        mtx_unlock(&sess->streams_mutex);
        WTF_LOG_WARN(sess->connection->server->context, "stream",
                     "Session %llu has reached stream limit %u", (unsigned long long)sess->id,
                     sess->max_streams);
        return WTF_ERROR_FLOW_CONTROL;
    }
    mtx_unlock(&sess->streams_mutex);

    WTF_LOG_DEBUG(sess->connection->server->context, "stream",
                  "Creating %s WebTransport stream on session %llu",
                  type == WTF_STREAM_BIDIRECTIONAL ? "bidirectional" : "unidirectional",
                  (unsigned long long)sess->id);

    wtf_stream* wt_stream = wtf_stream_create(sess, UINT64_MAX, type);
    if (!wt_stream) {
        return WTF_ERROR_OUT_OF_MEMORY;
    }

    wtf_connection* conn = sess->connection;

    uint32_t stream_open_flags = QUIC_STREAM_OPEN_FLAG_NONE;
    if (type == WTF_STREAM_UNIDIRECTIONAL) {
        stream_open_flags = QUIC_STREAM_OPEN_FLAG_UNIDIRECTIONAL;
    }

    HQUIC quic_stream;
    QUIC_STATUS status = conn->server->context->quic_api->StreamOpen(
        conn->quic_connection, stream_open_flags, wtf_upgraded_stream_callback, wt_stream,
        &quic_stream);

    if (QUIC_FAILED(status)) {
        WTF_LOG_ERROR(conn->server->context, "stream",
                      "StreamOpen failed for WebTransport %s stream on session %llu: 0x%x",
                      type == WTF_STREAM_BIDIRECTIONAL ? "bidirectional" : "unidirectional",
                      (unsigned long long)sess->id, status);
        wtf_stream_destroy(wt_stream);
        return wtf_quic_status_to_result(status);
    }

    wt_stream->quic_stream = quic_stream;

    status = conn->server->context->quic_api->StreamStart(quic_stream, QUIC_STREAM_START_FLAG_NONE);
    if (QUIC_FAILED(status) && status != QUIC_STATUS_PENDING) {
        WTF_LOG_ERROR(conn->server->context, "stream",
                      "StreamStart failed for WebTransport stream: 0x%x", status);
        conn->server->context->quic_api->StreamClose(quic_stream);
        wtf_stream_destroy(wt_stream);
        return wtf_quic_status_to_result(status);
    }

    WTF_LOG_INFO(conn->server->context, "stream",
                 "WebTransport %s stream created and started on session %llu",
                 type == WTF_STREAM_BIDIRECTIONAL ? "bidirectional" : "unidirectional",
                 (unsigned long long)sess->id);

    *stream = (wtf_stream_t*)wt_stream;
    return WTF_SUCCESS;
}

wtf_result_t wtf_session_drain(wtf_session_t* session)
{
    if (!session) {
        return WTF_ERROR_INVALID_PARAMETER;
    }

    wtf_session* sess = (wtf_session*)session;

    if (sess->state != WTF_SESSION_CONNECTED) {
        return WTF_ERROR_INVALID_STATE;
    }

    WTF_LOG_INFO(sess->connection->server->context, "session", "Draining session %llu",
                 (unsigned long long)sess->id);

    sess->state = WTF_SESSION_DRAINING;

    if (sess->callback) {
        wtf_session_event_t event = {.type = WTF_SESSION_EVENT_DRAINING,
                                     .session = session,
                                     .user_context = sess->user_context};
        sess->callback(&event);
    }

    return wtf_session_send_capsule(sess, WTF_CAPSULE_DRAIN_WEBTRANSPORT_SESSION, NULL, 0);
}

wtf_stream_t* wtf_session_find_stream_by_id(wtf_session_t* session, uint64_t stream_id)
{
    if (!session)
        return NULL;

    wtf_session* sess = (wtf_session*)session;

    mtx_lock(&sess->streams_mutex);

    stream_map_itr itr = stream_map_get(&sess->streams, stream_id);
    wtf_stream* stream = NULL;
    if (!stream_map_is_end(itr)) {
        stream = itr.data->val;
    }

    mtx_unlock(&sess->streams_mutex);
    return (wtf_stream_t*)stream;
}

wtf_result_t wtf_session_get_peer_address(wtf_session_t* session, void* address_buffer,
                                          size_t* buffer_size)
{
    if (!session || !buffer_size) {
        return WTF_ERROR_INVALID_PARAMETER;
    }

    wtf_session* sess = (wtf_session*)session;

    if (*buffer_size < sizeof(sess->connection->peer_address)) {
        *buffer_size = sizeof(sess->connection->peer_address);
        return WTF_ERROR_BUFFER_TOO_SMALL;
    }

    if (address_buffer) {
        memcpy(address_buffer, &sess->connection->peer_address,
               sizeof(sess->connection->peer_address));
    }
    *buffer_size = sizeof(sess->connection->peer_address);

    return WTF_SUCCESS;
}

wtf_session_state_t wtf_session_get_state(wtf_session_t* session)
{
    if (!session)
        return WTF_SESSION_CLOSED;
    return ((wtf_session*)session)->state;
}
