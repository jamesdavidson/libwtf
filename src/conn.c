#include "conn.h"

#include "datagram.h"
#include "http3.h"
#include "log.h"
#include "qpack.h"
#include "session.h"
#include "settings.h"
#include "stream.h"
#include "utils.h"
#include "varint.h"

bool wtf_connection_associate_stream_with_session(wtf_connection* conn, wtf_http3_stream* h3_stream,
                                                  wtf_session* session)
{
    if (!conn || !h3_stream || !session)
        return false;

    uint64_t stream_id = h3_stream->id;
    uint64_t session_id = session->id;

    mtx_lock(&session->streams_mutex);
    if (stream_map_size(&session->streams) >= session->max_streams) {
        mtx_unlock(&session->streams_mutex);
        WTF_LOG_WARN(conn->server->context, "stream",
                     "Session %llu has reached stream limit %u - rejecting stream %llu",
                     (unsigned long long)session_id, session->max_streams,
                     (unsigned long long)stream_id);

        if (h3_stream->quic_stream) {
            conn->server->context->quic_api->StreamShutdown(
                h3_stream->quic_stream,
                QUIC_STREAM_SHUTDOWN_FLAG_ABORT_SEND | QUIC_STREAM_SHUTDOWN_FLAG_ABORT_RECEIVE,
                WTF_WEBTRANSPORT_BUFFERED_STREAM_REJECTED);
        }

        return false;
    }

    stream_map_itr existing_itr = stream_map_get(&session->streams, stream_id);
    if (!stream_map_is_end(existing_itr)) {
        mtx_unlock(&session->streams_mutex);
        WTF_LOG_TRACE(conn->server->context, "stream",
                      "Stream %llu already associated with session %llu",
                      (unsigned long long)stream_id, (unsigned long long)session_id);
        return true;
    }
    mtx_unlock(&session->streams_mutex);

    wtf_stream_type_t stream_type = WTF_STREAM_IS_UNIDIRECTIONAL(stream_id)
        ? WTF_STREAM_UNIDIRECTIONAL
        : WTF_STREAM_BIDIRECTIONAL;

    wtf_stream* wt_stream = wtf_stream_create(session, stream_id, stream_type);
    if (!wt_stream) {
        WTF_LOG_ERROR(conn->server->context, "stream",
                      "Failed to create WebTransport stream for stream %llu",
                      (unsigned long long)stream_id);
        return false;
    }

    if (session->callback) {
        wt_stream->callback = NULL;
        wt_stream->user_context = session->user_context;
    }

    mtx_lock(&session->streams_mutex);
    stream_map_itr itr = stream_map_insert(&session->streams, stream_id, wt_stream);
    if (stream_map_is_end(itr)) {
        mtx_unlock(&session->streams_mutex);
        WTF_LOG_ERROR(conn->server->context, "stream", "Failed to add stream to map");
        wtf_stream_destroy(wt_stream);
        return false;
    }
    mtx_unlock(&session->streams_mutex);

    if (h3_stream->quic_stream) {
        wt_stream->quic_stream = h3_stream->quic_stream;

        bool is_connect_stream = (h3_stream->id == session->connect_stream->id);

        if (!is_connect_stream) {
            conn->server->context->quic_api->SetCallbackHandler(
                h3_stream->quic_stream, wtf_upgraded_stream_callback, wt_stream);

            WTF_LOG_DEBUG(conn->server->context, "stream",
                          "Transferred QUIC handle and switched callback for "
                          "WebTransport stream %llu",
                          (unsigned long long)stream_id);
        } else {
            WTF_LOG_DEBUG(conn->server->context, "stream",
                          "Transferred QUIC handle from CONNECT stream %llu "
                          "(preserving wtf_stream_callback)",
                          (unsigned long long)stream_id);
        }
    }

    WTF_LOG_INFO(conn->server->context, "stream",
                 "Created and associated WebTransport stream %llu (%s) with session %llu",
                 (unsigned long long)stream_id,
                 stream_type == WTF_STREAM_BIDIRECTIONAL ? "bidirectional" : "unidirectional",
                 (unsigned long long)session_id);

    if (session->callback) {
        wtf_session_event_t event = {
            .type = WTF_SESSION_EVENT_STREAM_OPENED,
            .session = (wtf_session_t*)session,
            .user_context = session->user_context,
            .stream_opened = {.stream = (wtf_stream_t*)wt_stream, .stream_type = stream_type}};
        session->callback(&event);
    }

    return true;
}

void wtf_connection_process_buffered_data(wtf_connection* conn, wtf_session* session)
{
    if (!conn || !session)
        return;

    mtx_lock(&conn->buffered_mutex);

    wtf_datagram** dgram_ptr = &conn->buffered_datagrams;
    while (*dgram_ptr) {
        wtf_datagram* dgram = *dgram_ptr;

        if (dgram->session_id == session->id) {
            *dgram_ptr = dgram->next;
            conn->buffered_datagram_count--;

            mtx_unlock(&conn->buffered_mutex);
            wtf_session_process_datagram(session, dgram->data, dgram->length);
            wtf_datagram_destroy(dgram);
            mtx_lock(&conn->buffered_mutex);

            dgram_ptr = &conn->buffered_datagrams;
        } else {
            dgram_ptr = &(*dgram_ptr)->next;
        }
    }

    uint64_t streams_to_process[WTF_MAX_BUFFERED_STREAMS];
    size_t stream_count_to_process = 0;

    for (http3_stream_map_itr itr = http3_stream_map_first(&conn->buffered_streams);
         !http3_stream_map_is_end(itr) && stream_count_to_process < WTF_MAX_BUFFERED_STREAMS;
         itr = http3_stream_map_next(itr)) {
        wtf_http3_stream* stream = itr.data->val;
        bool stream_belongs = false;

        if (stream->buffered_frames_length > 0) {
            stream_belongs = wtf_stream_belongs_to_session(
                stream->id, session->id, stream->buffered_frames, stream->buffered_frames_length);
        }

        if (stream_belongs) {
            streams_to_process[stream_count_to_process++] = stream->id;
        }
    }

    for (size_t i = 0; i < stream_count_to_process; i++) {
        uint64_t stream_id = streams_to_process[i];
        http3_stream_map_itr buffered_itr = http3_stream_map_get(
            &conn->buffered_streams, stream_id);

        if (!http3_stream_map_is_end(buffered_itr)) {
            wtf_http3_stream* stream = buffered_itr.data->val;

            http3_stream_map_erase(&conn->buffered_streams, stream_id);
            conn->buffered_stream_count--;

            mtx_unlock(&conn->buffered_mutex);

            mtx_lock(&conn->streams_mutex);
            http3_stream_map_itr itr = http3_stream_map_insert(&conn->streams, stream->id, stream);
            if (!http3_stream_map_is_end(itr)) {
                stream->webtransport_session = session;
                stream->is_webtransport = true;
            }
            mtx_unlock(&conn->streams_mutex);

            uint16_t offset = 0;
            uint64_t frame_or_type;
            uint64_t parsed_session_id;

            if (wtf_varint_decode(stream->buffered_frames_length, stream->buffered_frames, &offset,
                                  &frame_or_type)
                && wtf_varint_decode(stream->buffered_frames_length, stream->buffered_frames,
                                     &offset, &parsed_session_id)) {
                wtf_connection_associate_stream_with_session(conn, stream, session);
            }

            mtx_lock(&conn->buffered_mutex);
        }
    }

    mtx_unlock(&conn->buffered_mutex);

    WTF_LOG_DEBUG(conn->server->context, "session", "Processed buffered data for session %llu",
                  (unsigned long long)session->id);
}

wtf_connection* wtf_connection_create(wtf_server* server, HQUIC quic_connection)
{
    wtf_connection* conn = malloc(sizeof(wtf_connection));
    if (!conn) {
        return NULL;
    }

    memset(conn, 0, sizeof(*conn));
    uint64_t connection_id = 0;
    if (!wtf_random_uint64(&connection_id)) {
        free(conn);
        return NULL;
    }
    conn->id = connection_id;
    conn->quic_connection = quic_connection;
    conn->server = server;
    conn->state = WTF_CONNECTION_STATE_HANDSHAKING;
    conn->max_sessions = server->config.max_sessions_per_connection > 0
        ? server->config.max_sessions_per_connection
        : WTF_DEFAULT_MAX_SESSIONS;
    conn->max_datagram_size = WTF_MAX_DATAGRAM_SIZE;

    wtf_settings_init(&conn->local_settings);
    wtf_settings_init(&conn->peer_settings);

    session_map_init(&conn->sessions);
    http3_stream_map_init(&conn->streams);
    http3_stream_map_init(&conn->buffered_streams);

    if (mtx_init(&conn->streams_mutex, mtx_plain) != thrd_success) {
        session_map_cleanup(&conn->sessions);
        http3_stream_map_cleanup(&conn->streams);
        http3_stream_map_cleanup(&conn->buffered_streams);
        free(conn);
        return NULL;
    }

    if (mtx_init(&conn->sessions_mutex, mtx_plain) != thrd_success) {
        mtx_destroy(&conn->streams_mutex);
        session_map_cleanup(&conn->sessions);
        http3_stream_map_cleanup(&conn->streams);
        http3_stream_map_cleanup(&conn->buffered_streams);
        free(conn);
        return NULL;
    }

    if (mtx_init(&conn->buffered_mutex, mtx_plain) != thrd_success) {
        mtx_destroy(&conn->sessions_mutex);
        mtx_destroy(&conn->streams_mutex);
        session_map_cleanup(&conn->sessions);
        http3_stream_map_cleanup(&conn->streams);
        http3_stream_map_cleanup(&conn->buffered_streams);
        free(conn);
        return NULL;
    }

    if (!wtf_qpack_preinit(&conn->qpack, WTF_QPACK_DYNAMIC_TABLE_SIZE,
                           WTF_QPACK_MAX_BLOCKED_STREAMS)) {
        mtx_destroy(&conn->buffered_mutex);
        mtx_destroy(&conn->sessions_mutex);
        mtx_destroy(&conn->streams_mutex);
        session_map_cleanup(&conn->sessions);
        http3_stream_map_cleanup(&conn->streams);
        http3_stream_map_cleanup(&conn->buffered_streams);
        free(conn);
        return NULL;
    }

    uint32_t addr_size = sizeof(conn->peer_address);
    server->context->quic_api->GetParam(quic_connection, QUIC_PARAM_CONN_REMOTE_ADDRESS, &addr_size,
                                        &conn->peer_address);
    return conn;
}

void wtf_connection_destroy(wtf_connection* conn)
{
    if (!conn)
        return;

    mtx_lock(&conn->sessions_mutex);
    for (session_map_itr itr = session_map_first(&conn->sessions); !session_map_is_end(itr);
         itr = session_map_next(itr)) {
        wtf_session_destroy(itr.data->val);
    }
    session_map_cleanup(&conn->sessions);
    mtx_unlock(&conn->sessions_mutex);

    mtx_lock(&conn->streams_mutex);
    for (http3_stream_map_itr itr = http3_stream_map_first(&conn->streams);
         !http3_stream_map_is_end(itr); itr = http3_stream_map_next(itr)) {
        wtf_http3_stream_destroy(itr.data->val);
    }
    http3_stream_map_cleanup(&conn->streams);
    mtx_unlock(&conn->streams_mutex);

    mtx_lock(&conn->buffered_mutex);
    for (http3_stream_map_itr itr = http3_stream_map_first(&conn->buffered_streams);
         !http3_stream_map_is_end(itr); itr = http3_stream_map_next(itr)) {
        wtf_http3_stream_destroy(itr.data->val);
    }
    http3_stream_map_cleanup(&conn->buffered_streams);

    wtf_datagram* buffered_dgram = conn->buffered_datagrams;
    while (buffered_dgram) {
        wtf_datagram* next = buffered_dgram->next;
        wtf_datagram_destroy(buffered_dgram);
        buffered_dgram = next;
    }
    mtx_unlock(&conn->buffered_mutex);

    wtf_qpack_cleanup(&conn->qpack);
    mtx_destroy(&conn->buffered_mutex);
    mtx_destroy(&conn->sessions_mutex);
    mtx_destroy(&conn->streams_mutex);
    free(conn);
}

QUIC_STATUS QUIC_API wtf_connection_callback(HQUIC Connection, void* Context,
                                             QUIC_CONNECTION_EVENT* Event)
{
    wtf_connection* conn = (wtf_connection*)Context;

    if (!conn || !conn->server || !conn->server->context) {
        return QUIC_STATUS_INTERNAL_ERROR;
    }

    switch (Event->Type) {
        case QUIC_CONNECTION_EVENT_CONNECTED: {
            WTF_LOG_INFO(conn->server->context, "conn",
                         "Connection established - starting HTTP/3 handshake");

            if (Event->CONNECTED.NegotiatedAlpnLength > 0 && Event->CONNECTED.NegotiatedAlpn) {
                char alpn_str[256];
                size_t copy_len = min(Event->CONNECTED.NegotiatedAlpnLength, sizeof(alpn_str) - 1);
                memcpy(alpn_str, Event->CONNECTED.NegotiatedAlpn, copy_len);
                alpn_str[copy_len] = '\0';
                WTF_LOG_TRACE(conn->server->context, "conn", "Negotiated ALPN: %s", alpn_str);
            }

            if (Event->CONNECTED.SessionResumed) {
                WTF_LOG_TRACE(conn->server->context, "conn", "Session resumed");
            }

            if (!wtf_http3_create_control_stream(conn)) {
                WTF_LOG_ERROR(conn->server->context, "conn", "Failed to create control stream");
                conn->server->context->quic_api->ConnectionShutdown(
                    Connection, QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, WTF_H3_INTERNAL_ERROR);
                return QUIC_STATUS_SUCCESS;
            }

            if (!wtf_http3_create_qpack_streams(conn)) {
                WTF_LOG_ERROR(conn->server->context, "conn", "Failed to create QPACK streams");
                conn->server->context->quic_api->ConnectionShutdown(
                    Connection, QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, WTF_H3_INTERNAL_ERROR);
                return QUIC_STATUS_SUCCESS;
            }

            return QUIC_STATUS_SUCCESS;
        }

        case QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED: {
            WTF_LOG_DEBUG(conn->server->context, "conn", "Peer started new stream");

            wtf_http3_stream* stream = wtf_http3_stream_create(
                conn, Event->PEER_STREAM_STARTED.Stream, UINT64_MAX);
            if (!stream) {
                WTF_LOG_ERROR(conn->server->context, "conn",
                              "Failed to create peer stream context");
                conn->server->context->quic_api->StreamShutdown(
                    Event->PEER_STREAM_STARTED.Stream,
                    QUIC_STREAM_SHUTDOWN_FLAG_ABORT_SEND | QUIC_STREAM_SHUTDOWN_FLAG_ABORT_RECEIVE,
                    WTF_H3_INTERNAL_ERROR);
                return QUIC_STATUS_SUCCESS;
            }

            conn->server->context->quic_api->SetCallbackHandler(Event->PEER_STREAM_STARTED.Stream,
                                                                wtf_http3_stream_callback, stream);
            return QUIC_STATUS_SUCCESS;
        }

        case QUIC_CONNECTION_EVENT_DATAGRAM_RECEIVED: {
            const uint8_t* data = Event->DATAGRAM_RECEIVED.Buffer->Buffer;
            uint32_t length = Event->DATAGRAM_RECEIVED.Buffer->Length;

            wtf_datagram_process(conn, data, length, true);
            return QUIC_STATUS_SUCCESS;
        }

        case QUIC_CONNECTION_EVENT_DATAGRAM_SEND_STATE_CHANGED: {
            if (Event->DATAGRAM_SEND_STATE_CHANGED.ClientContext) {
                wtf_internal_send_context* send_ctx
                    = (wtf_internal_send_context*)Event->DATAGRAM_SEND_STATE_CHANGED.ClientContext;
                if (send_ctx) {
                    wtf_session* session = send_ctx->session;

                    if (session && session->callback) {
                        wtf_session_event_t event = {
                            .type = WTF_SESSION_EVENT_DATAGRAM_SEND_STATE_CHANGE,
                            .session = session,
                            .user_context = session->user_context,
                            .datagram_send_state_changed = {
                                .buffers = send_ctx->count > 1 ? &send_ctx->buffers[1] : NULL,
                                .buffer_count = send_ctx->count > 0 ? send_ctx->count - 1 : 0,
                                .state = (wtf_datagram_send_state_t)
                                             Event->DATAGRAM_SEND_STATE_CHANGED.State}};
                        session->callback(&event);
                    } else {
                        if (QUIC_DATAGRAM_SEND_STATE_IS_FINAL(Event->DATAGRAM_SEND_STATE_CHANGED.State)) {
                            for (uint32_t i = 1; i < send_ctx->count; i++) {
                                if (send_ctx->buffers[i].data) {
                                    free(send_ctx->buffers[i].data);
                                }
                            }
                        }
                    }

                    if (QUIC_DATAGRAM_SEND_STATE_IS_FINAL(
                            Event->DATAGRAM_SEND_STATE_CHANGED.State)) {
                        if (send_ctx->buffers && send_ctx->count > 0 && send_ctx->buffers[0].data) {
                            free(send_ctx->buffers[0].data);
                        }
                        free(send_ctx->buffers);
                        free(send_ctx);
                    }
                }
            }
            return QUIC_STATUS_SUCCESS;
        }

        case QUIC_CONNECTION_EVENT_PEER_CERTIFICATE_RECEIVED: {
            return QUIC_STATUS_SUCCESS;
        }

        case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT:
        case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER: {
            uint64_t error_code = Event->SHUTDOWN_INITIATED_BY_PEER.ErrorCode;
            WTF_LOG_INFO(conn->server->context, "conn",
                         "Connection shutdown initiated: error=0x%llx",
                         (unsigned long long)error_code);

            conn->state = WTF_CONNECTION_STATE_CLOSING;

            mtx_lock(&conn->sessions_mutex);
            for (session_map_itr itr = session_map_first(&conn->sessions); !session_map_is_end(itr);
                 itr = session_map_next(itr)) {
                wtf_session* session = itr.data->val;
                if (session->callback && session->state == WTF_SESSION_CONNECTED) {
                    session->state = WTF_SESSION_CLOSED;
                    wtf_session_event_t event = {
                        .type = WTF_SESSION_EVENT_DISCONNECTED,
                        .session = (wtf_session_t*)session,
                        .user_context = session->user_context,
                        .disconnected = {.error_code = (uint32_t)error_code,
                                         .reason = "Connection shutdown"}};
                    session->callback(&event);
                }
            }
            mtx_unlock(&conn->sessions_mutex);

            return QUIC_STATUS_SUCCESS;
        }

        case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE: {
            WTF_LOG_INFO(conn->server->context, "conn", "Connection shutdown complete");

            conn->state = WTF_CONNECTION_STATE_CLOSED;

            mtx_lock(&conn->server->connections_mutex);
            connection_map_erase(&conn->server->connections, conn->id);
            mtx_unlock(&conn->server->connections_mutex);

            wtf_connection_destroy(conn);
            return QUIC_STATUS_SUCCESS;
        }
        case QUIC_CONNECTION_EVENT_IDEAL_PROCESSOR_CHANGED: {
            WTF_LOG_INFO(conn->server->context, "conn", "Ideal processor changed to %d",
                         Event->IDEAL_PROCESSOR_CHANGED.IdealProcessor);
            return QUIC_STATUS_SUCCESS;
        }

        case QUIC_CONNECTION_EVENT_DATAGRAM_STATE_CHANGED: {
            return QUIC_STATUS_SUCCESS;
        }

        case QUIC_CONNECTION_EVENT_RESUMPTION_TICKET_RECEIVED: {
            WTF_LOG_DEBUG(conn->server->context, "conn", "Resumption ticket received");

            return QUIC_STATUS_SUCCESS;
        }

        default:
            WTF_LOG_DEBUG(conn->server->context, "conn", "Unhandled connection event: %d",
                          Event->Type);
            return QUIC_STATUS_SUCCESS;
    }
}

wtf_session* wtf_connection_find_session(wtf_connection* conn, uint64_t session_id)
{
    if (!conn)
        return NULL;

    mtx_lock(&conn->sessions_mutex);

    session_map_itr itr = session_map_get(&conn->sessions, session_id);
    wtf_session* session = NULL;
    if (!session_map_is_end(itr)) {
        session = itr.data->val;
    }

    mtx_unlock(&conn->sessions_mutex);
    return session;
}
